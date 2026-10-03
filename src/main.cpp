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
static String deviceId,pairKey,hostIp,hostEpoch,bootId,discoverNonce,serialLine,wifiSsid,wifiPassword,notice="USB 연결 또는 Wi-Fi 설정";
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
static String commandId,draftSession,pendingOp,expectedMenu;
static snowball::Navigation nav;
static snowball::ButtonGesture gestures[3];
static bool listInitial=false,followTail=false,layoutOk=true;
static int settingsIndex=0,wifiIndex=0;
static uint32_t nextContentFetch=0;

enum Page { SESSION_CONTENT=0, HOME=0, READ_REPLY=0, REMOTE_LIST=1, COMPOSE=2, CONFIRM=3, WIFI_LIST=4, WIFI_PASSWORD=5, INFO=7, SETTINGS=8, ACTIONS=9 };
static Page page=SESSION_CONTENT;
static const char* settingsItems[]={"Wi-Fi 설정","미들웨어 검색","화면 테마","기기 정보"};
static const char* actionItems[]={"키보드 입력 / Compose","모델 / Model","Effort","새로고침 / Refresh","내용으로 / Back"};
void back();void draw();void select();void returnSession(bool top=false);void syncNavigation();
bool wifiUi(){return page==WIFI_LIST||page==WIFI_PASSWORD||scanRunning;}

