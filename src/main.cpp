#include <M5Unified.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <esp_wifi.h>
#include <atomic>
#include "../firmware/hangul.h"
#include "../firmware/editor_layout.h"
#include "../firmware/submission.h"
#include "../firmware/navigation.h"
#include "../firmware/snowball_icon.h"

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static WiFiUDP discovery;
static WiFiServer reverseServer(47774);
static WiFiClient reverseClient;
static bool reverseReady=false;
static String reverseLine,reverseChallenge;
static uint32_t reverseStarted=0;
static snowball::TextInput input;
static DynamicJsonDocument remoteView(12288);
static String deviceId,controllerId,pairKey,hostIp,hostEpoch,bootId,discoverNonce,serialLine,wifiSsid,wifiPassword,notice="USB 연결 또는 Wi-Fi 설정";
static uint16_t hostPort=47771;
static uint32_t requestId=0,wireSeq=0,pendingId=0,lastUsb=0,lastPoll=0,lastHello=0,lastDiscovery=0,lastResponse=0,wifiStarted=0;
static bool faces=false,dirty=true,pending=false,pendingSend=false,scanRunning=false,wifiEditing=false,contrast=false;
static bool pendingReverse=false;
static int cursor=0,readerOffset=0,scanCount=0;
struct WifiNetwork {String ssid;int32_t rssi=0;bool secure=false;};
static WifiNetwork scanNetworks[24];
static bool scanActive=false,resumeAfterScan=false;
static uint32_t scanStarted=0,scanAttemptAt=0;
static uint32_t scanDuration=0;
static std::atomic<int> scanEventStatus{-1};
static std::atomic<bool> wifiAssociationReady{false};
static int scanAttempts=0,scanLastCode=0,scanTotal=0;
static String scanNotice;
static bool passwordVisible=false;
static String commandId,draftSession,pendingOp,expectedMenu,editorNotice;
static snowball::Navigation nav;
static snowball::ButtonGesture gestures[3];
static bool listInitial=false,followTail=false,layoutOk=true;
static int settingsIndex=0,wifiIndex=0;
static uint32_t nextContentFetch=0;

enum Page { SESSION_CONTENT=0, HOME=0, READ_REPLY=0, REMOTE_LIST=1, COMPOSE=2, CONFIRM=3, WIFI_LIST=4, WIFI_PASSWORD=5, INFO=7, SETTINGS=8, ACTIONS=9 };
static Page page=SESSION_CONTENT;
static const char* settingsItems[]={"Wi-Fi 설정","미들웨어 검색","화면 테마","기기 정보"};
static const char* actionItems[]={"키보드 입력 / Compose","모델 / Model","Effort","새로고침 / Refresh","내용으로 / Back"};
void back();void draw();void select();void returnSession(bool top=false,bool refresh=true);void syncNavigation();
bool wifiUi(){return page==WIFI_LIST||page==WIFI_PASSWORD||scanRunning;}

