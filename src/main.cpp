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
#include "../firmware/i18n.h"
#include "../firmware/connection.h"
#include "../firmware/footer_icons.h"
using snowball::Ui;
static bool displayKorean=false;
static const char* tr(Ui key){return snowball::translate(key,displayKorean);}
static snowball::ConnectionFlow connection;

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
static String deviceId,controllerId,pairKey,hostIp,hostEpoch,bootId,discoverNonce,serialLine,wifiSsid,wifiPassword,notice="";
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
static String attemptSsid,attemptPassword;
static snowball::Navigation nav;
static snowball::ButtonGesture gestures[3];
static bool listInitial=false,followTail=false,layoutOk=true;
static int settingsIndex=0,wifiIndex=0;
static uint32_t nextContentFetch=0;
static int captureConnectionPhase=-1;
static uint32_t captureRequestedAt=0;

enum Page { SESSION_CONTENT=0, HOME=0, READ_REPLY=0, REMOTE_LIST=1, COMPOSE=2, CONFIRM=3, WIFI_LIST=4, WIFI_PASSWORD=5, INFO=7, SETTINGS=8, ACTIONS=9, CONNECT_STATUS=10, MIDDLEWARE_FIND=11, DISPLAY_LANGUAGE=12, INPUT_SETTINGS=13 };
static Page page=SESSION_CONTENT;
static const Ui settingsItems[]={Ui::WifiSettings,Ui::FindMiddleware,Ui::Theme,Ui::DisplayLanguage,Ui::InputSettings,Ui::DeviceInfo};
static const Ui actionItems[]={Ui::Compose,Ui::Model,Ui::Effort,Ui::Refresh,Ui::BackContent};
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
  if(view["connected"].as<bool>()&&(view["deviceId"].as<String>()!=deviceId||view["controllerId"].as<String>()!=controllerId)){notice=tr(Ui::IdentityMismatch);pending=false;dirty=true;return;}
  String previous=remoteView["sessionKey"]|"",acceptedOp=pendingOp;pendingOp="";
  remoteView.clear();remoteView.set(view);lastPoll=lastResponse=millis();pending=false;
  if(page==CONNECT_STATUS&&remoteView["connected"].as<bool>())connection.authenticated(millis());
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
  if(pending){notice=tr(Ui::Waiting);dirty=true;return;}
  pendingOp=op;DynamicJsonDocument action(4096);action["op"]=op;action["locale"]=displayKorean?"ko":"en";if(index>=0)action["index"]=index;
  bool sending=String(op)=="send";
  if(sending){action["text"]=input.text().c_str();action["commandId"]=commandId;}
  if(usbOnline()){
    DynamicJsonDocument packet(4608);packet["type"]="request";packet["deviceId"]=deviceId;packet["id"]=++requestId;packet["action"]=action;
    serialJson(packet);pendingId=requestId;pending=true;pendingReverse=false;pendingSend=sending;lastPoll=millis();notice=sending?tr(Ui::Sending):tr(Ui::Loading);dirty=true;return;
  }
  if(WiFi.status()!=WL_CONNECTED||pairKey.length()!=64||hostIp.isEmpty()||hostEpoch.isEmpty()){
    notice=tr(Ui::Offline);dirty=true;return;
  }
  String payload;serializeJson(action,payload);uint32_t seq=++wireSeq;
  DynamicJsonDocument envelope(6144);envelope["epoch"]=hostEpoch;envelope["boot"]=bootId;envelope["seq"]=seq;envelope["payload"]=payload;envelope["mac"]=hmac(material("request",seq,payload));String raw;serializeJson(envelope,raw);
  if(reverseReady&&reverseClient.connected()){
    reverseClient.print(raw);reverseClient.print('\n');pending=true;pendingReverse=true;pendingSend=sending;pendingId=seq;lastPoll=millis();notice=sending?tr(Ui::Sending):tr(Ui::Loading);dirty=true;return;
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
  http.end();hostEpoch="";notice=sending?tr(Ui::DeliveryUnknown):tr(Ui::ConnectionFailed);pendingSend=false;dirty=true;lastPoll=millis();
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
      hostEpoch=epoch;hostIp=reverseClient.remoteIP().toString();reverseReady=true;notice=tr(Ui::PcConnected);dirty=true;lastPoll=0;continue;
    }
    if(!pending||!pendingReverse||d["seq"].as<uint32_t>()!=pendingId||d["boot"].as<String>()!=bootId||d["epoch"].as<String>()!=hostEpoch)continue;
    String payload=d["payload"].as<String>();if(!safeEqual(d["mac"].as<String>(),hmac(material("response",pendingId,payload))))continue;
    DynamicJsonDocument v(12288);if(!deserializeJson(v,payload))acceptView(v.as<JsonVariantConst>());

  }
}
void discoverHost(){
  if(reverseReady&&reverseClient.connected()){notice=String(tr(Ui::MiddlewareConnected))+hostIp;dirty=true;request("poll");return;}
  if(WiFi.status()!=WL_CONNECTED){notice=tr(Ui::WifiRequired);dirty=true;return;}
  if(pairKey.length()!=64){notice=tr(Ui::EnrollmentRequired);dirty=true;return;}
  discoverNonce=randomHex(8);StaticJsonDocument<160>d;d["type"]="snowball.discover";d["nonce"]=discoverNonce;String raw;serializeJson(d,raw);
  IPAddress broadcast=WiFi.localIP();IPAddress mask=WiFi.subnetMask();for(int i=0;i<4;i++)broadcast[i]|=~mask[i];
  discovery.beginPacket(broadcast,47770);discovery.print(raw);discovery.endPacket();lastDiscovery=millis();notice=tr(Ui::Discovering);dirty=true;
}
void readDiscovery(){int size=discovery.parsePacket();if(size<=0)return;if(size>768){discovery.flush();return;}char buffer[769];int n=discovery.read(buffer,768);buffer[n]=0;StaticJsonDocument<1024>d;
  if(deserializeJson(d,buffer)||String(d["type"]|"")!="snowball.gateway"||d["nonce"].as<String>()!=discoverNonce||millis()-lastDiscovery>8000)return;
  String epoch=d["epoch"].as<String>(),ip=d["ip"].as<String>(),host=d["host"].as<String>();int port=d["port"]|0;IPAddress candidate;
  if(!hex(epoch,32)||!candidate.fromString(ip)||candidate!=discovery.remoteIP()||port<1024||port>65535)return;
  for(int i=0;i<4;i++)if((candidate[i]&WiFi.subnetMask()[i])!=(WiFi.localIP()[i]&WiFi.subnetMask()[i]))return;
  String proof="discover\n"+discoverNonce+"\n"+epoch+"\n"+ip+"\n"+String(port)+"\n"+host;
  if(!safeEqual(d["mac"].as<String>(),hmac(proof)))return;
  hostIp=ip;hostPort=port;hostEpoch=epoch;notice=String(tr(Ui::MiddlewareFound))+host;dirty=true;request("poll");
}
void startWifiAssociation(){
  // A successful background reconnect must persist its original credentials,
  // never the editable password currently shown in another Wi-Fi form.
  attemptSsid=wifiSsid;attemptPassword=wifiPassword;
  WiFi.begin(attemptSsid.c_str(),attemptPassword.c_str());wifiStarted=millis();
}
void resumeWifi(){
  if(!resumeAfterScan)return;resumeAfterScan=false;WiFi.setAutoReconnect(true);
  String saved=prefs.getString("ssid","");
  if(WiFi.status()!=WL_CONNECTED&&saved.length()){
    wifiSsid=saved;wifiPassword=prefs.getString("password","");wifiAssociationReady.store(false);startWifiAssociation();
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
    scanNotice=count?String(count)+" AP / 2.4GHz":tr(Ui::NoAps);
  } else scanNotice=String(tr(Ui::ScanFailed))+String(count)+")";
  // Keep a local snapshot: reconnect and retries may discard driver scan records.
  WiFi.scanDelete();dirty=true;
  DynamicJsonDocument result(4096);result["type"]="wifi-networks";result["ok"]=count>=0;result["code"]=count;result["total"]=scanTotal;result["attempts"]=scanAttempts;result["durationMs"]=scanDuration;
  JsonArray networks=result.createNestedArray("networks");for(int i=0;i<scanCount;i++){JsonObject n=networks.createNestedObject();n["ssid"]=scanNetworks[i].ssid;n["rssi"]=scanNetworks[i].rssi;n["secure"]=scanNetworks[i].secure;}
  serialJson(result);resumeWifi();
}
void scanWifi(){
  if(pendingSend){notice=tr(Ui::ScanAfterSend);dirty=true;return;}
  connection.cancel();page=WIFI_LIST;nav.list(scanCount+3,0,0);dirty=true;
  // Repeated button presses must not clear a scan that is already in flight.
  if(scanRunning)return;
  WiFi.mode(WIFI_STA);WiFi.scanDelete();
  if(WiFi.status()!=WL_CONNECTED){
    // The ESP32 driver rejects scans while association/reconnect is in progress.
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);wifiStarted=0;resumeAfterScan=true;
  }
  scanRunning=true;scanActive=false;scanStarted=scanAttemptAt=millis();scanAttempts=0;scanLastCode=0;cursor=0;scanNotice=tr(Ui::Scanning);
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
String knownPassword(const String& ssid){
  if(ssid.isEmpty())return String("");
  for(int i=0;i<8;i++)if(prefs.getString(("ssid"+String(i)).c_str(),"")==ssid)return prefs.getString(("pass"+String(i)).c_str(),"");
  return prefs.getString("ssid","")==ssid?prefs.getString("password",""):String("");
}
bool knownNetwork(const String& ssid){
  if(ssid.isEmpty())return false;
  if(prefs.getString("ssid","")==ssid)return true;
  for(int i=0;i<8;i++)if(prefs.getString(("ssid"+String(i)).c_str(),"")==ssid)return true;
  return false;
}
void saveNetwork(const String& ssid,const String& password){
  int slot=-1;for(int i=0;i<8;i++)if(prefs.getString(("ssid"+String(i)).c_str(),"")==ssid){slot=i;break;}
  if(slot<0){slot=prefs.getUInt("wifiNext",0)%8;prefs.putUInt("wifiNext",(slot+1)%8);}
  prefs.putString(("ssid"+String(slot)).c_str(),ssid);prefs.putString(("pass"+String(slot)).c_str(),password);
  prefs.putString("ssid",ssid);prefs.putString("password",password);
}
void findMiddleware(){
  cancelScan();resumeAfterScan=false;
  if(WiFi.status()!=WL_CONNECTED){notice=tr(Ui::WifiRequired);scanWifi();return;}
  wifiSsid=WiFi.SSID();wifiPassword=knownPassword(wifiSsid);
  connection.middleware(millis());page=CONNECT_STATUS;nav.list(0,0,0);dirty=true;draw();discoverHost();
}
void connectWifi(){
  if(wifiSsid.isEmpty()||wifiSsid.length()>32||wifiPassword.length()>63||(wifiPassword.length()>0&&wifiPassword.length()<8)){notice=tr(Ui::InvalidWifi);dirty=true;return;}
  // Persist only after successful association; wrong credentials cannot erase
  // the last working network. Explicit Forget is the only deletion path.
  cancelScan();resumeAfterScan=false;WiFi.setAutoReconnect(true);WiFi.mode(WIFI_STA);
  // begin() can return the old WL_CONNECTED state before the disconnect event.
  // Only a matching existing association or a fresh GOT_IP may persist a key.
  bool existing=WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiSsid&&WiFi.psk()==wifiPassword;
  wifiAssociationReady.store(existing);
  connection.wifi(millis());page=CONNECT_STATUS;nav.list(0,0,0);notice=tr(Ui::WifiConnecting);dirty=true;draw();
  if(!existing){
    // Retire the previous association before admitting GOT_IP for another key.
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);
    uint32_t retiring=millis();while(WiFi.status()==WL_CONNECTED&&millis()-retiring<1500)delay(5);
    if(WiFi.status()==WL_CONNECTED){connection.cancel();page=WIFI_PASSWORD;wifiEditing=true;passwordVisible=false;nav.list(3,0,0);notice=tr(Ui::WifiFailed);dirty=true;return;}
    wifiAssociationReady.store(false);WiFi.setAutoReconnect(true);
  }
  startWifiAssociation();
}
void syncNavigation(){
  if(page==SESSION_CONTENT){nav.returnCrumb=4;nav.configure(remoteView["contentTotal"]|0,11,true);}
  else if(page==REMOTE_LIST)nav.configure(listInitial?0:(remoteView["menuTotal"]|0),7,false);
  else if(page==SETTINGS)nav.configure(6,7,false);
  else if(page==ACTIONS)nav.configure(5,7,false);
  else if(page==DISPLAY_LANGUAGE||page==INPUT_SETTINGS)nav.configure(2,7,false);
  else if(page==MIDDLEWARE_FIND)nav.configure(3,7,false);
  else if(page==CONNECT_STATUS)nav.configure(4,4,false);
  else if(page==WIFI_LIST)nav.configure(scanCount+3,7,false);
  else if(page==COMPOSE){nav.focus=snowball::Focus::Content;nav.configure(1,1,false);}
  else if(page==WIFI_PASSWORD)nav.configure(wifiEditing?3:2,3,false);
  else if(page==CONFIRM)nav.configure(2,2,false);
  else nav.configure(8,11,true);
  cursor=nav.index;
}
void returnSession(bool top,bool refresh){
  connection.cancel();if(scanRunning)cancelScan();resumeWifi();page=SESSION_CONTENT;
  nav.index=readerOffset;nav.focus=top?snowball::Focus::Top:snowball::Focus::Content;syncNavigation();
  expectedMenu="";listInitial=false;if(refresh)request("read",nav.index);dirty=true;
}
void openRemote(const char* kind,int origin){
  if(pending){notice=tr(Ui::Wait);dirty=true;return;}
  page=REMOTE_LIST;expectedMenu=kind;listInitial=true;nav.list(0,0,origin);request(kind);dirty=true;
}
void beginCompose(){
  if(String(remoteView["sessionKey"]|"").isEmpty()){notice=tr(Ui::NoSession);dirty=true;return;}
  if(input.text().empty())draftSession=remoteView["sessionKey"].as<String>();
  editorNotice="";page=COMPOSE;nav.list(1,0,4);dirty=true;
}
void back(){
  if(page==WIFI_PASSWORD){page=WIFI_LIST;passwordVisible=false;nav.list(scanCount+3,wifiIndex,0);dirty=true;return;}
  if(page==WIFI_LIST||page==INFO||page==DISPLAY_LANGUAGE||page==INPUT_SETTINGS||page==MIDDLEWARE_FIND||page==CONNECT_STATUS){if(scanRunning)cancelScan();connection.cancel();resumeWifi();page=SETTINGS;nav.list(6,settingsIndex,0);dirty=true;return;}
  if(page==CONFIRM){page=COMPOSE;nav.list(1,0,4);dirty=true;return;}
  returnSession();
}
void select(){
  cursor=nav.index;
  if(nav.focus==snowball::Focus::Top){
    if(nav.crumb==0){page=SETTINGS;nav.list(6,settingsIndex,0);}
    else openRemote(nav.crumb==1?"machines":nav.crumb==2?"harnesses":nav.crumb==3?"projects":"sessions",nav.crumb);
  } else if(page==REMOTE_LIST){if(!listInitial&&!pending)request("select",cursor);}
  else if(page==SESSION_CONTENT)beginCompose();
  else if(page==SETTINGS){
    settingsIndex=cursor;
    if(cursor==0)scanWifi();else if(cursor==1)findMiddleware();else if(cursor==2){contrast=!contrast;request("skin");}else if(cursor==3){page=DISPLAY_LANGUAGE;nav.list(2,displayKorean?1:0,0);}else if(cursor==4){page=INPUT_SETTINGS;nav.list(2,input.korean?1:0,0);}else {page=INFO;nav.list(8,0,0);nav.reader=true;}
  } else if(page==DISPLAY_LANGUAGE){
    displayKorean=cursor==1;prefs.putBool("displayKo",displayKorean);notice=tr(Ui::DisplayHints);request("poll");
  } else if(page==INPUT_SETTINGS){
    if(input.korean!=(cursor==1))toggleInputLanguage();notice=tr(Ui::InputSettingsHints);
  } else if(page==MIDDLEWARE_FIND){
    if(cursor==0)findMiddleware();else if(cursor==1)scanWifi();else back();
  } else if(page==ACTIONS){
    if(cursor==0)beginCompose();else if(cursor==1)openRemote("models",4);else if(cursor==2)openRemote("efforts",4);else returnSession();
  } else if(page==COMPOSE){
    if(pending){editorNotice=tr(Ui::Confirming);}
    else if(input.text().empty()){editorNotice=tr(Ui::EmptyInput);}
    else if(draftSession!=remoteView["sessionKey"].as<String>()){editorNotice=tr(Ui::DraftMismatch);}
    else if(remoteView["readOnly"]|true){editorNotice=tr(Ui::ReadOnly);}
    else if(commandId.length()){request("status");}
    else {commandId=randomHex(16);request("send");}
  } else if(page==CONFIRM){
    if(cursor==1)back();else if(draftSession!=remoteView["sessionKey"].as<String>()){notice=tr(Ui::SendCancelled);back();}else request("send");
  } else if(page==WIFI_LIST){
    if(scanRunning)return;wifiIndex=cursor;
    if(cursor<scanCount){wifiSsid=scanNetworks[cursor].ssid;wifiPassword=knownPassword(wifiSsid);wifiEditing=true;passwordVisible=false;page=WIFI_PASSWORD;nav.list(3,0,0);notice=knownNetwork(wifiSsid)?tr(Ui::SavedPassword):tr(Ui::EnterPassword);}
    else if(cursor==scanCount)scanWifi();
    else if(cursor==scanCount+1){wifiSsid="";wifiPassword="";wifiEditing=false;passwordVisible=false;page=WIFI_PASSWORD;nav.list(2,0,0);notice=tr(Ui::EnterHidden);}
    else {prefs.remove("ssid");prefs.remove("password");for(int i=0;i<8;i++){prefs.remove(("ssid"+String(i)).c_str());prefs.remove(("pass"+String(i)).c_str());}resumeAfterScan=false;WiFi.setAutoReconnect(false);WiFi.disconnect(true,true);wifiStarted=0;notice=tr(Ui::WifiForgotten);back();}
  } else if(page==WIFI_PASSWORD){
    if(!wifiEditing){if(cursor==0){wifiPassword=knownPassword(wifiSsid);wifiEditing=true;nav.list(3,0,0);notice=knownNetwork(wifiSsid)?tr(Ui::SavedPassword):tr(Ui::WifiPassword);}else back();}
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
  if(page==CONNECT_STATUS&&button==1){if(event==Gesture::Click||event==Gesture::Hold||event==Gesture::Double)back();return;}
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
    else if(commandId.length()||pendingSend){notice=tr(Ui::PendingDraft);}
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
  StaticJsonDocument<384>d;d["type"]="screen_begin";d["width"]=320;d["height"]=240;d["format"]=raw?"rgb332":"rgb888";
  d["page"]=(int)page;d["connectionPhase"]=(int)connection.phase;d["connectionChangedAt"]=connection.changedAt;d["displayLanguage"]=displayKorean?"ko":"en";serialJson(d);
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
    if(pairKey.length()&&pairKey!=key){notice=tr(Ui::EnrollmentMismatch);dirty=true;return;}
    if(pairKey!=key)prefs.putString("pair",key);pairKey=key;hostEpoch=epoch;hostIp=d["host"].as<String>();hostPort=d["port"]|47771;bool first=!lastUsb;lastUsb=millis();if(first&&!pending)request("poll");return;
  }
  if(type=="response"&&pending&&!pendingReverse&&d["id"].as<uint32_t>()==pendingId){lastUsb=millis();acceptView(d["view"]);return;}
  if(type=="render"){notice=d["text"].as<String>();dirty=true;return;}
  if(type=="screenshot"){
    if(d["connectionPhase"].is<int>()){
      int phase=d["connectionPhase"];if(phase<1||phase>3)return;
      captureConnectionPhase=phase;captureRequestedAt=millis();return;
    }
    screenshot(d["raw"]|false);return;
  }
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
    else if(command=="select"&&(nav.focus==snowball::Focus::Top||page==REMOTE_LIST||page==DISPLAY_LANGUAGE||page==INPUT_SETTINGS))select();
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
  // Explicit UI/network QA uses the same native settings and connect handlers.
  // No prompt dispatch and no credential values are exported.
  if(type=="settings-check"&&!pendingSend){
    if(page==COMPOSE||page==CONFIRM||page==WIFI_PASSWORD||!input.text().empty())return;
    String action=d["action"]|"";
    if(action=="display"&&d["korean"].is<bool>()){displayKorean=d["korean"].as<bool>();prefs.putBool("displayKo",displayKorean);notice=tr(Ui::DisplayHints);request("poll");}
    else if(action=="input"&&d["korean"].is<bool>()){if(input.korean!=d["korean"].as<bool>())toggleInputLanguage();}
    else if(action=="settings"){connection.cancel();page=SETTINGS;nav.list(6,settingsIndex,0);}
    else if(action=="display-menu"){page=DISPLAY_LANGUAGE;nav.list(2,displayKorean?1:0,0);}
    else if(action=="input-menu"){page=INPUT_SETTINGS;nav.list(2,input.korean?1:0,0);}
    else if(action=="find")findMiddleware();
    else if(action=="connect-saved"){wifiSsid=prefs.getString("ssid","");wifiPassword=knownPassword(wifiSsid);if(wifiSsid.length())connectWifi();}
    else if(action=="saved-network"&&!scanRunning){
      String saved=prefs.getString("ssid","");
      for(int i=0;i<scanCount;i++)if(saved.length()&&scanNetworks[i].ssid==saved){page=WIFI_LIST;nav.list(scanCount+3,i,0);select();break;}
    }
    dirty=true;return;
  }
  if(type=="connection-inspect"){
    StaticJsonDocument<512> result;result["type"]="connection-state";result["page"]=(int)page;
    result["phase"]=(int)connection.phase;result["changedAt"]=connection.changedAt;result["uptimeMs"]=millis();
    result["wifi"]=WiFi.status()==WL_CONNECTED;result["middleware"]=middlewareOnline();result["pending"]=pending;
    result["displayLanguage"]=displayKorean?"ko":"en";result["inputLanguage"]=input.korean?"ko":"en";
    serialJson(result);return;
  }
  if(type=="inspect"){
    DynamicJsonDocument result(remoteView.memoryUsage()+4096);
    if(!result.capacity()){StaticJsonDocument<160> error;error["type"]="inspection-error";error["code"]="allocation_failed";error["heap"]=ESP.getFreeHeap();error["maxBlock"]=ESP.getMaxAllocHeap();serialJson(error);return;}
    result["type"]="inspection";result["deviceId"]=deviceId;result["faces"]=faces;result["wifi"]=WiFi.status()==WL_CONNECTED;result["ip"]=WiFi.localIP().toString();result["paired"]=pairKey.length()==64;result["reverse"]=reverseReady&&reverseClient.connected();result["page"]=(int)page;result["korean"]=input.korean;result["draft"]=input.text().c_str();result["heap"]=ESP.getFreeHeap();result["maxBlock"]=ESP.getMaxAllocHeap();result["view"]=remoteView;
    result["scanRunning"]=scanRunning;result["scanCount"]=scanCount;result["scanTotal"]=scanTotal;result["scanCode"]=scanLastCode;result["scanAttempts"]=scanAttempts;result["scanMessage"]=scanNotice;result["scanDurationMs"]=scanDuration;
    result["displayLanguage"]=displayKorean?"ko":"en";result["inputLanguage"]=input.korean?"ko":"en";result["connectionPhase"]=(int)connection.phase;result["connectionChangedAt"]=connection.changedAt;result["uptimeMs"]=millis();result["knownNetwork"]=page==WIFI_PASSWORD&&knownNetwork(wifiSsid);
    result["wifiStatus"]=(int)WiFi.status();result["wifiConnecting"]=wifiStarted!=0;result["pending"]=pending;
    result["focus"]=nav.focus==snowball::Focus::Top?"top":"content";result["crumb"]=nav.crumb;result["cursor"]=nav.index;result["editCaret"]=(uint32_t)input.caret();result["viewportStart"]=nav.start();result["layoutOk"]=layoutOk;
    result["passwordVisible"]=page==WIFI_PASSWORD&&wifiEditing&&passwordVisible;result["passwordLength"]=page==WIFI_PASSWORD&&wifiEditing?wifiPassword.length():0;
    serialJson(result);return;
  }
  // Hardware QA only: exercise the actual IME and display, never dispatch.
  if(type=="ime-check"&&!pending&&!commandId.length()){input.clear();input.korean=d["korean"]|false;String keys=d["keys"]|"";if(keys.length()>100)return;for(char c:keys)if(c>=32&&c<127)input.append(c);draftSession=remoteView["sessionKey"].as<String>();page=COMPOSE;nav.list(1,0,4);notice=tr(Ui::ImeDiagnostic);dirty=true;return;}
  if(type=="clear-check"&&!pending){input.clear();returnSession();return;}
}
String fitText(String label,int width){
  if(canvas.textWidth(label.c_str())<=width)return label;
  while(label.length()&&canvas.textWidth((label+"..").c_str())>width){size_t pos=label.length()-1;while(pos>0&&((uint8_t)label[pos]&0xc0)==0x80)pos--;label.remove(pos);}return label+"..";
}
enum class Symbol {Left,Right,Up,Down,PageUp,PageDown,Home,End,Select,Edit,Menu,Tail,Language,Back,Eye,Run};
void footerGlyph(int x,int y,const FooterIcon& icon){
  for(int row=0;row<icon.height;row++){
    uint16_t mask=pgm_read_word(icon.mask+row);
    for(int column=0;column<icon.width;column++)if(mask&(0x8000>>column)){
      uint8_t value=pgm_read_byte(icon.pixels+row*icon.width+column);
      uint8_t r=value&0xe0;r|=r>>3;r|=r>>6;uint8_t g=(value&0x1c)<<3;g|=g>>3;g|=g>>6;
      canvas.drawPixel(x+column,y+row,canvas.color565(r,g,(value&3)*85));
    }
  }
}
void symbol(int x,int y,Symbol kind,uint16_t ink){
  (void)ink;
  static const FooterIcon* const icons[]={&footerLeft,&footerRight,&footerUp,&footerDown,&footerPageUp,&footerPageDown,&footerHome,&footerEnd,&footerSelect,&footerEdit,&footerMenu,&footerTail,&footerLanguage,&footerBack,&footerEye,&footerRun};
  footerGlyph(x,y,*icons[static_cast<int>(kind)]);
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
  S click=page==CONNECT_STATUS?S::Back:nav.focus==snowball::Focus::Top?S::Select:page==SESSION_CONTENT?S::Edit:page==COMPOSE?S::Run:S::Select;
  S hold=page==COMPOSE?S::Language:page==SESSION_CONTENT?S::Menu:page==WIFI_PASSWORD&&wifiEditing?S::Eye:S::Back;
  S twice=page==SESSION_CONTENT&&nav.focus==snowball::Focus::Content?S::Tail:S::Back;
  for(int b=0;b<3;b++){
    int x=2+b*snowball::BoxStride,y=snowball::BottomY;
    canvas.fillRoundRect(x,y,104,27,5,card);canvas.drawRoundRect(x,y,104,27,5,accent);
    S actions[3]={b==0?(sideways?S::Left:S::Up):b==2?(sideways?S::Right:S::Down):click,
      b==0?(page==COMPOSE?S::Left:S::PageUp):b==2?(page==COMPOSE?S::Right:S::PageDown):hold,
      b==0?S::Home:b==2?S::End:twice};
    for(int slot=0;slot<3;slot++){
      int gx=x+2+slot*34;
      const FooterIcon* gesture=slot==0?&footerGestureTap:slot==1?&footerGestureHold:&footerGestureDouble;
      footerGlyph(gx,y+3,*gesture);symbol(gx+12,y+5,actions[slot],TFT_WHITE);
      if(slot<2)canvas.drawFastVLine(gx+32,y+5,17,contrast?accent:0x31aa);
      layoutOk=layoutOk&&gx+28<=x+104&&y+23<=240;
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
    String label=crumb==1?String(remoteView["machine"]|tr(Ui::Machine)):crumb==2?String(remoteView["harnessName"]|tr(Ui::Harness)):crumb==3?String(remoteView["project"]|""):String(remoteView["session"]|"...");
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
    if(lines.size()==0){canvas.setCursor(10,43);canvas.print(remoteView["connected"].as<bool>()?tr(Ui::EmptyContent):tr(Ui::MiddlewareConnecting));}
    else for(int row=0;row<11&&row<(int)lines.size();row++){String label=lines[row].as<String>();layoutOk=layoutOk&&canvas.textWidth(label.c_str())<=300;canvas.setCursor(10,35+row*14);canvas.print(fitText(label,300));}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,194);String status=(remoteView["readOnly"]|true)?tr(Ui::ReadOnlyShort):tr(Ui::Controllable);status+="  "+String(nav.index+1)+"/"+String(remoteView["contentTotal"]|0)+"  "+String(remoteView["model"]|"");
    if(!remoteView["connected"].as<bool>()||String(remoteView["commandStatus"]|"idle")!="idle")status=remoteView["message"]|tr(Ui::Offline);
    canvas.print(fitText(status,304));
    if(offset!=nav.index){canvas.fillCircle(314,197,2,TFT_ORANGE);}
  } else if(page==REMOTE_LIST||page==SETTINGS||page==ACTIONS||page==WIFI_LIST||page==DISPLAY_LANGUAGE||page==INPUT_SETTINGS||page==MIDDLEWARE_FIND){
    int start=nav.start(),total=nav.total,offset=remoteView["menuOffset"]|0;
    for(int row=0;row<7&&start+row<total;row++){
      int index=start+row,y=34+row*22;bool selected=nav.focus==snowball::Focus::Content&&index==nav.index;uint16_t color=selected?accent:card;
      canvas.fillRoundRect(7,y,306,21,3,color);canvas.setTextColor(selected?TFT_BLACK:TFT_WHITE,color);canvas.setCursor(12,y+3);String label;
      if(page==SETTINGS)label=tr(settingsItems[index]);else if(page==ACTIONS)label=tr(actionItems[index]);
      else if(page==DISPLAY_LANGUAGE)label=index==0?tr(Ui::DisplayEnglish):tr(Ui::DisplayKorean);
      else if(page==INPUT_SETTINGS)label=index==0?tr(Ui::English):tr(Ui::Korean);
      else if(page==MIDDLEWARE_FIND)label=index==0?tr(Ui::FindMiddleware):index==1?tr(Ui::WifiSettings):tr(Ui::Back);
      else if(page==REMOTE_LIST){int local=index-offset;if(!listInitial&&String(remoteView["menuKind"]|"")==expectedMenu&&local>=0&&local<(int)remoteView["items"].size())label=remoteView["items"][local].as<String>();else label=tr(Ui::LoadingShort);}
      else if(index<scanCount)label=scanNetworks[index].ssid+" "+String(scanNetworks[index].rssi)+"dBm";else label=index==scanCount?tr(Ui::ScanAgain):index==scanCount+1?tr(Ui::HiddenNetwork):tr(Ui::ForgetWifi);
      bool glyph=false;if(page==REMOTE_LIST&&expectedMenu=="harnesses"){int local=index-offset;if(local>=0)glyph=harnessGlyph(12,y+2,remoteView["itemIcons"][local],selected?TFT_BLACK:TFT_WHITE);}
      if(glyph)canvas.setCursor(36,y+3);canvas.print(fitText(label,glyph?271:295));
    }
    if(total==0){canvas.setTextColor(TFT_WHITE,bg);canvas.setCursor(10,45);canvas.print(pending||listInitial?tr(Ui::LoadingNative):tr(Ui::NoEntries));}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(8,193);canvas.print(fitText(page==WIFI_LIST?scanNotice:page==DISPLAY_LANGUAGE?String(tr(Ui::DisplayHints)):page==INPUT_SETTINGS?String(tr(Ui::InputSettingsHints)):notice,302));
  } else if(page==COMPOSE){
    canvas.setTextColor(accent,bg);canvas.setCursor(10,35);canvas.print(input.korean?tr(Ui::EditorKorean):tr(Ui::EditorEnglish));
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
    canvas.print(fitText(commandId.length()||pendingSend?notice:editorNotice.length()?editorNotice:String(tr(Ui::EditorHints)),304));
  } else if(page==WIFI_PASSWORD){
    canvas.setCursor(10,35);canvas.print(fitText(wifiSsid,300));canvas.setCursor(10,55);canvas.print(wifiEditing?tr(Ui::Password):tr(Ui::NetworkName));canvas.setCursor(10,77);canvas.setTextWrap(true);if(wifiEditing){if(passwordVisible)canvas.print(wifiPassword);else for(size_t i=0;i<wifiPassword.length();i++)canvas.print('*');}canvas.setTextWrap(false);
    for(int i=0;i<(wifiEditing?3:2);i++){int y=126+i*21;bool active=nav.focus==snowball::Focus::Content&&nav.index==i;canvas.fillRoundRect(7,y,306,20,3,active?accent:card);canvas.setTextColor(active?TFT_BLACK:TFT_WHITE,active?accent:card);canvas.setCursor(12,y+3);canvas.print(!wifiEditing?(i==0?tr(Ui::Next):tr(Ui::Back)):i==0?tr(Ui::Connect):i==1?(passwordVisible?tr(Ui::HidePassword):tr(Ui::ShowPassword)):tr(Ui::Back));}
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(10,193);canvas.print(fitText(notice.length()?notice:String(tr(Ui::PasswordHints)),300));
  } else if(page==CONNECT_STATUS){
    bool associated=connection.phase!=snowball::ConnectionPhase::Wifi;
    bool connected=connection.phase==snowball::ConnectionPhase::Connected;
    canvas.setCursor(10,38);canvas.print(fitText(wifiSsid,300));
    const Ui labels[]={Ui::WifiAttempt,Ui::WifiSuccess,Ui::MiddlewareAttempt,Ui::MiddlewareSuccess};
    for(int row=0;row<4;row++){
      int y=66+row*27;bool done=row==0||(row==1&&associated)||(row==2&&associated)||(row==3&&connected);
      if(nav.focus==snowball::Focus::Content&&nav.index==row)canvas.drawFastVLine(3,y-2,19,accent);
      canvas.setTextColor(done?TFT_WHITE:0x7bef,bg);canvas.setCursor(35,y);canvas.print(tr(labels[row]));
      if((row==0&&!associated)||(row==2&&associated&&!connected))canvas.drawCircle(18,y+7,5,accent);
      else if(done)symbol(10,y-1,Symbol::Select,TFT_WHITE);
    }
    canvas.setFont(&fonts::efontKR_10);canvas.setTextColor(accent,bg);canvas.setCursor(10,193);
    canvas.print(connected?tr(Ui::SessionNext):associated?tr(Ui::MiddlewareConnecting):tr(Ui::WifiConnecting));
  } else if(page==INFO){
    canvas.setCursor(10,36);canvas.printf("%s\nESP32 / %u MB\nFACES: %s\nWi-Fi: %s\nIP: %s\n%s: %s\n%s",deviceId.c_str(),ESP.getFlashChipSize()/1048576,faces?tr(Ui::Found):tr(Ui::Absent),WiFi.status()==WL_CONNECTED?tr(Ui::WifiSuccess):tr(Ui::WifiOffline),WiFi.localIP().toString().c_str(),tr(Ui::Enrollment),pairKey.length()==64?tr(Ui::Registered):tr(Ui::Needed),tr(Ui::InfoInput));
  }
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
  input.korean=prefs.getBool("korean",false);displayKorean=prefs.getBool("displayKo",false);notice=tr(Ui::Welcome);
  WiFi.onEvent([](WiFiEvent_t type,WiFiEventInfo_t event){
    if(type==ARDUINO_EVENT_WIFI_SCAN_DONE)scanEventStatus.store(event.wifi_scan_done.status);
    else if(type==ARDUINO_EVENT_WIFI_STA_GOT_IP)wifiAssociationReady.store(true);
    else if(type==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)wifiAssociationReady.store(false);
  });
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
  if(wifiSsid.length()){connection.wifi(millis());page=CONNECT_STATUS;startWifiAssociation();}
  discovery.begin(47773);reverseServer.begin();M5.BtnA.setHoldThresh(650);M5.BtnB.setHoldThresh(650);M5.BtnC.setHoldThresh(650);draw();hello();
}
void loop(){
  M5.update();
  if(M5.BtnA.pressedFor(2500)&&M5.BtnB.pressedFor(2500)&&M5.BtnC.pressedFor(2500)){prefs.remove("pair");pairKey="";hostEpoch="";notice=tr(Ui::ResetEnrollment);dirty=true;delay(500);}
  uint32_t now=millis();bool pressed[]={M5.BtnA.isPressed(),M5.BtnB.isPressed(),M5.BtnC.isPressed()};
  for(int b=0;b<3;b++)buttonAction(b,gestures[b].update(now,pressed[b]));
  if(faces&&digitalRead(5)==LOW){Wire.requestFrom(0x08,1);if(Wire.available()){uint8_t c=Wire.read();if(c&&c!=0xff)keyboard(c);}}
  while(Serial.available()){char c=Serial.read();if(c=='\n'){handleSerial(serialLine);serialLine="";}else if(c!='\r'){if(serialLine.length()<24576)serialLine+=c;else serialLine="";}}
  updateScan();
  if(wifiStarted){
    if(wifiAssociationReady.load()&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==attemptSsid&&WiFi.psk()==attemptPassword){
      saveNetwork(attemptSsid,attemptPassword);wifiStarted=0;if(!wifiUi())notice=String(tr(Ui::WifiConnected))+WiFi.localIP().toString();
      if(page==CONNECT_STATUS&&connection.phase==snowball::ConnectionPhase::Wifi){connection.middleware(millis());dirty=true;draw();discoverHost();}
      dirty=true;
    }else if(millis()-wifiStarted>=20000){wifiStarted=0;notice=tr(Ui::WifiFailed);dirty=true;}
  }
  auto connectionResult=connection.update(millis(),WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiSsid);
  if(connectionResult==snowball::ConnectionResult::WifiFailed){
    wifiStarted=0;WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);wifiEditing=true;passwordVisible=false;
    page=WIFI_PASSWORD;nav.list(3,0,0);notice=tr(Ui::WifiFailed);dirty=true;
  }else if(connectionResult==snowball::ConnectionResult::MiddlewareFailed){
    page=MIDDLEWARE_FIND;nav.list(3,0,0);notice=tr(Ui::MiddlewareFailed);dirty=true;
  }else if(connectionResult==snowball::ConnectionResult::OpenSession)returnSession();
  readDiscovery();
  readReverse();
  if(!lastUsb&&millis()-lastHello>3000)hello();
  if(pending&&millis()-lastPoll>7000){pending=false;pendingSend=false;notice=tr(Ui::ResponseUnknown);dirty=true;}
  if(!pending&&!scanRunning&&millis()-lastPoll>3500&&(usbOnline()||(!hostEpoch.isEmpty()&&WiFi.status()==WL_CONNECTED))&&page!=WIFI_LIST&&page!=WIFI_PASSWORD&&page!=CONFIRM&&page!=MIDDLEWARE_FIND)request("poll");
  if(!usbOnline()&&!wifiUi()&&page!=MIDDLEWARE_FIND&&WiFi.status()==WL_CONNECTED&&hostEpoch.isEmpty()&&millis()-lastDiscovery>8000)discoverHost();
  if(!pending&&!wifiUi()&&millis()-nextContentFetch>120){
    if(page==SESSION_CONTENT&&nav.index!=(remoteView["contentOffset"]|0)){nextContentFetch=millis();request("read",nav.index);}
    else if(page==REMOTE_LIST&&!listInitial&&String(remoteView["menuKind"]|"")==expectedMenu){int offset=remoteView["menuOffset"]|0;if(nav.start()!=offset){nextContentFetch=millis();request("browse",nav.index);}}
  }
  if(dirty)draw();
  if(captureConnectionPhase>=0){
    if(page==CONNECT_STATUS&&(int)connection.phase==captureConnectionPhase){captureConnectionPhase=-1;screenshot(true);}
    else if(millis()-captureRequestedAt>=25000){captureConnectionPhase=-1;StaticJsonDocument<128> error;error["type"]="screen_error";error["code"]="observed_phase_unavailable";serialJson(error);}
  }
  delay(5);
}