String randomHex(size_t bytes){String s;for(size_t i=0;i<bytes;i++){uint8_t b=esp_random()&255;char h[3];snprintf(h,3,"%02x",b);s+=h;}return s;}
String hmac(const String &value){uint8_t result[32];mbedtls_md_context_t ctx;mbedtls_md_init(&ctx);mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);mbedtls_md_hmac_starts(&ctx,(const unsigned char*)pairKey.c_str(),pairKey.length());mbedtls_md_hmac_update(&ctx,(const unsigned char*)value.c_str(),value.length());mbedtls_md_hmac_finish(&ctx,result);mbedtls_md_free(&ctx);String s;for(int i=0;i<32;i++){char h[3];snprintf(h,3,"%02x",result[i]);s+=h;}return s;}
bool safeEqual(const String&a,const String&b){if(a.length()!=b.length())return false;uint8_t difference=0;for(size_t i=0;i<a.length();i++)difference|=a[i]^b[i];return difference==0;}
bool hex(const String&s,size_t length){if(s.length()!=length)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
String material(const char* direction,uint32_t seq,const String&payload){return String(direction)+"\n"+hostEpoch+"\n"+bootId+"\n"+String(seq)+"\n"+payload;}
void serialJson(JsonDocument&doc){serializeJson(doc,Serial);Serial.println();}
void hello(){StaticJsonDocument<384>d;d["type"]="hello";d["deviceId"]=deviceId;d["board"]="M5Stack";d["faces"]=faces;d["flashBytes"]=ESP.getFlashChipSize();d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");d["firmware"]="0.2.0";serialJson(d);lastHello=millis();}
bool usbOnline(){return lastUsb&&millis()-lastUsb<6500;}
bool middlewareOnline(){return lastResponse&&millis()-lastResponse<10000&&remoteView["connected"].as<bool>();}
void acceptView(JsonVariantConst view){
  String previous=remoteView["sessionKey"]|"",acceptedOp=pendingOp;pendingOp="";
  remoteView.clear();remoteView.set(view);lastResponse=millis();pending=false;
  notice=String(remoteView["message"]|"");contrast=String(remoteView["skinId"]|"")=="high-contrast";
  if(pendingSend){pendingSend=false;if(remoteView["connected"].as<bool>()&&snowball::journalAdmitted(remoteView["commandStatus"]|"unconfirmed")){input.clear();draftSession="";}returnSession();}
  if(page==REMOTE_LIST){
    String kind=remoteView["menuKind"]|"";
    if(acceptedOp=="select"&&!kind.isEmpty()){expectedMenu=kind;listInitial=true;nav.returnCrumb=kind=="sessions"?3:kind=="harnesses"?2:1;}
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
  if(page==SESSION_CONTENT){nav.returnCrumb=3;nav.configure(remoteView["contentTotal"]|0,11,true);}
  else if(page==REMOTE_LIST)nav.configure(listInitial?0:(remoteView["menuTotal"]|0),7,false);
  else if(page==SETTINGS)nav.configure(4,7,false);
  else if(page==ACTIONS)nav.configure(5,7,false);
  else if(page==WIFI_LIST)nav.configure(scanCount+3,7,false);
  else if(page==COMPOSE)nav.configure(4,4,false);
  else if(page==WIFI_PASSWORD)nav.configure(wifiEditing?3:2,3,false);
  else if(page==CONFIRM)nav.configure(2,2,false);
  else nav.configure(8,11,true);
  cursor=nav.index;
}
void returnSession(bool top){
  if(scanRunning)cancelScan();resumeWifi();page=SESSION_CONTENT;
  nav.index=readerOffset;nav.focus=top?snowball::Focus::Top:snowball::Focus::Content;syncNavigation();
  expectedMenu="";listInitial=false;request("read",nav.index);dirty=true;
}
void openRemote(const char* kind,int origin){
  if(pending){notice="응답 대기 중";dirty=true;return;}
  page=REMOTE_LIST;expectedMenu=kind;listInitial=true;nav.list(0,0,origin);request(kind);dirty=true;
}
void beginCompose(){
  if(String(remoteView["sessionKey"]|"").isEmpty()){notice="상단에서 세션을 선택하세요";dirty=true;return;}
  if(input.text().empty())draftSession=remoteView["sessionKey"].as<String>();
  page=COMPOSE;nav.list(4,0,3);dirty=true;
}
void back(){
  if(page==WIFI_PASSWORD){page=WIFI_LIST;passwordVisible=false;nav.list(scanCount+3,wifiIndex,0);dirty=true;return;}
  if(page==WIFI_LIST||page==INFO){if(scanRunning)cancelScan();resumeWifi();page=SETTINGS;nav.list(4,settingsIndex,0);dirty=true;return;}
  if(page==CONFIRM){page=COMPOSE;nav.list(4,0,3);dirty=true;return;}
  returnSession();
}
void select(){
  cursor=nav.index;
  if(nav.focus==snowball::Focus::Top){
    if(nav.crumb==0){page=SETTINGS;nav.list(4,settingsIndex,0);}
    else openRemote(nav.crumb==1?"machines":nav.crumb==2?"harnesses":"sessions",nav.crumb);
  } else if(page==REMOTE_LIST){if(!listInitial&&!pending)request("select",cursor);}
  else if(page==SESSION_CONTENT)beginCompose();
  else if(page==SETTINGS){
    settingsIndex=cursor;
    if(cursor==0)scanWifi();else if(cursor==1)discoverHost();else if(cursor==2){contrast=!contrast;request("skin");}else {page=INFO;nav.list(8,0,0);nav.reader=true;}
  } else if(page==ACTIONS){
    if(cursor==0)beginCompose();else if(cursor==1)openRemote("models",3);else if(cursor==2)openRemote("efforts",3);else returnSession();
  } else if(page==COMPOSE){
    if(cursor==0){if(input.text().empty())notice="입력 내용이 없습니다";else if(draftSession!=remoteView["sessionKey"].as<String>())notice="세션 변경: 기존 입력을 먼저 지우세요";else {page=CONFIRM;commandId=randomHex(16);nav.list(2,0,3);}}
    else if(cursor==1){input.toggle();notice=input.korean?"한글 두벌식":"English";}
    else if(cursor==2){input.clear();draftSession=remoteView["sessionKey"].as<String>();}
    else returnSession();
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
  if(crossing==snowball::Crossing::Top&&page!=SESSION_CONTENT)returnSession(true);
  if(page==SESSION_CONTENT){readerOffset=nav.index;followTail=false;}
  dirty=true;
}
void buttonAction(int button,snowball::Gesture event){
  using snowball::Gesture;if(event==Gesture::None)return;
  if(button!=1){if(event==Gesture::Double){nav.edge(button==2);cursor=nav.index;if(page==SESSION_CONTENT){readerOffset=nav.index;followTail=false;}dirty=true;}
    else move(button==0?-1:1,event==Gesture::Hold||event==Gesture::Repeat);return;}
  if(event==Gesture::Click)select();
  else if(event==Gesture::Hold){
    if(page==WIFI_PASSWORD){if(wifiEditing){passwordVisible=!passwordVisible;dirty=true;}else back();}
    else if(page==COMPOSE){input.toggle();dirty=true;}
    else if(page==CONFIRM)back();
    else if(page==SESSION_CONTENT){page=ACTIONS;nav.list(5,0,3);dirty=true;}
    else back();
  } else if(event==Gesture::Double){
    if(page==SESSION_CONTENT&&nav.focus==snowball::Focus::Content){nav.edge(true);readerOffset=nav.index;followTail=true;dirty=true;}
    else if(page==WIFI_PASSWORD||page==COMPOSE||page==CONFIRM)back();else returnSession();
  }
}
void keyboard(uint8_t c){
  if(page==COMPOSE&&nav.focus==snowball::Focus::Content){
    if(c==9)input.toggle();else if(c==8||c==127)input.backspace();
    else if(c==13||c==10){nav.index=0;select();}else if(c==27)back();else if(c>=32&&c<127)input.append(c);dirty=true;return;
  }
  if(page==WIFI_PASSWORD&&nav.focus==snowball::Focus::Content){String &value=wifiEditing?wifiPassword:wifiSsid;size_t limit=wifiEditing?63:32;
    if(c==9&&wifiEditing)passwordVisible=!passwordVisible;
    else if(c==8||c==127){if(value.length())value.remove(value.length()-1);}else if(c==13||c==10){nav.index=0;select();}else if(c==27)back();else if(c>=32&&c<127&&value.length()<limit)value+=(char)c;dirty=true;return;
  }
  if(c=='w'||c=='W'||c=='a'||c=='A'||c==0xb5||c==0xb4)move(-1);
  else if(c=='s'||c=='S'||c=='d'||c=='D'||c==0xb6||c==0xb7)move(1);
  else if(c==13||c==10||c==' ')select();else if(c==27||c==8)back();
}
void screenshot(){
  if(dirty)draw();
  // Diagnostic capture never exports a user password, even while shown on LCD.
  bool restorePassword=passwordVisible&&page==WIFI_PASSWORD;if(restorePassword){passwordVisible=false;draw();}
  StaticJsonDocument<160>d;d["type"]="screen_begin";d["width"]=320;d["height"]=240;d["format"]="rgb888";serialJson(d);
  static uint8_t row[960],encoded[1281];DynamicJsonDocument line(1600);
  for(int y=0;y<240;y++){canvas.readRectRGB(0,y,320,1,row);size_t n=0;mbedtls_base64_encode(encoded,sizeof(encoded),&n,row,sizeof(row));encoded[n]=0;line.clear();line["type"]="screen_row";line["y"]=y;line["data"]=(const char*)encoded;serialJson(line);delay(1);}
  StaticJsonDocument<64>end;end["type"]="screen_end";serialJson(end);
  if(restorePassword){passwordVisible=true;dirty=true;}
}
void handleSerial(const String&raw){DynamicJsonDocument d(24576);if(deserializeJson(d,raw))return;String type=d["type"]|"";
  if(type=="probe"){hello();return;}
  if(type=="enroll"){
    String key=d["key"].as<String>(),epoch=d["epoch"].as<String>();if(!hex(key,64)||!hex(epoch,32))return;
    // Updating a different enrollment requires a deliberate three-button hold.
    if(pairKey.length()&&pairKey!=key){notice="등록 키가 다릅니다. A+B+C 길게 눌러 초기화";dirty=true;return;}
    if(pairKey!=key)prefs.putString("pair",key);pairKey=key;hostEpoch=epoch;hostIp=d["host"].as<String>();hostPort=d["port"]|47771;bool first=!lastUsb;lastUsb=millis();if(first&&!pending)request("poll");return;
  }
  if(type=="response"&&pending&&!pendingReverse&&d["id"].as<uint32_t>()==pendingId){lastUsb=millis();acceptView(d["view"]);return;}
  if(type=="render"){notice=d["text"].as<String>();dirty=true;return;}
  if(type=="screenshot"){screenshot();return;}
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
    else if(command=="content")returnSession();else if(command=="actions"){page=ACTIONS;nav.list(5,0,3);dirty=true;}
    cursor=nav.index;if(page==SESSION_CONTENT)readerOffset=nav.index;return;
  }
  if(type=="inspect"){
    DynamicJsonDocument result(16384);result["type"]="inspection";result["deviceId"]=deviceId;result["faces"]=faces;result["wifi"]=WiFi.status()==WL_CONNECTED;result["ip"]=WiFi.localIP().toString();result["paired"]=pairKey.length()==64;result["reverse"]=reverseReady&&reverseClient.connected();result["page"]=(int)page;result["korean"]=input.korean;result["draft"]=input.text().c_str();result["heap"]=ESP.getFreeHeap();result["view"]=remoteView;
    result["scanRunning"]=scanRunning;result["scanCount"]=scanCount;result["scanTotal"]=scanTotal;result["scanCode"]=scanLastCode;result["scanAttempts"]=scanAttempts;result["scanMessage"]=scanNotice;result["scanDurationMs"]=scanDuration;
    result["wifiStatus"]=(int)WiFi.status();result["wifiConnecting"]=wifiStarted!=0;result["pending"]=pending;
    result["focus"]=nav.focus==snowball::Focus::Top?"top":"content";result["crumb"]=nav.crumb;result["cursor"]=nav.index;result["viewportStart"]=nav.start();result["layoutOk"]=layoutOk;
    result["passwordVisible"]=page==WIFI_PASSWORD&&wifiEditing&&passwordVisible;result["passwordLength"]=page==WIFI_PASSWORD&&wifiEditing?wifiPassword.length():0;
    serialJson(result);return;
  }
  // Hardware QA only: exercise the actual IME and display, never dispatch.
  if(type=="ime-check"&&!pending){input.clear();input.korean=d["korean"]|false;String keys=d["keys"]|"";if(keys.length()>100)return;for(char c:keys)if(c>=32&&c<127)input.append(c);page=COMPOSE;nav.list(4,0,3);notice="IME 진단 / USB QA";dirty=true;return;}
  if(type=="clear-check"&&!pending){input.clear();returnSession();return;}
}
String fitText(String label,int width){
  if(canvas.textWidth(label.c_str())<=width)return label;
  while(label.length()&&canvas.textWidth((label+"..").c_str())>width){size_t pos=label.length()-1;while(pos>0&&((uint8_t)label[pos]&0xc0)==0x80)pos--;label.remove(pos);}return label+"..";
}
void footer(){
  const uint16_t card=contrast?TFT_BLACK:0x18e7,accent=contrast?TFT_YELLOW:0x07bf;
  String click="선택",hold="뒤로",twice="내용";
  if(nav.focus==snowball::Focus::Top){hold="메뉴";}
  else if(page==SESSION_CONTENT){click="입력";hold="메뉴";twice="최신";}
  else if(page==COMPOSE){hold="한영";twice="뒤로";}
  else if(page==WIFI_PASSWORD){hold=wifiEditing?"표시":"뒤로";twice="취소";}
  else if(page==CONFIRM){hold="취소";twice="취소";}
  canvas.setFont(&fonts::efontKR_10);canvas.setTextWrap(false);
  for(int b=0;b<3;b++){
    int x=2+b*snowball::BoxStride;canvas.fillRoundRect(x,207,104,33,3,card);canvas.drawRoundRect(x,207,104,33,3,accent);
    bool top=nav.focus==snowball::Focus::Top;
    String line=b==0?(top?"A ←|-PgUp":"A ↑|-PgUp"):b==2?(top?"C →|-PgDn":"C ↓|-PgDn"):"B "+click+"|-"+hold;
    String second=b==0?"..Home":b==2?"..End":".."+twice;
    layoutOk=layoutOk&&canvas.textWidth(line.c_str())<=94&&canvas.textWidth(second.c_str())<=94;
    canvas.setTextColor(TFT_WHITE,card);canvas.setCursor(x+5,210);canvas.print(line);canvas.setTextColor(accent,card);canvas.setCursor(x+5,224);canvas.print(second);
  }
}
void draw(){
  syncNavigation();layoutOk=true;
  const uint16_t bg=contrast?TFT_BLACK:0x0843,card=contrast?TFT_BLACK:0x18e7,accent=contrast?TFT_YELLOW:0x07bf;
  canvas.fillScreen(bg);canvas.setTextWrap(false);canvas.setFont(&fonts::efontKR_14);
  canvas.fillRect(0,0,320,29,card);canvas.drawFastHLine(0,29,320,accent);
  bool top=nav.focus==snowball::Focus::Top;
  if(top&&nav.crumb==0)canvas.fillRoundRect(1,2,26,25,3,accent);
  for(int y=0;y<20;y++)for(int x=0;x<20;x++)canvas.drawPixel(4+x,4+y,g_snowball_icon_128[(y*128/20)*128+x*128/20]);
  int x=30;bool firstCrumb=true;
  for(int crumb=1;crumb<=3;crumb++){
    if(crumb==1&&!(top&&nav.crumb<=1))continue;
    canvas.setTextColor(accent,card);canvas.setCursor(x,7);canvas.print(firstCrumb?"|":">");firstCrumb=false;x+=12;
    String label=crumb==1?String(remoteView["machine"]|"Machine"):crumb==2?String(remoteView["harness"]|"---"):String(remoteView["session"]|"세션 선택");
    int budget=crumb==1?84:crumb==2?42:318-x;label=fitText(label,budget-4);int width=min(budget,canvas.textWidth(label.c_str())+4);
    bool focused=top&&nav.crumb==crumb;canvas.fillRoundRect(x-2,3,width+3,23,3,focused?accent:card);canvas.setTextColor(focused?TFT_BLACK:TFT_WHITE,focused?accent:card);canvas.setCursor(x,7);canvas.print(label);x+=width+3;
    layoutOk=layoutOk&&x<=320;
  }
  canvas.setTextColor(TFT_WHITE,bg);canvas.setFont(&fonts::efontKR_14);
  if(page==SESSION_CONTENT){
    if(nav.focus==snowball::Focus::Content)canvas.drawFastVLine(2,35,153,accent);
    JsonArrayConst lines=remoteView["contentLines"].as<JsonArrayConst>();int offset=remoteView["contentOffset"]|0;
    if(lines.size()==0){canvas.setCursor(10,43);canvas.print(String(remoteView["sessionKey"]|"").isEmpty()?"상단에서 세션을 선택하세요":"네이티브 세션 내용 없음");}
    else for(int row=0;row<11&&row<(int)lines.size();row++){String label=lines[row].as<String>();layoutOk=layoutOk&&canvas.textWidth(label.c_str())<=300;canvas.setCursor(10,35+row*14);canvas.print(fitText(label,300));}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,194);String status=String(remoteView["readOnly"]|true)?"읽기 전용":"제어 가능";status+="  "+String(nav.index+1)+"/"+String(remoteView["contentTotal"]|0)+"  "+String(remoteView["model"]|"");canvas.print(fitText(status,304));
    if(offset!=nav.index){canvas.fillCircle(314,197,2,TFT_ORANGE);}
  } else if(page==REMOTE_LIST||page==SETTINGS||page==ACTIONS||page==WIFI_LIST){
    int start=nav.start(),total=nav.total,offset=remoteView["menuOffset"]|0;
    for(int row=0;row<7&&start+row<total;row++){
      int index=start+row,y=34+row*22;bool selected=nav.focus==snowball::Focus::Content&&index==nav.index;uint16_t color=selected?accent:card;
      canvas.fillRoundRect(7,y,306,21,3,color);canvas.setTextColor(selected?TFT_BLACK:TFT_WHITE,color);canvas.setCursor(12,y+3);String label;
      if(page==SETTINGS)label=settingsItems[index];else if(page==ACTIONS)label=actionItems[index];
      else if(page==REMOTE_LIST){int local=index-offset;if(!listInitial&&String(remoteView["menuKind"]|"")==expectedMenu&&local>=0&&local<(int)remoteView["items"].size())label=remoteView["items"][local].as<String>();else label="조회 중...";}
      else if(index<scanCount)label=scanNetworks[index].ssid+" "+String(scanNetworks[index].rssi)+"dBm";else label=index==scanCount?"다시 검색 / Scan":index==scanCount+1?"숨김 네트워크 / Hidden":"저장 Wi-Fi 삭제 / Forget";
      canvas.print(fitText(label,295));
    }
    if(total==0){canvas.setTextColor(TFT_WHITE,bg);canvas.setCursor(10,45);canvas.print(pending||listInitial?"실제 목록 조회 중...":"사용 가능한 항목 없음");}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,193);canvas.print(fitText(page==WIFI_LIST?scanNotice:notice,302));
  } else if(page==COMPOSE||page==CONFIRM){
    canvas.setTextColor(accent,bg);canvas.setCursor(10,35);canvas.print(page==COMPOSE?(input.korean?"입력 [한글]":"입력 [EN]"):"이 세션으로 전송할까요?");
    canvas.setTextColor(TFT_WHITE,bg);canvas.setTextWrap(true);canvas.setCursor(10,55);String text=input.text().c_str();while(canvas.textWidth(text.c_str())>1100&&text.length()){size_t i=1;while(i<text.length()&&((uint8_t)text[i]&0xc0)==0x80)i++;text.remove(0,i);}canvas.print(text);canvas.setTextWrap(false);
    const char* composeItems[]={"전송 검토 / Send","한/영 전환 / Language","입력 삭제 / Clear","내용으로 / Back"};
    for(int i=0;i<(page==COMPOSE?4:2);i++){int y=page==COMPOSE?119+i*21:140+i*24;bool active=nav.focus==snowball::Focus::Content&&nav.index==i;canvas.fillRoundRect(7,y,306,20,3,active?accent:card);canvas.setTextColor(active?TFT_BLACK:TFT_WHITE,active?accent:card);canvas.setCursor(12,y+3);canvas.print(page==COMPOSE?composeItems[i]:i==0?"전송 / Send":"입력으로 / Back");}
  } else if(page==WIFI_PASSWORD){
    canvas.setCursor(10,35);canvas.print(fitText(wifiSsid,300));canvas.setCursor(10,55);canvas.print(wifiEditing?"암호 / Password":"SSID 입력 / Network name");canvas.setCursor(10,77);canvas.setTextWrap(true);if(wifiEditing){if(passwordVisible)canvas.print(wifiPassword);else for(size_t i=0;i<wifiPassword.length();i++)canvas.print('*');}canvas.setTextWrap(false);
    for(int i=0;i<(wifiEditing?3:2);i++){int y=134+i*22;bool active=nav.focus==snowball::Focus::Content&&nav.index==i;canvas.fillRoundRect(7,y,306,21,3,active?accent:card);canvas.setTextColor(active?TFT_BLACK:TFT_WHITE,active?accent:card);canvas.setCursor(12,y+3);canvas.print(!wifiEditing?(i==0?"암호 입력으로 / Next":"취소 / Back"):i==0?"연결 / Connect":i==1?(passwordVisible?"암호 숨기기 / Hide":"암호 보이기 / Show"):"취소 / Back");}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(10,193);canvas.print("Tab: 표시/숨김   Backspace: 삭제");
  } else if(page==INFO){canvas.setCursor(10,36);canvas.printf("%s\nESP32 / %u MB\nFACES: %s\nWi-Fi: %s\nIP: %s\n기기 등록: %s\n영문 / 한글 두벌식",deviceId.c_str(),ESP.getFlashChipSize()/1048576,faces?"FOUND":"ABSENT",WiFi.status()==WL_CONNECTED?"CONNECTED":"OFFLINE",WiFi.localIP().toString().c_str(),pairKey.length()==64?"등록됨":"필요");}
  footer();canvas.pushSprite(0,0);dirty=false;
}
void setup(){
  auto config=M5.config();config.internal_mic=false;config.internal_spk=true;M5.begin(config);Serial.begin(115200);M5.Display.setRotation(1);M5.Display.setBrightness(160);M5.Speaker.setVolume(64);
  canvas.setColorDepth(8);if(!canvas.createSprite(320,240)){M5.Display.print("Display allocation failed");while(true)delay(1000);}
  Wire.begin(21,22,100000);Wire.beginTransmission(0x08);faces=Wire.endTransmission()==0;pinMode(5,INPUT_PULLUP);
  uint64_t mac=ESP.getEfuseMac();char id[20];snprintf(id,sizeof(id),"m5-%02x%02x%02x%02x%02x%02x",(uint8_t)mac,(uint8_t)(mac>>8),(uint8_t)(mac>>16),(uint8_t)(mac>>24),(uint8_t)(mac>>32),(uint8_t)(mac>>40));deviceId=id;bootId=randomHex(8);
  prefs.begin("snowball",false);pairKey=prefs.getString("pair","");wifiSsid=prefs.getString("ssid","");wifiPassword=prefs.getString("password","");WiFi.persistent(false);
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