String randomHex(size_t bytes){String s;for(size_t i=0;i<bytes;i++){uint8_t b=esp_random()&255;char h[3];snprintf(h,3,"%02x",b);s+=h;}return s;}
String hmac(const String &value){uint8_t result[32];mbedtls_md_context_t ctx;mbedtls_md_init(&ctx);mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);mbedtls_md_hmac_starts(&ctx,(const unsigned char*)pairKey.c_str(),pairKey.length());mbedtls_md_hmac_update(&ctx,(const unsigned char*)value.c_str(),value.length());mbedtls_md_hmac_finish(&ctx,result);mbedtls_md_free(&ctx);String s;for(int i=0;i<32;i++){char h[3];snprintf(h,3,"%02x",result[i]);s+=h;}return s;}
bool safeEqual(const String&a,const String&b){if(a.length()!=b.length())return false;uint8_t difference=0;for(size_t i=0;i<a.length();i++)difference|=a[i]^b[i];return difference==0;}
bool hex(const String&s,size_t length){if(s.length()!=length)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
String material(const char* direction,uint32_t seq,const String&payload){return String(direction)+"\n"+hostEpoch+"\n"+bootId+"\n"+String(seq)+"\n"+payload;}
void serialJson(JsonDocument&doc){serializeJson(doc,Serial);Serial.println();}
void hello(){StaticJsonDocument<384>d;d["type"]="hello";d["deviceId"]=deviceId;d["board"]="M5Stack";d["faces"]=faces;d["flashBytes"]=ESP.getFlashChipSize();d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");d["firmware"]="0.2.2";serialJson(d);lastHello=millis();}
bool usbOnline(){return lastUsb&&millis()-lastUsb<6500;}
void toggleInputLanguage(){input.toggle();prefs.putBool("korean",input.korean);}
bool middlewareOnline(){return lastResponse&&millis()-lastResponse<10000&&remoteView["connected"].as<bool>();}
void acceptView(JsonVariantConst view){
  if(view["connected"].as<bool>()&&(view["deviceId"].as<String>()!=deviceId||view["controllerId"].as<String>()!=controllerId)){notice="기기 상태 대상 불일치";pending=false;dirty=true;return;}
  String previous=remoteView["sessionKey"]|"",acceptedOp=pendingOp;pendingOp="";
  remoteView.clear();remoteView.set(view);lastPoll=lastResponse=millis();pending=false;
  notice=String(remoteView["message"]|"");contrast=String(remoteView["skinId"]|"")=="high-contrast";
  pendingSend=false;
  if(snowball::ownJournalAdmission(commandId.c_str(),remoteView["commandId"]|"",remoteView["commandStatus"]|"unconfirmed",remoteView["connected"].as<bool>())){
    input.clear();draftSession="";commandId="";returnSession(false,false);followTail=true;
  }
  if(page==REMOTE_LIST){
    String kind=remoteView["menuKind"]|"";
    if(acceptedOp=="select"&&!kind.isEmpty()){expectedMenu=kind;listInitial=true;nav.returnCrumb=kind=="sessions"?4:kind=="projects"?3:kind=="harnesses"?2:1;}
    if(kind==expectedMenu){nav.configure(remoteView["menuTotal"]|0,7,false);if(listInitial){nav.index=remoteView["menuIndex"]|0;nav.focus=snowball::Focus::Content;listInitial=false;}}
    else if(acceptedOp=="select"&&kind.isEmpty())returnSession();
  }
  if(page==SESSION_CONTENT){
    if(previous!=String(remoteView["sessionKey"]|"")){nav.index=remoteView["contentOffset"]|0;followTail=false;}
    nav.configure(remoteView["contentTotal"]|0,11,true);if(followTail)nav.edge(true);
  }
  cursor=nav.index;readerOffset=page==SESSION_CONTENT?nav.index:readerOffset;dirty=true;
}
void request(const char* op,int index=-1){
  if(String(op)=="poll"&&wifiUi())return;
  if(pending){notice="응답 대기 중 / Waiting";dirty=true;return;}
  pendingOp=op;DynamicJsonDocument action(4096);action["op"]=op;if(index>=0)action["index"]=index;
  bool sending=String(op)=="send";
  if(sending){action["text"]=input.text().c_str();action["commandId"]=commandId;}
  if(usbOnline()){
    DynamicJsonDocument packet(4608);packet["type"]="request";packet["deviceId"]=deviceId;packet["id"]=++requestId;packet["action"]=action;
    serialJson(packet);pendingId=requestId;pending=true;pendingReverse=false;pendingSend=sending;lastPoll=millis();notice=sending?"전송 확인 중 / Sending":"조회 중 / Loading";dirty=true;return;
  }
  if(WiFi.status()!=WL_CONNECTED||pairKey.length()!=64||hostIp.isEmpty()||hostEpoch.isEmpty()){
    notice="미들웨어 미연결 / Offline";dirty=true;return;
  }
  String payload;serializeJson(action,payload);uint32_t seq=++wireSeq;
  DynamicJsonDocument envelope(6144);envelope["epoch"]=hostEpoch;envelope["boot"]=bootId;envelope["seq"]=seq;envelope["payload"]=payload;envelope["mac"]=hmac(material("request",seq,payload));String raw;serializeJson(envelope,raw);
  if(reverseReady&&reverseClient.connected()){
    reverseClient.print(raw);reverseClient.print('\n');pending=true;pendingReverse=true;pendingSend=sending;pendingId=seq;lastPoll=millis();notice=sending?"전송 확인 중 / Sending":"조회 중 / Loading";dirty=true;return;
  }
  HTTPClient http;WiFiClient client;http.setConnectTimeout(1500);http.setTimeout(4500);http.begin(client,"http://"+hostIp+":"+String(hostPort)+"/device");http.addHeader("Content-Type","application/json");int status=http.POST(raw);
  if(status==200&&http.getSize()<=24000){
    String response=http.getString();DynamicJsonDocument reply(24576);
    if(!deserializeJson(reply,response)&&reply["seq"].as<uint32_t>()==seq&&reply["boot"].as<String>()==bootId&&reply["epoch"].as<String>()==hostEpoch){
      String body=reply["payload"].as<String>();if(safeEqual(reply["mac"].as<String>(),hmac(material("response",seq,body)))){
        DynamicJsonDocument view(12288);if(!deserializeJson(view,body)){pendingSend=sending;acceptView(view.as<JsonVariantConst>());http.end();lastPoll=millis();return;}
      }
    }
  }
  http.end();hostEpoch="";notice=sending?"전송 불명확. 상태를 확인하세요":"연결 실패. 다시 검색하세요";pendingSend=false;dirty=true;lastPoll=millis();
  // Never resend a mutating request automatically, including USB/LAN failover.
}
void readReverse(){
  if(WiFi.status()!=WL_CONNECTED)return;
  if(!reverseClient.connected()){
    if(reverseReady){reverseReady=false;hostEpoch="";}
    WiFiClient candidate=reverseServer.available();if(!candidate)return;
    reverseClient=candidate;reverseClient.setNoDelay(true);reverseLine="";reverseChallenge=randomHex(8);reverseStarted=millis();
    StaticJsonDocument<192>d;d["type"]="reverse-challenge";d["nonce"]=reverseChallenge;d["deviceId"]=deviceId;serializeJson(d,reverseClient);reverseClient.print('\n');
  }
  if(!reverseReady&&millis()-reverseStarted>2500){reverseClient.stop();return;}
  while(reverseClient.available()){
    char c=reverseClient.read();if(c=='\r')continue;if(c!='\n'){if(reverseLine.length()>=24000){reverseClient.stop();reverseLine="";return;}reverseLine+=c;continue;}
    DynamicJsonDocument d(24576);DeserializationError err=deserializeJson(d,reverseLine);reverseLine="";if(err){reverseClient.stop();return;}
    if(!reverseReady){String epoch=d["epoch"].as<String>();if(String(d["type"]|"")!="reverse-auth"||d["nonce"].as<String>()!=reverseChallenge||!hex(epoch,32)||pairKey.length()!=64||!safeEqual(d["mac"].as<String>(),hmac("reverse\n"+reverseChallenge+"\n"+epoch))){reverseClient.stop();return;}
      hostEpoch=epoch;hostIp=reverseClient.remoteIP().toString();reverseReady=true;notice="PC 연결됨 / Wi-Fi";dirty=true;lastPoll=0;continue;
    }
    if(!pending||!pendingReverse||d["seq"].as<uint32_t>()!=pendingId||d["boot"].as<String>()!=bootId||d["epoch"].as<String>()!=hostEpoch)continue;
    String payload=d["payload"].as<String>();if(!safeEqual(d["mac"].as<String>(),hmac(material("response",pendingId,payload))))continue;
    DynamicJsonDocument v(12288);if(!deserializeJson(v,payload))acceptView(v.as<JsonVariantConst>());

  }
}
void discoverHost(){
  if(reverseReady&&reverseClient.connected()){notice="미들웨어 연결됨: "+hostIp;dirty=true;request("poll");return;}
  if(WiFi.status()!=WL_CONNECTED){notice="Wi-Fi 연결이 필요합니다";dirty=true;return;}
  if(pairKey.length()!=64){notice="USB로 최초 기기 등록이 필요합니다";dirty=true;return;}
  discoverNonce=randomHex(8);StaticJsonDocument<160>d;d["type"]="snowball.discover";d["nonce"]=discoverNonce;String raw;serializeJson(d,raw);
  IPAddress broadcast=WiFi.localIP();IPAddress mask=WiFi.subnetMask();for(int i=0;i<4;i++)broadcast[i]|=~mask[i];
  discovery.beginPacket(broadcast,47770);discovery.print(raw);discovery.endPacket();lastDiscovery=millis();notice="로컬 미들웨어 검색 중...";dirty=true;
}
void readDiscovery(){int size=discovery.parsePacket();if(size<=0)return;if(size>768){discovery.flush();return;}char buffer[769];int n=discovery.read(buffer,768);buffer[n]=0;StaticJsonDocument<1024>d;
  if(deserializeJson(d,buffer)||String(d["type"]|"")!="snowball.gateway"||d["nonce"].as<String>()!=discoverNonce||millis()-lastDiscovery>8000)return;
  String epoch=d["epoch"].as<String>(),ip=d["ip"].as<String>(),host=d["host"].as<String>();int port=d["port"]|0;IPAddress candidate;
  if(!hex(epoch,32)||!candidate.fromString(ip)||candidate!=discovery.remoteIP()||port<1024||port>65535)return;
  for(int i=0;i<4;i++)if((candidate[i]&WiFi.subnetMask()[i])!=(WiFi.localIP()[i]&WiFi.subnetMask()[i]))return;
  String proof="discover\n"+discoverNonce+"\n"+epoch+"\n"+ip+"\n"+String(port)+"\n"+host;
  if(!safeEqual(d["mac"].as<String>(),hmac(proof)))return;
  hostIp=ip;hostPort=port;hostEpoch=epoch;notice="미들웨어 발견: "+host;dirty=true;request("poll");
}
void resumeWifi(){
  if(!resumeAfterScan)return;resumeAfterScan=false;WiFi.setAutoReconnect(true);
  String saved=prefs.getString("ssid","");
  if(WiFi.status()!=WL_CONNECTED&&saved.length()){
    wifiSsid=saved;wifiPassword=prefs.getString("password","");wifiAssociationReady.store(false);WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str());wifiStarted=millis();
  }
}
void cancelScan(){
  if(scanActive)esp_wifi_scan_stop();scanActive=false;scanRunning=false;WiFi.scanDelete();
}
void finishScan(int count){
  scanDuration=millis()-scanStarted;scanLastCode=count;scanActive=false;scanRunning=false;
  if(count>=0){
    scanTotal=count;scanCount=min(count,24);cursor=0;if(page==WIFI_LIST)nav.list(scanCount+3,0,0);
    for(int i=0;i<scanCount;i++){scanNetworks[i].ssid=WiFi.SSID(i);scanNetworks[i].rssi=WiFi.RSSI(i);scanNetworks[i].secure=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;}
    scanNotice=count?String(count)+" AP / 2.4GHz":"2.4GHz AP 없음 / No APs";
  } else scanNotice="검색 실패 / Scan failed ("+String(count)+")";
  // Keep a local snapshot: reconnect and retries may discard driver scan records.
  WiFi.scanDelete();dirty=true;
  DynamicJsonDocument result(4096);result["type"]="wifi-networks";result["ok"]=count>=0;result["code"]=count;result["total"]=scanTotal;result["attempts"]=scanAttempts;result["durationMs"]=scanDuration;
  JsonArray networks=result.createNestedArray("networks");for(int i=0;i<scanCount;i++){JsonObject n=networks.createNestedObject();n["ssid"]=scanNetworks[i].ssid;n["rssi"]=scanNetworks[i].rssi;n["secure"]=scanNetworks[i].secure;}
  serialJson(result);resumeWifi();
}
void scanWifi(){
  if(pendingSend){notice="전송 확인 후 Wi-Fi 검색";dirty=true;return;}
  page=WIFI_LIST;nav.list(scanCount+3,0,0);dirty=true;
  // Repeated button presses must not clear a scan that is already in flight.
  if(scanRunning)return;
  WiFi.mode(WIFI_STA);WiFi.scanDelete();
  if(WiFi.status()!=WL_CONNECTED){
    // The ESP32 driver rejects scans while association/reconnect is in progress.
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);wifiStarted=0;resumeAfterScan=true;
  }
  scanRunning=true;scanActive=false;scanStarted=scanAttemptAt=millis();scanAttempts=0;scanLastCode=0;cursor=0;scanNotice="2.4GHz Wi-Fi 검색 중...";
}
void updateScan(){
  if(!scanRunning)return;
  if(millis()-scanStarted>12000){if(scanActive)esp_wifi_scan_stop();finishScan(WIFI_SCAN_FAILED);return;}
  if(!scanActive){
    if(millis()-scanAttemptAt<300)return;
    scanAttemptAt=millis();scanAttempts++;scanEventStatus.store(-1);int code=WiFi.scanNetworks(true,false,false,120);scanLastCode=code;
    if(code==WIFI_SCAN_RUNNING)scanActive=true;
    else if(code>=0)finishScan(code);
    else if(scanAttempts>=3)finishScan(code);
    return;
  }
  // Arduino 2.x derives its timeout from dwell time, which can expire before
  // a real background scan completes. Consume results only after SCAN_DONE.
  int eventStatus=scanEventStatus.load();if(eventStatus<0)return;
  finishScan(eventStatus==0?WiFi.scanComplete():WIFI_SCAN_FAILED);
}
void connectWifi(){
  if(wifiSsid.isEmpty()||wifiSsid.length()>32||wifiPassword.length()>63||(wifiPassword.length()>0&&wifiPassword.length()<8)){notice="SSID 또는 암호 길이를 확인하세요";dirty=true;return;}
  // Persist only after successful association; wrong credentials cannot erase
  // the last working network. Explicit Forget is the only deletion path.
  cancelScan();resumeAfterScan=false;WiFi.setAutoReconnect(true);WiFi.mode(WIFI_STA);
  // begin() can return the old WL_CONNECTED state before the disconnect event.
  // Only a matching existing association or a fresh GOT_IP may persist a key.
  wifiAssociationReady.store(WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiSsid&&WiFi.psk()==wifiPassword);
  WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str());wifiStarted=millis();notice="Wi-Fi 연결 중...";page=SESSION_CONTENT;nav.focus=snowball::Focus::Content;nav.index=readerOffset;syncNavigation();dirty=true;
}
void syncNavigation(){
  if(page==SESSION_CONTENT){nav.returnCrumb=4;nav.configure(remoteView["contentTotal"]|0,11,true);}
  else if(page==REMOTE_LIST)nav.configure(listInitial?0:(remoteView["menuTotal"]|0),7,false);
  else if(page==SETTINGS)nav.configure(4,7,false);
  else if(page==ACTIONS)nav.configure(5,7,false);
  else if(page==WIFI_LIST)nav.configure(scanCount+3,7,false);
  else if(page==COMPOSE){nav.focus=snowball::Focus::Content;nav.configure(1,1,false);}
  else if(page==WIFI_PASSWORD)nav.configure(wifiEditing?3:2,3,false);
  else if(page==CONFIRM)nav.configure(2,2,false);
  else nav.configure(8,11,true);
  cursor=nav.index;
}
void returnSession(bool top,bool refresh){
  if(scanRunning)cancelScan();resumeWifi();page=SESSION_CONTENT;
  nav.index=readerOffset;nav.focus=top?snowball::Focus::Top:snowball::Focus::Content;syncNavigation();
  expectedMenu="";listInitial=false;if(refresh)request("read",nav.index);dirty=true;
}
void openRemote(const char* kind,int origin){
  if(pending){notice="응답 대기 중";dirty=true;return;}
  page=REMOTE_LIST;expectedMenu=kind;listInitial=true;nav.list(0,0,origin);request(kind);dirty=true;
}
void beginCompose(){
  if(String(remoteView["sessionKey"]|"").isEmpty()){notice="네이티브 세션 없음";dirty=true;return;}
  if(input.text().empty())draftSession=remoteView["sessionKey"].as<String>();
  editorNotice="";page=COMPOSE;nav.list(1,0,4);dirty=true;
}
void back(){
  if(page==WIFI_PASSWORD){page=WIFI_LIST;passwordVisible=false;nav.list(scanCount+3,wifiIndex,0);dirty=true;return;}
  if(page==WIFI_LIST||page==INFO){if(scanRunning)cancelScan();resumeWifi();page=SETTINGS;nav.list(4,settingsIndex,0);dirty=true;return;}
  if(page==CONFIRM){page=COMPOSE;nav.list(1,0,4);dirty=true;return;}
  returnSession();
}
void select(){
  cursor=nav.index;
  if(nav.focus==snowball::Focus::Top){
    if(nav.crumb==0){page=SETTINGS;nav.list(4,settingsIndex,0);}
    else openRemote(nav.crumb==1?"machines":nav.crumb==2?"harnesses":nav.crumb==3?"projects":"sessions",nav.crumb);
  } else if(page==REMOTE_LIST){if(!listInitial&&!pending)request("select",cursor);}
  else if(page==SESSION_CONTENT)beginCompose();
  else if(page==SETTINGS){
    settingsIndex=cursor;
    if(cursor==0)scanWifi();else if(cursor==1)discoverHost();else if(cursor==2){contrast=!contrast;request("skin");}else {page=INFO;nav.list(8,0,0);nav.reader=true;}
  } else if(page==ACTIONS){
    if(cursor==0)beginCompose();else if(cursor==1)openRemote("models",4);else if(cursor==2)openRemote("efforts",4);else returnSession();
  } else if(page==COMPOSE){
    if(pending){editorNotice="전송 상태 확인 중";}
    else if(input.text().empty()){editorNotice="입력 내용이 없습니다";}
    else if(draftSession!=remoteView["sessionKey"].as<String>()){editorNotice="세션 변경: 기존 입력을 먼저 지우세요";}
    else if(remoteView["readOnly"]|true){editorNotice="읽기 전용 세션";}
    else if(commandId.length()){request("status");}
    else {commandId=randomHex(16);request("send");}
  } else if(page==CONFIRM){
    if(cursor==1)back();else if(draftSession!=remoteView["sessionKey"].as<String>()){notice="세션 변경. 전송 취소";back();}else request("send");
  } else if(page==WIFI_LIST){
    if(scanRunning)return;wifiIndex=cursor;
    if(cursor<scanCount){wifiSsid=scanNetworks[cursor].ssid;wifiPassword="";wifiEditing=true;passwordVisible=false;page=WIFI_PASSWORD;nav.list(3,0,0);notice="암호 입력 후 연결";}
    else if(cursor==scanCount)scanWifi();
    else if(cursor==scanCount+1){wifiSsid="";wifiPassword="";wifiEditing=false;passwordVisible=false;page=WIFI_PASSWORD;nav.list(2,0,0);notice="숨김 SSID 입력 후 Enter";}
    else {prefs.remove("ssid");prefs.remove("password");resumeAfterScan=false;WiFi.setAutoReconnect(false);WiFi.disconnect(true,true);wifiStarted=0;notice="저장한 Wi-Fi 삭제됨";back();}
  } else if(page==WIFI_PASSWORD){
    if(!wifiEditing){if(cursor==0){wifiEditing=true;nav.list(3,0,0);notice="Wi-Fi 암호 입력";}else back();}
    else if(cursor==0)connectWifi();else if(cursor==1){passwordVisible=!passwordVisible;}else back();
  } else back();
  syncNavigation();dirty=true;
}
void move(int delta,bool paging=false){
  syncNavigation();auto crossing=nav.move(delta,paging);cursor=nav.index;
  if(crossing==snowball::Crossing::Editor&&page==SESSION_CONTENT){readerOffset=nav.index;beginCompose();return;}
  if(crossing==snowball::Crossing::Top&&page!=SESSION_CONTENT)returnSession(true);
  if(page==SESSION_CONTENT){readerOffset=nav.index;followTail=false;}
  dirty=true;
}
void buttonAction(int button,snowball::Gesture event){
  using snowball::Gesture;if(event==Gesture::None)return;
  if(page==COMPOSE&&button!=1){
    if(event==Gesture::Double)input.edge(button==2);
    else if(button==0&&input.caret()==0){returnSession(false,false);return;}
    else input.move(button==0?-1:1);
    dirty=true;return;
  }
  if(button!=1){if(event==Gesture::Double){nav.edge(button==2);cursor=nav.index;if(page==SESSION_CONTENT){readerOffset=nav.index;followTail=false;}dirty=true;}
    else move(button==0?-1:1,event==Gesture::Hold||event==Gesture::Repeat);return;}
  if(event==Gesture::Click)select();
  else if(event==Gesture::Hold){
    if(page==WIFI_PASSWORD){if(wifiEditing){passwordVisible=!passwordVisible;dirty=true;}else back();}
    else if(page==COMPOSE){toggleInputLanguage();dirty=true;}
    else if(page==CONFIRM)back();
    else if(page==SESSION_CONTENT){page=ACTIONS;nav.list(5,0,4);dirty=true;}
    else back();
  } else if(event==Gesture::Double){
    if(page==SESSION_CONTENT&&nav.focus==snowball::Focus::Content){nav.edge(true);readerOffset=nav.index;followTail=true;dirty=true;}
    else if(page==WIFI_PASSWORD||page==COMPOSE||page==CONFIRM)back();else returnSession();
  }
}
void keyboard(uint8_t c){
  if(page==SESSION_CONTENT&&nav.focus==snowball::Focus::Content&&c!=27){beginCompose();if(page==COMPOSE&&c!=13&&c!=10)keyboard(c);return;}
  if(page==COMPOSE&&nav.focus==snowball::Focus::Content){
    if(!commandId.length()&&!pendingSend)editorNotice="";
    if(c==9)toggleInputLanguage();else if(c==0xb4||c==0xb5){if(input.caret()==0)returnSession(false,false);else input.move(-1);}else if(c==0xb6||c==0xb7)input.move(1);
    else if(c==1)input.edge(false);else if(c==5)input.edge(true);
    else if(c==27)back();else if(c==13||c==10)select();
    else if(c==21&&!pendingSend){input.clear();commandId="";draftSession=remoteView["sessionKey"].as<String>();}
    else if(commandId.length()||pendingSend){notice="전송 상태 확인. Ctrl+U: 초안 삭제";}
    else if(c==8||c==127)input.backspace();else if(c>=32&&c<127)input.append(c);dirty=true;return;
  }
  if(page==WIFI_PASSWORD&&nav.focus==snowball::Focus::Content){String &value=wifiEditing?wifiPassword:wifiSsid;size_t limit=wifiEditing?63:32;
    if(c==9&&wifiEditing)passwordVisible=!passwordVisible;
    else if(c==8||c==127){if(value.length())value.remove(value.length()-1);}else if(c==13||c==10){nav.index=0;select();}else if(c==27)back();else if(c>=32&&c<127&&value.length()<limit)value+=(char)c;dirty=true;return;
  }
  if(c=='w'||c=='W'||c=='a'||c=='A'||c==0xb5||c==0xb4)move(-1);
  else if(c=='s'||c=='S'||c=='d'||c=='D'||c==0xb6||c==0xb7)move(1);
  else if(c==13||c==10||c==' ')select();else if(c==27||c==8)back();
}
void screenshot(bool raw=false){
  if(dirty)draw();
  // Diagnostic capture never exports a user password, even while shown on LCD.
  bool restorePassword=passwordVisible&&page==WIFI_PASSWORD;if(restorePassword){passwordVisible=false;draw();}
  raw=raw&&canvas.getColorDepth()==8;
  StaticJsonDocument<160>d;d["type"]="screen_begin";d["width"]=320;d["height"]=240;d["format"]=raw?"rgb332":"rgb888";serialJson(d);
  static uint8_t row[960],encoded[1281];DynamicJsonDocument line(1600);
  for(int y=0;y<240;y++){
    if(raw)memcpy(row,(const uint8_t*)canvas.getBuffer()+y*320,320);else canvas.readRectRGB(0,y,320,1,row);
    size_t n=0;mbedtls_base64_encode(encoded,sizeof(encoded),&n,row,raw?320:sizeof(row));encoded[n]=0;
    line.clear();line["type"]="screen_row";line["y"]=y;line["data"]=(const char*)encoded;serialJson(line);delay(1);
  }
  StaticJsonDocument<64>end;end["type"]="screen_end";serialJson(end);
  if(restorePassword){passwordVisible=true;dirty=true;}
}
void handleSerial(const String&raw){
  // Small diagnostic commands must not reserve the full signed-view budget.
  // The 8-bit framebuffer and live network view also occupy ESP32 heap.
  size_t capacity=raw.length()*2+1024;if(capacity>24576)capacity=24576;
  DynamicJsonDocument d(capacity);if(deserializeJson(d,raw))return;String type=d["type"]|"";
  if(type=="probe"){hello();return;}
  if(type=="enroll"){
    String key=d["key"].as<String>(),epoch=d["epoch"].as<String>();if(!hex(key,64)||!hex(epoch,32))return;
    // Updating a different enrollment requires a deliberate three-button hold.
    if(pairKey.length()&&pairKey!=key){notice="등록 키가 다릅니다. A+B+C 길게 눌러 초기화";dirty=true;return;}
    if(pairKey!=key)prefs.putString("pair",key);pairKey=key;hostEpoch=epoch;hostIp=d["host"].as<String>();hostPort=d["port"]|47771;bool first=!lastUsb;lastUsb=millis();if(first&&!pending)request("poll");return;
  }
  if(type=="response"&&pending&&!pendingReverse&&d["id"].as<uint32_t>()==pendingId){lastUsb=millis();acceptView(d["view"]);return;}
  if(type=="render"){notice=d["text"].as<String>();dirty=true;return;}
  if(type=="screenshot"){screenshot(d["raw"]|false);return;}
  if(type=="wifi-scan"){
    if(page==WIFI_PASSWORD||page==CONFIRM){StaticJsonDocument<160> r;r["type"]="wifi-networks";r["ok"]=false;r["code"]="editing";r.createNestedArray("networks");serialJson(r);return;}
    scanWifi();return;
  }
  if(type=="wifi-provision"){
    if(pending||page==WIFI_PASSWORD)return;wifiSsid=d["ssid"].as<String>();wifiPassword=d["password"].as<String>();connectWifi();return;
  }
  // Explicit Wi-Fi UI QA shares the real key handler and cannot submit a
  // password or a harness prompt. Normal background traffic never uses this.
  if(type=="wifi-key-check"){
    int key=d["key"]|0;
    if(page==WIFI_LIST&&key==13&&cursor<scanCount&&!scanRunning)select();
    else if(page==WIFI_PASSWORD&&(key==9||key==8||key==27||(key>=32&&key<127)))keyboard(key);
    return;
  }
  if(type=="nav-check"){
    if(page==COMPOSE||page==CONFIRM||page==WIFI_PASSWORD)return;
    String command=d["action"]|"";
    if(command=="up")move(-1);else if(command=="down")move(1);else if(command=="pgup")move(-1,true);else if(command=="pgdn")move(1,true);
    else if(command=="home"){nav.edge(false);dirty=true;}else if(command=="end"){nav.edge(true);dirty=true;}
    else if(command=="select"&&(nav.focus==snowball::Focus::Top||page==REMOTE_LIST))select();
    else if(command=="content")returnSession();else if(command=="actions"){page=ACTIONS;nav.list(5,0,4);dirty=true;}
    cursor=nav.index;if(page==SESSION_CONTENT)readerOffset=nav.index;return;
  }
  // USB QA exercises real editor handling but cannot admit Enter/B dispatch.
  if(type=="editor-check"&&!pendingSend){
    String action=d["action"]|"";
    if(page==COMPOSE&&(action=="left"||action=="right"||action=="home"||action=="end"||action=="hold-left"||action=="hold-right")){
      int button=action=="left"||action=="home"||action=="hold-left"?0:2;
      buttonAction(button,action=="home"||action=="end"?snowball::Gesture::Double:action.startsWith("hold")?snowball::Gesture::Repeat:snowball::Gesture::Click);
    }else if(action=="key"&&(page==COMPOSE||page==SESSION_CONTENT)){
      int key=d["key"]|0;if(key==8||key==9||key==21||key==27||key==127||(key>=32&&key<127)||(key>=0xb4&&key<=0xb7))keyboard(key);
    }
    return;
  }
  if(type=="inspect"){
    DynamicJsonDocument result(remoteView.memoryUsage()+4096);
    if(!result.capacity()){StaticJsonDocument<160> error;error["type"]="inspection-error";error["code"]="allocation_failed";error["heap"]=ESP.getFreeHeap();error["maxBlock"]=ESP.getMaxAllocHeap();serialJson(error);return;}
    result["type"]="inspection";result["deviceId"]=deviceId;result["faces"]=faces;result["wifi"]=WiFi.status()==WL_CONNECTED;result["ip"]=WiFi.localIP().toString();result["paired"]=pairKey.length()==64;result["reverse"]=reverseReady&&reverseClient.connected();result["page"]=(int)page;result["korean"]=input.korean;result["draft"]=input.text().c_str();result["heap"]=ESP.getFreeHeap();result["maxBlock"]=ESP.getMaxAllocHeap();result["view"]=remoteView;
    result["scanRunning"]=scanRunning;result["scanCount"]=scanCount;result["scanTotal"]=scanTotal;result["scanCode"]=scanLastCode;result["scanAttempts"]=scanAttempts;result["scanMessage"]=scanNotice;result["scanDurationMs"]=scanDuration;
    result["wifiStatus"]=(int)WiFi.status();result["wifiConnecting"]=wifiStarted!=0;result["pending"]=pending;
    result["focus"]=nav.focus==snowball::Focus::Top?"top":"content";result["crumb"]=nav.crumb;result["cursor"]=nav.index;result["editCaret"]=(uint32_t)input.caret();result["viewportStart"]=nav.start();result["layoutOk"]=layoutOk;
    result["passwordVisible"]=page==WIFI_PASSWORD&&wifiEditing&&passwordVisible;result["passwordLength"]=page==WIFI_PASSWORD&&wifiEditing?wifiPassword.length():0;
    serialJson(result);return;
  }
  // Hardware QA only: exercise the actual IME and display, never dispatch.
  if(type=="ime-check"&&!pending&&!commandId.length()){input.clear();input.korean=d["korean"]|false;String keys=d["keys"]|"";if(keys.length()>100)return;for(char c:keys)if(c>=32&&c<127)input.append(c);draftSession=remoteView["sessionKey"].as<String>();page=COMPOSE;nav.list(1,0,4);notice="IME 진단 / USB QA";dirty=true;return;}
  if(type=="clear-check"&&!pending){input.clear();returnSession();return;}
}
String fitText(String label,int width){
  if(canvas.textWidth(label.c_str())<=width)return label;
  while(label.length()&&canvas.textWidth((label+"..").c_str())>width){size_t pos=label.length()-1;while(pos>0&&((uint8_t)label[pos]&0xc0)==0x80)pos--;label.remove(pos);}return label+"..";
}
enum class Symbol {Left,Right,Up,Down,PageUp,PageDown,Home,End,Select,Edit,Menu,Tail,Language,Back,Eye,Run};
void symbol(int x,int y,Symbol kind,uint16_t ink){
  using S=Symbol;
  if(kind==S::PageUp||kind==S::PageDown){canvas.drawRect(x+2,y,10,14,ink);symbol(x+1,y+1,kind==S::PageUp?S::Up:S::Down,ink);return;}
  if(kind==S::Home||kind==S::End){bool end=kind==S::End;canvas.drawFastVLine(x+(end?13:0),y+1,12,ink);symbol(x+1,y,end?S::Right:S::Left,ink);return;}
  if(kind==S::Tail){symbol(x,y-1,S::Down,ink);canvas.drawFastHLine(x+1,y+13,12,ink);return;}
  if(kind==S::Left||kind==S::Right||kind==S::Up||kind==S::Down){
    bool horizontal=kind==S::Left||kind==S::Right,positive=kind==S::Right||kind==S::Down;
    if(horizontal){int tip=x+(positive?12:1),base=x+(positive?8:5);canvas.drawFastHLine(x+1,y+7,12,ink);canvas.drawLine(tip,y+7,base,y+3,ink);canvas.drawLine(tip,y+7,base,y+11,ink);}
    else{int tip=y+(positive?12:1),base=y+(positive?8:5);canvas.drawFastVLine(x+7,y+1,12,ink);canvas.drawLine(x+7,tip,x+3,base,ink);canvas.drawLine(x+7,tip,x+11,base,ink);}return;
  }
  if(kind==S::Select){canvas.drawLine(x+2,y+7,x+6,y+11,ink);canvas.drawLine(x+6,y+11,x+12,y+3,ink);}
  else if(kind==S::Run)canvas.fillTriangle(x+3,y+1,x+3,y+13,x+12,y+7,ink);
  else if(kind==S::Edit){canvas.drawLine(x+2,y+11,x+10,y+3,ink);canvas.drawLine(x+4,y+13,x+12,y+5,ink);canvas.drawLine(x+10,y+3,x+12,y+5,ink);canvas.drawLine(x+2,y+11,x+2,y+13,ink);canvas.drawLine(x+2,y+13,x+4,y+13,ink);}
  else if(kind==S::Menu){for(int i=0;i<3;i++)canvas.drawFastHLine(x+2,y+3+i*4,11,ink);}
  else if(kind==S::Language){canvas.drawCircle(x+7,y+7,6,ink);canvas.drawEllipse(x+7,y+7,3,6,ink);canvas.drawFastHLine(x+1,y+7,13,ink);}
  else if(kind==S::Eye){canvas.drawEllipse(x+7,y+7,6,4,ink);canvas.fillCircle(x+7,y+7,2,ink);}
  else {canvas.drawLine(x+2,y+5,x+6,y+1,ink);canvas.drawLine(x+2,y+5,x+6,y+9,ink);canvas.drawFastHLine(x+2,y+5,9,ink);canvas.drawFastVLine(x+11,y+5,7,ink);canvas.drawFastHLine(x+7,y+11,5,ink);}
}
bool harnessGlyph(int x,int y,JsonVariantConst icon,uint16_t ink){
  JsonArrayConst rows=icon["rows"].as<JsonArrayConst>();if((icon["size"]|0)!=16||rows.size()!=16)return false;
  for(auto row:rows)if(!row.is<int>()||row.as<int>()<0||row.as<int>()>65535)return false;
  const char* packed=icon["rgb565"]|"";static uint8_t pixels[512];size_t bytes=0;
  bool color=strlen(packed)==684&&mbedtls_base64_decode(pixels,sizeof(pixels),&bytes,(const uint8_t*)packed,684)==0&&bytes==sizeof(pixels);
  for(int iy=0;iy<16;iy++){uint16_t bits=rows[iy].as<uint16_t>();for(int ix=0;ix<16;ix++)if(bits&(0x8000>>ix)){
    int offset=2*(iy*16+ix);canvas.drawPixel(x+ix,y+iy,color?(uint16_t)((pixels[offset]<<8)|pixels[offset+1]):ink);
  }}return true;
}
void footer(){
  const uint16_t card=contrast?TFT_BLACK:0x18e7,accent=contrast?TFT_YELLOW:0x07bf;
  using S=Symbol;bool sideways=nav.focus==snowball::Focus::Top||page==COMPOSE;
  S click=nav.focus==snowball::Focus::Top?S::Select:page==SESSION_CONTENT?S::Edit:page==COMPOSE?S::Run:S::Select;
  S hold=page==COMPOSE?S::Language:page==SESSION_CONTENT?S::Menu:page==WIFI_PASSWORD&&wifiEditing?S::Eye:S::Back;
  S twice=page==SESSION_CONTENT&&nav.focus==snowball::Focus::Content?S::Tail:S::Back;
  for(int b=0;b<3;b++){
    int x=2+b*snowball::BoxStride,y=snowball::BottomY;
    canvas.fillRoundRect(x,y,104,27,5,card);canvas.drawRoundRect(x,y,104,27,5,accent);
    S actions[3]={b==0?(sideways?S::Left:S::Up):b==2?(sideways?S::Right:S::Down):click,
      b==0?(page==COMPOSE?S::Left:S::PageUp):b==2?(page==COMPOSE?S::Right:S::PageDown):hold,
      b==0?S::Home:b==2?S::End:twice};
    for(int slot=0;slot<3;slot++){
      int gx=x+4+slot*34,cy=y+13;
      if(slot==0)canvas.drawCircle(gx+3,cy,2,accent);
      else if(slot==1){canvas.fillRoundRect(gx,cy-3,7,5,2,accent);canvas.drawFastHLine(gx,cy+4,7,accent);}
      else {canvas.drawCircle(gx+1,cy,1,accent);canvas.drawCircle(gx+5,cy,1,accent);}
      symbol(gx+10,y+6,actions[slot],TFT_WHITE);
      if(slot<2)canvas.drawFastVLine(gx+29,y+6,15,contrast?accent:0x31aa);
      layoutOk=layoutOk&&gx+24<x+104&&y+21<=240;
    }
  }
}
void draw(){
  syncNavigation();layoutOk=true;
  const uint16_t bg=contrast?TFT_BLACK:0x0843,card=contrast?TFT_BLACK:0x18e7,accent=contrast?TFT_YELLOW:0x07bf;
  canvas.setClipRect(0,0,320,240);canvas.fillScreen(bg);canvas.setTextWrap(false);canvas.setFont(&fonts::efontKR_14);
  canvas.setClipRect(0,0,320,30);
  canvas.fillRect(0,0,320,29,card);canvas.drawFastHLine(0,29,320,accent);
  bool top=nav.focus==snowball::Focus::Top;
  if(top&&nav.crumb==0)canvas.fillRoundRect(1,2,26,25,3,accent);
  for(int y=0;y<20;y++)for(int x=0;x<20;x++)canvas.drawPixel(4+x,4+y,g_snowball_icon_128[(y*128/20)*128+x*128/20]);
  int x=30;bool firstCrumb=true;
  for(int crumb=1;crumb<=4;crumb++){
    if(crumb==1&&!(top&&nav.crumb<=1))continue;
    canvas.setTextColor(accent,card);canvas.setCursor(x,7);canvas.print(firstCrumb?"|":">");firstCrumb=false;x+=12;
    bool focused=top&&nav.crumb==crumb;
    if(crumb==2&&!focused){
      if(!harnessGlyph(x,6,remoteView["harnessIcon"],TFT_WHITE)){canvas.drawRoundRect(x,5,17,18,2,TFT_WHITE);symbol(x+1,7,Symbol::Run,TFT_WHITE);}
      x+=21;continue;
    }
    String label=crumb==1?String(remoteView["machine"]|"Machine"):crumb==2?String(remoteView["harnessName"]|"Harness"):crumb==3?String(remoteView["project"]|""):String(remoteView["session"]|"...");
    int budget=crumb==1?84:crumb==2?110:crumb==3?(top&&nav.crumb<=1?66:focused?112:96):315-x;
    label=fitText(label,budget-4);
    int width=min(budget,canvas.textWidth(label.c_str())+4);
    canvas.fillRoundRect(x-2,3,width+3,23,3,focused?accent:card);canvas.setTextColor(focused?TFT_BLACK:TFT_WHITE,focused?accent:card);canvas.setCursor(x,7);canvas.print(label);x+=width+3;
    layoutOk=layoutOk&&x<=320;
  }
  canvas.setClipRect(0,30,320,snowball::BottomY-30);
  canvas.setTextColor(TFT_WHITE,bg);canvas.setFont(&fonts::efontKR_14);
  if(page==SESSION_CONTENT){
    if(nav.focus==snowball::Focus::Content)canvas.drawFastVLine(2,35,153,accent);
    JsonArrayConst lines=remoteView["contentLines"].as<JsonArrayConst>();int offset=remoteView["contentOffset"]|0;
    if(lines.size()==0){canvas.setCursor(10,43);canvas.print(remoteView["connected"].as<bool>()?"네이티브 세션 내용 없음":"미들웨어 연결 중...");}
    else for(int row=0;row<11&&row<(int)lines.size();row++){String label=lines[row].as<String>();layoutOk=layoutOk&&canvas.textWidth(label.c_str())<=300;canvas.setCursor(10,35+row*14);canvas.print(fitText(label,300));}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,194);String status=String(remoteView["readOnly"]|true)?"읽기 전용":"제어 가능";status+="  "+String(nav.index+1)+"/"+String(remoteView["contentTotal"]|0)+"  "+String(remoteView["model"]|"");
    if(!remoteView["connected"].as<bool>()||String(remoteView["commandStatus"]|"idle")!="idle")status=remoteView["message"]|"미들웨어 미연결 / Offline";
    canvas.print(fitText(status,304));
    if(offset!=nav.index){canvas.fillCircle(314,197,2,TFT_ORANGE);}
  } else if(page==REMOTE_LIST||page==SETTINGS||page==ACTIONS||page==WIFI_LIST){
    int start=nav.start(),total=nav.total,offset=remoteView["menuOffset"]|0;
    for(int row=0;row<7&&start+row<total;row++){
      int index=start+row,y=34+row*22;bool selected=nav.focus==snowball::Focus::Content&&index==nav.index;uint16_t color=selected?accent:card;
      canvas.fillRoundRect(7,y,306,21,3,color);canvas.setTextColor(selected?TFT_BLACK:TFT_WHITE,color);canvas.setCursor(12,y+3);String label;
      if(page==SETTINGS)label=settingsItems[index];else if(page==ACTIONS)label=actionItems[index];
      else if(page==REMOTE_LIST){int local=index-offset;if(!listInitial&&String(remoteView["menuKind"]|"")==expectedMenu&&local>=0&&local<(int)remoteView["items"].size())label=remoteView["items"][local].as<String>();else label="조회 중...";}
      else if(index<scanCount)label=scanNetworks[index].ssid+" "+String(scanNetworks[index].rssi)+"dBm";else label=index==scanCount?"다시 검색 / Scan":index==scanCount+1?"숨김 네트워크 / Hidden":"저장 Wi-Fi 삭제 / Forget";
      bool glyph=false;if(page==REMOTE_LIST&&expectedMenu=="harnesses"){int local=index-offset;if(local>=0)glyph=harnessGlyph(12,y+2,remoteView["itemIcons"][local],selected?TFT_BLACK:TFT_WHITE);}
      if(glyph)canvas.setCursor(36,y+3);canvas.print(fitText(label,glyph?271:295));
    }
    if(total==0){canvas.setTextColor(TFT_WHITE,bg);canvas.setCursor(10,45);canvas.print(pending||listInitial?"실제 목록 조회 중...":"사용 가능한 항목 없음");}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,193);canvas.print(fitText(page==WIFI_LIST?scanNotice:notice,302));
  } else if(page==COMPOSE){
    canvas.setTextColor(accent,bg);canvas.setCursor(10,35);canvas.print(input.korean?"Prompt Edit  ·  한글":"Prompt Edit  ·  EN");
    canvas.drawRoundRect(6,53,308,137,4,accent);canvas.setTextColor(TFT_WHITE,bg);
    const std::string text=input.text();const size_t caret=input.caret();
    auto layout=snowball::editorLayout(text,caret,294,[](const std::string& glyph){return canvas.textWidth(glyph.c_str());});
    int first=std::max(0,(int)layout.cursorRow-8);
    for(int row=first;row<(int)layout.rows.size()&&row<first+9;row++){
      const auto span=layout.rows[row];auto line=text.substr(span.begin,span.end-span.begin);int y=59+(row-first)*14;
      canvas.setCursor(12,y);canvas.print(line.c_str());
      if(row==(int)layout.cursorRow){int caretX=12+canvas.textWidth(text.substr(span.begin,caret-span.begin).c_str());canvas.drawFastVLine(caretX,y,14,accent);layoutOk=layoutOk&&caretX<=306;}
    }
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,196);
    canvas.print(fitText(commandId.length()||pendingSend?notice:editorNotice.length()?editorNotice:String("Tab 한/영 · Esc 내용 · Ctrl+U 삭제"),304));
  } else if(page==WIFI_PASSWORD){
    canvas.setCursor(10,35);canvas.print(fitText(wifiSsid,300));canvas.setCursor(10,55);canvas.print(wifiEditing?"암호 / Password":"SSID 입력 / Network name");canvas.setCursor(10,77);canvas.setTextWrap(true);if(wifiEditing){if(passwordVisible)canvas.print(wifiPassword);else for(size_t i=0;i<wifiPassword.length();i++)canvas.print('*');}canvas.setTextWrap(false);
    for(int i=0;i<(wifiEditing?3:2);i++){int y=126+i*21;bool active=nav.focus==snowball::Focus::Content&&nav.index==i;canvas.fillRoundRect(7,y,306,20,3,active?accent:card);canvas.setTextColor(active?TFT_BLACK:TFT_WHITE,active?accent:card);canvas.setCursor(12,y+3);canvas.print(!wifiEditing?(i==0?"암호 입력으로 / Next":"취소 / Back"):i==0?"연결 / Connect":i==1?(passwordVisible?"암호 숨기기 / Hide":"암호 보이기 / Show"):"취소 / Back");}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(10,193);canvas.print("Tab: 표시/숨김   Backspace: 삭제");
  } else if(page==INFO){canvas.setCursor(10,36);canvas.printf("%s\nESP32 / %u MB\nFACES: %s\nWi-Fi: %s\nIP: %s\n기기 등록: %s\n영문 / 한글 두벌식",deviceId.c_str(),ESP.getFlashChipSize()/1048576,faces?"FOUND":"ABSENT",WiFi.status()==WL_CONNECTED?"CONNECTED":"OFFLINE",WiFi.localIP().toString().c_str(),pairKey.length()==64?"등록됨":"필요");}
  canvas.setClipRect(0,snowball::BottomY,320,snowball::BottomHeight);footer();
  canvas.setClipRect(0,0,320,240);canvas.pushSprite(0,0);dirty=false;
}
void setup(){
  auto config=M5.config();config.internal_mic=false;config.internal_spk=true;M5.begin(config);Serial.begin(115200);M5.Display.setRotation(1);M5.Display.setBrightness(160);M5.Speaker.setVolume(64);
  canvas.setColorDepth(8);if(!canvas.createSprite(320,240)){M5.Display.print("Display allocation failed");while(true)delay(1000);}
  Wire.begin(21,22,100000);Wire.beginTransmission(0x08);faces=Wire.endTransmission()==0;pinMode(5,INPUT_PULLUP);
  uint64_t mac=ESP.getEfuseMac();char id[20];snprintf(id,sizeof(id),"m5-%02x%02x%02x%02x%02x%02x",(uint8_t)mac,(uint8_t)(mac>>8),(uint8_t)(mac>>16),(uint8_t)(mac>>24),(uint8_t)(mac>>32),(uint8_t)(mac>>40));deviceId=id;bootId=randomHex(8);
  String identity=String("snowball.controller.v1\nsnowball.device-m5stack\nphysical\n")+deviceId;uint8_t digest[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const unsigned char*)identity.c_str(),identity.length(),digest);controllerId="ctl_";
  for(int i=0;i<8;i++){char h[3];snprintf(h,3,"%02x",digest[i]);controllerId+=h;}
  prefs.begin("snowball",false);pairKey=prefs.getString("pair","");wifiSsid=prefs.getString("ssid","");wifiPassword=prefs.getString("password","");WiFi.persistent(false);
  input.korean=prefs.getBool("korean",false);
  WiFi.onEvent([](WiFiEvent_t type,WiFiEventInfo_t event){
    if(type==ARDUINO_EVENT_WIFI_SCAN_DONE)scanEventStatus.store(event.wifi_scan_done.status);
    else if(type==ARDUINO_EVENT_WIFI_STA_GOT_IP)wifiAssociationReady.store(true);
    else if(type==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)wifiAssociationReady.store(false);
  });
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
  if(wifiSsid.length()){WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str());wifiStarted=millis();}
  discovery.begin(47773);reverseServer.begin();M5.BtnA.setHoldThresh(650);M5.BtnB.setHoldThresh(650);M5.BtnC.setHoldThresh(650);draw();hello();
}
void loop(){
  M5.update();
  if(M5.BtnA.pressedFor(2500)&&M5.BtnB.pressedFor(2500)&&M5.BtnC.pressedFor(2500)){prefs.remove("pair");pairKey="";hostEpoch="";notice="기기 등록 초기화. USB로 재등록";dirty=true;delay(500);}
  uint32_t now=millis();bool pressed[]={M5.BtnA.isPressed(),M5.BtnB.isPressed(),M5.BtnC.isPressed()};
  for(int b=0;b<3;b++)buttonAction(b,gestures[b].update(now,pressed[b]));
  if(faces&&digitalRead(5)==LOW){Wire.requestFrom(0x08,1);if(Wire.available()){uint8_t c=Wire.read();if(c&&c!=0xff)keyboard(c);}}
  while(Serial.available()){char c=Serial.read();if(c=='\n'){handleSerial(serialLine);serialLine="";}else if(c!='\r'){if(serialLine.length()<24576)serialLine+=c;else serialLine="";}}
  updateScan();
  if(wifiStarted){if(wifiAssociationReady.load()&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiSsid){prefs.putString("ssid",wifiSsid);prefs.putString("password",wifiPassword);wifiStarted=0;notice="Wi-Fi 연결됨: "+WiFi.localIP().toString();dirty=true;discoverHost();}else if(millis()-wifiStarted>20000){wifiStarted=0;notice="Wi-Fi 연결 실패. 설정에서 재시도";dirty=true;}}
  readDiscovery();
  readReverse();
  if(!lastUsb&&millis()-lastHello>3000)hello();
  if(pending&&millis()-lastPoll>7000){pending=false;pendingSend=false;notice="응답 불명확. 상태 조회로 확인";dirty=true;}
  if(!pending&&!scanRunning&&millis()-lastPoll>3500&&(usbOnline()||(!hostEpoch.isEmpty()&&WiFi.status()==WL_CONNECTED))&&page!=WIFI_LIST&&page!=WIFI_PASSWORD&&page!=CONFIRM)request("poll");
  if(!usbOnline()&&!wifiUi()&&WiFi.status()==WL_CONNECTED&&hostEpoch.isEmpty()&&millis()-lastDiscovery>8000)discoverHost();
  if(!pending&&!wifiUi()&&millis()-nextContentFetch>120){
    if(page==SESSION_CONTENT&&nav.index!=(remoteView["contentOffset"]|0)){nextContentFetch=millis();request("read",nav.index);}
    else if(page==REMOTE_LIST&&!listInitial&&String(remoteView["menuKind"]|"")==expectedMenu){int offset=remoteView["menuOffset"]|0;if(nav.start()!=offset){nextContentFetch=millis();request("browse",nav.index);}}
  }
  if(dirty)draw();delay(5);
}
