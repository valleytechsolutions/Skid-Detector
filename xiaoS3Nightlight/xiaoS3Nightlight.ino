/*
 * Black Wire Militia Blue-Team Nightlight
 * Copyright (c) 2026 Your Pal Kal, Valleytech Solutions (@valleytechsolutions)
 *
 * This file is part of the Black Wire Militia Blue-Team Nightlight project.
 * Use, modification, and distribution of this software require attribution
 * to the original author. See the LICENSE file for full terms.
 *
 * Created by Your Pal Kal
 * @valleytechsolutions
 */

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <set>
#include <string>
#include <vector>
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#define DATA_PIN  A0
#define NUM_LEDS  60

Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);

const char* ssid     = "SKIDDETECTOR";
const char* password = "12345678";

WebServer server(80);

bool    ledsOn     = true;
uint8_t currentR   = 255, currentG = 100, currentB = 0;
uint8_t brightness = 80;
int     mode       = 2;

int spdFromSlider(int s) {
  const int delays[] = {0,2000,1000,600,400,300,200,120,80,50,35,20};
  if (s<1) s=1; if (s>11) s=11;
  return delays[s];
}
int spd = 200;

uint8_t hue           = 0;
float   cycleHue      = 0.0f;
uint8_t meteorPos     = 0;
uint8_t sparkleHue    = 0;
float   breatheVal    = 0.0f;
float   breatheDir    = 1.0f;
int     partyStep     = 0;
uint8_t strobeEffStep = 0;
unsigned long lastUpdate = 0;

bool deauthAlert    = false;
bool btAlert        = false;
bool axonAlert      = false;
bool strobing       = false;
bool axonPulse      = false;
bool alertDismissed = false;
int  strobeStep     = 0;
unsigned long lastStrobe      = 0;
unsigned long strobeStartTime = 0;
unsigned long axonPulseStart  = 0;
#define ATTACK_STROBE_MS 5000
#define AXON_PULSE_MS    5000

// Deauth detection
volatile int  deauthCount         = 0;
unsigned long deauthWindow        = 0;
int           deauthConfirm       = 0;
int           deauthMissedWindows = 0;
#define DEAUTH_WINDOW_MS  2000
#define DEAUTH_THRESHOLD  30
#define DEAUTH_CONFIRM    2
#define DEAUTH_CLEAR      3

// BLE detection — background task on core 0
BLEScan*          pBLEScan        = nullptr;
SemaphoreHandle_t bleMutex        = NULL;
volatile int      bleScanPackets  = 0;
volatile int      bleScanMACs     = 0;
volatile bool     bleScanDone     = false;
volatile bool     axonFoundInScan = false;
String            axonFoundMAC    = "";

// BLE spike detection state
#define BLE_HISTORY_SIZE  6
int bleHistPkt[BLE_HISTORY_SIZE]  = {0,0,0,0,0,0};
int bleHistMAC[BLE_HISTORY_SIZE]  = {0,0,0,0,0,0};
int bleHistIdx                    = 0;
int bleHistCount                  = 0;
int bleMissedWindows              = 0;
int btSpamConfirm                 = 0;
#define BLE_SPIKE_RATIO   3.5f
#define BLE_ABS_PKT_MIN   40
#define BLE_ABS_MAC_MIN   15
#define BLE_SCAN_INTERVAL 5000
#define BLE_SCAN_SECS     1
#define BLE_CONFIRM       2
#define BLE_CLEAR         3

// Axon
int axonMissedWindows = 0;
int axonConfirm       = 0;

// Channel hop
unsigned long lastChanHop = 0;
uint8_t       chanIdx     = 0;
const uint8_t channels[]  = {1,6,11};

// Alert log
struct AlertEntry { String type, detail; unsigned long timestamp; };
std::vector<AlertEntry> alertLog;
unsigned long bootTime = 0;

void addLog(String type, String detail) {
  AlertEntry e;
  e.type=type; e.detail=detail; e.timestamp=millis()-bootTime;
  alertLog.push_back(e);
  if ((int)alertLog.size()>20) alertLog.erase(alertLog.begin());
}

struct WiFiNetwork { String ssid,mac,enc; int32_t rssi; int channel; };
struct BLEEntry    { String mac,name;     int rssi; };
std::vector<WiFiNetwork> wifiResults;
std::vector<BLEEntry>    bleResults;

bool isAxonMAC(const std::string& mac) {
  if (mac.size()<8) return false;
  String m=String(mac.c_str()); m.toUpperCase();
  return (m.startsWith("00:25:DF")||m.startsWith("00:58:28")||
          m.startsWith("00:C0:D4")||m.startsWith("84:70:03"));
}

uint32_t hsv(uint8_t h,uint8_t s,uint8_t v) {
  uint8_t r,g,b;
  if (s==0){r=g=b=v;}
  else {
    uint8_t region=h/43,rem=(h-(region*43))*6;
    uint8_t p=(v*(255-s))>>8;
    uint8_t q=(v*(255-((s*rem)>>8)))>>8;
    uint8_t t=(v*(255-((s*(255-rem))>>8)))>>8;
    switch(region){
      case 0:r=v;g=t;b=p;break; case 1:r=q;g=v;b=p;break;
      case 2:r=p;g=v;b=t;break; case 3:r=p;g=q;b=v;break;
      case 4:r=t;g=p;b=v;break; default:r=v;g=p;b=q;break;
    }
  }
  return strip.Color(r,g,b);
}

void fillAll(uint32_t c){for(int i=0;i<NUM_LEDS;i++)strip.setPixelColor(i,c);}

void fadeToBlack(int amt){
  for(int i=0;i<NUM_LEDS;i++){
    uint32_t c=strip.getPixelColor(i);
    strip.setPixelColor(i,strip.Color(
      max(0,(int)((c>>16)&0xFF)-amt),
      max(0,(int)((c>>8)&0xFF)-amt),
      max(0,(int)(c&0xFF)-amt)));
  }
}

void IRAM_ATTR wifi_sniffer_cb(void* buf,wifi_promiscuous_pkt_type_t type){
  if(type!=WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt=(wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p=pkt->payload;
  if(((p[0]&0x0C)>>2)==0&&((p[0]&0xF0)>>4)==12) deauthCount++;
}

// BLE task local state — only touched on core 0
static int taskPktCount        = 0;
static std::set<std::string> taskMACSet;
static bool   taskAxonFound    = false;
static String taskAxonMAC      = "";

class MyBLECallbacks:public BLEAdvertisedDeviceCallbacks{
  void onResult(BLEAdvertisedDevice dev) override{
    taskPktCount++;
    std::string mac=std::string(dev.getAddress().toString().c_str());
    taskMACSet.insert(mac);
    if(isAxonMAC(mac)){
      taskAxonFound=true;
      taskAxonMAC=String(mac.c_str());
    }
  }
};

// Manual scan tab — uses separate callback, deduplicates by MAC
class ScanBLECallbacks:public BLEAdvertisedDeviceCallbacks{
  void onResult(BLEAdvertisedDevice dev) override{
    BLEEntry e;
    e.mac=String(dev.getAddress().toString().c_str());
    e.name=dev.haveName()?String(dev.getName().c_str()):String("Unknown");
    e.rssi=dev.getRSSI();
    for(auto& x:bleResults){if(x.mac==e.mac){x.rssi=e.rssi;return;}}
    bleResults.push_back(e);
  }
};

// BLE scan task — pinned to core 0, main loop on core 1
void bleScanTask(void* param){
  vTaskDelay(pdMS_TO_TICKS(8000)); // let WiFi and web server stabilize first
  for(;;){
    taskPktCount  = 0;
    taskMACSet.clear();
    taskAxonFound = false;
    taskAxonMAC   = "";

    if(pBLEScan){
      pBLEScan->start(BLE_SCAN_SECS,false);
      pBLEScan->clearResults();
    }

    if(xSemaphoreTake(bleMutex,pdMS_TO_TICKS(100))==pdTRUE){
      bleScanPackets  = taskPktCount;
      bleScanMACs     = (int)taskMACSet.size();
      axonFoundInScan = taskAxonFound;
      axonFoundMAC    = taskAxonMAC;
      bleScanDone     = true;
      xSemaphoreGive(bleMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_INTERVAL));
  }
}

void triggerAttackStrobe(){
  if(!alertDismissed){strobing=true;strobeStartTime=millis();}
}

void triggerAxonPulse(){
  if(!alertDismissed){axonPulse=true;axonPulseStart=millis();}
}

void handleWifiScan(){
  wifiResults.clear();
  esp_wifi_set_promiscuous(false);
  int n=WiFi.scanNetworks(false,true);
  for(int i=0;i<n;i++){
    WiFiNetwork net;
    net.ssid=WiFi.SSID(i).length()>0?WiFi.SSID(i):"[hidden]";
    net.mac=WiFi.BSSIDstr(i); net.rssi=WiFi.RSSI(i); net.channel=WiFi.channel(i);
    switch(WiFi.encryptionType(i)){
      case WIFI_AUTH_OPEN:         net.enc="Open";    break;
      case WIFI_AUTH_WEP:          net.enc="WEP";     break;
      case WIFI_AUTH_WPA_PSK:      net.enc="WPA";     break;
      case WIFI_AUTH_WPA2_PSK:     net.enc="WPA2";    break;
      case WIFI_AUTH_WPA_WPA2_PSK: net.enc="WPA/2";   break;
      case WIFI_AUTH_WPA3_PSK:     net.enc="WPA3";    break;
      default:                     net.enc="Unknown"; break;
    }
    String mu=net.mac; mu.toUpperCase();
    if(mu.startsWith("00:25:DF")||mu.startsWith("00:58:28")||
       mu.startsWith("00:C0:D4")||mu.startsWith("84:70:03")){
      axonConfirm++;
      addLog("AXON","WiFi OUI: "+net.mac+" SSID: "+net.ssid);
      if(!axonAlert){axonAlert=true;triggerAxonPulse();}
    }
    wifiResults.push_back(net);
  }
  WiFi.scanDelete();
  // Always restore promiscuous after scan
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);

  String j="[";
  for(int i=0;i<(int)wifiResults.size();i++){
    if(i>0)j+=",";
    j+="{\"ssid\":\""+wifiResults[i].ssid+"\",\"mac\":\""+wifiResults[i].mac+"\"";
    j+=",\"rssi\":"+String(wifiResults[i].rssi)+",\"ch\":"+String(wifiResults[i].channel);
    j+=",\"enc\":\""+wifiResults[i].enc+"\"}";
  }
  j+="]";
  server.send(200,"application/json",j);
}

void handleBLEScan(){
  // Returns last background scan results immediately — no blocking
  // This prevents the 3-second block that was causing iOS/Android timeouts
  bleResults.clear();
  if(xSemaphoreTake(bleMutex,pdMS_TO_TICKS(200))==pdTRUE){
    // Copy last scan's MAC set into bleResults for display
    // We use the background task's last snapshot
    int pkt=bleScanPackets, macs=bleScanMACs;
    xSemaphoreGive(bleMutex);
    // Build a simple result list from what the task saw
    // For the manual tab we run a fresh short scan without blocking the loop
    // by doing it synchronously here — acceptable since user tapped the button
  }
  // Run a short dedicated scan for the manual tab
  BLEScan* sp=BLEDevice::getScan();
  sp->setAdvertisedDeviceCallbacks(new ScanBLECallbacks(),false);
  sp->setActiveScan(true);
  sp->setInterval(50);
  sp->setWindow(40);
  sp->start(2,false); // 2 seconds — short enough to not timeout
  sp->clearResults();
  // Restore background callbacks
  sp->setAdvertisedDeviceCallbacks(new MyBLECallbacks(),true);
  sp->setActiveScan(true);
  sp->setInterval(10);
  sp->setWindow(9);

  String j="[";
  for(int i=0;i<(int)bleResults.size();i++){
    if(i>0)j+=",";
    String nm=bleResults[i].name; nm.replace("\"","'");
    j+="{\"mac\":\""+bleResults[i].mac+"\",\"name\":\""+nm+"\",\"rssi\":"+String(bleResults[i].rssi)+"}";
  }
  j+="]";
  server.send(200,"application/json",j);
}

void handleAlertLog(){
  String j="[";
  for(int i=0;i<(int)alertLog.size();i++){
    if(i>0)j+=",";
    unsigned long sec=alertLog[i].timestamp/1000,mn=sec/60; sec=sec%60;
    char ts[12]; sprintf(ts,"%02lu:%02lu",mn,sec);
    j+="{\"type\":\""+alertLog[i].type+"\",\"detail\":\""+alertLog[i].detail+"\",\"time\":\""+String(ts)+"\"}";
  }
  j+="]";
  server.send(200,"application/json",j);
}

void handleAlerts(){
  String j="{\"deauth\":";
  j+=(deauthAlert&&!alertDismissed)?"true":"false";
  j+=",\"bt\":"; j+=(btAlert&&!alertDismissed)?"true":"false";
  j+=",\"axon\":"; j+=(axonAlert&&!alertDismissed)?"true":"false";
  j+=",\"dismissed\":"; j+=alertDismissed?"true":"false"; j+="}";
  server.send(200,"application/json",j);
}

void handleToggle(){
  ledsOn=!ledsOn;
  if(!ledsOn){strip.clear();strip.show();}
  server.send(200,"text/plain",ledsOn?"on":"off");
}

void handleMode(){
  String m=server.arg("mode");
  ledsOn=true;strobing=false;axonPulse=false;alertDismissed=false;
  partyStep=0;strobeEffStep=0;
  if      (m=="solid")         {mode=0;currentR=server.arg("r").toInt();currentG=server.arg("g").toInt();currentB=server.arg("b").toInt();}
  else if (m=="rainbow_wave")  mode=1;
  else if (m=="rainbow_solid") mode=2;
  else if (m=="rainbow_chase") mode=3;
  else if (m=="color_cycle")   mode=4;
  else if (m=="sparkle")       mode=5;
  else if (m=="meteor")        mode=6;
  else if (m=="breathe")       mode=7;
  else if (m=="strobe_effect") mode=8;
  else if (m=="party")         mode=9;
  else if (m=="fire")          mode=10;
  server.send(200,"text/plain","ok");
}

void handleSpeed(){
  spd=spdFromSlider(server.arg("v").toInt());
  server.send(200,"text/plain","ok");
}

void handleBrightness(){
  brightness=server.arg("v").toInt();
  strip.setBrightness(brightness); strip.show();
  server.send(200,"text/plain","ok");
}

void handleDismiss(){
  alertDismissed=true; strobing=false; axonPulse=false;
  server.send(200,"text/plain","ok");
}

void handleRoot(){
  String h="";
  h+=F("<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<meta name='apple-mobile-web-app-capable' content='yes'>"
    "<title>SKID DETECTOR</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700&display=swap' rel='stylesheet'>"
    "<style>"
    "*{-webkit-box-sizing:border-box;box-sizing:border-box;margin:0;padding:0}"
    "html,body{width:100%;min-height:100%;-webkit-text-size-adjust:none}"
    "body{background:#000;color:#00ff41;font-family:'Share Tech Mono',monospace;"
    "-webkit-font-smoothing:antialiased;display:-webkit-flex;display:flex;"
    "-webkit-flex-direction:column;flex-direction:column;"
    "-webkit-align-items:center;align-items:center;"
    "min-height:100vh;padding:20px 16px 60px;gap:12px;"
    "position:relative;overflow-x:hidden}"
    "#mc{position:fixed;top:0;left:0;width:100%;height:100%;z-index:0;pointer-events:none}"
    ".w{position:relative;z-index:1;width:100%;display:-webkit-flex;display:flex;"
    "-webkit-flex-direction:column;flex-direction:column;"
    "-webkit-align-items:center;align-items:center;gap:12px}"
    ".hdr{display:-webkit-flex;display:flex;-webkit-flex-direction:column;flex-direction:column;"
    "-webkit-align-items:center;align-items:center;gap:10px;padding-top:8px}"
    ".lw{width:88px;height:88px;background:#000;border-radius:8px;"
    "display:-webkit-flex;display:flex;-webkit-align-items:center;align-items:center;"
    "-webkit-justify-content:center;justify-content:center;"
    "border:1px solid #00ff41;box-shadow:0 0 14px rgba(0,255,65,.6),0 0 32px rgba(0,255,65,.2)}"
    ".bn{font-family:'Orbitron',monospace;font-size:.82rem;letter-spacing:.12em;"
    "color:#00ff41;text-align:center;line-height:1.5;text-shadow:0 0 8px rgba(0,255,65,.8)}"
    ".bs{font-size:.58rem;letter-spacing:.2em;color:#00ff41;text-align:center;"
    "text-shadow:0 0 6px rgba(0,255,65,.5)}"
    ".sc{width:100%;max-width:340px;height:1px;"
    "background:linear-gradient(to right,transparent,#00ff41,transparent);"
    "box-shadow:0 0 8px rgba(0,255,65,.7)}"
    ".tabs{display:-webkit-flex;display:flex;width:100%;max-width:340px;"
    "border:1px solid #0a3d0a;border-radius:6px;overflow:hidden}"
    ".tab{-webkit-flex:1;flex:1;padding:9px 2px;font-family:'Share Tech Mono',monospace;"
    "font-size:.56rem;letter-spacing:.04em;text-align:center;cursor:pointer;"
    "background:#000d00;color:#1a5c1a;border:none;-webkit-appearance:none;"
    "text-transform:uppercase;-webkit-tap-highlight-color:transparent}"
    ".tab.act{background:#071f07;color:#00ff41;box-shadow:inset 0 0 10px rgba(0,255,65,.15)}"
    ".pane{display:none;width:100%;-webkit-flex-direction:column;flex-direction:column;"
    "-webkit-align-items:center;align-items:center;gap:12px}"
    ".pane.act{display:-webkit-flex;display:flex}"
    ".cd{background:rgba(0,8,0,.95);border:1px solid #0a3d0a;border-radius:6px;"
    "padding:14px;width:100%;max-width:340px;display:-webkit-flex;display:flex;"
    "-webkit-flex-direction:column;flex-direction:column;gap:10px}"
    ".ct{font-size:.56rem;color:#1a5c1a;letter-spacing:.2em;"
    "border-bottom:1px solid #0a3d0a;padding-bottom:5px}"
    "button{width:100%;padding:11px;border:1px solid #0d5c0d;border-radius:4px;"
    "font-family:'Share Tech Mono',monospace;font-size:.72rem;letter-spacing:.08em;"
    "cursor:pointer;background:#000d00;color:#00c832;"
    "-webkit-appearance:none;appearance:none;text-transform:uppercase;"
    "-webkit-tap-highlight-color:transparent}"
    "button:active{opacity:.5}"
    "button.on{border-color:#00ff41;color:#00ff41;background:#071f07;"
    "box-shadow:0 0 10px rgba(0,255,65,.45)}"
    ".tb{background:#071407;border:2px solid #00ff41;color:#00ff41;"
    "font-family:'Orbitron',monospace;font-size:.8rem;font-weight:700;"
    "letter-spacing:.2em;padding:13px;box-shadow:0 0 10px rgba(0,255,65,.3)}"
    ".tb.off{background:#140400;border-color:#ff4400;color:#ff4400;"
    "box-shadow:0 0 10px rgba(255,68,0,.35)}"
    ".g2{display:-webkit-flex;display:flex;-webkit-flex-wrap:wrap;flex-wrap:wrap;gap:7px}"
    ".g2 button{-webkit-flex:1 1 calc(50% - 4px);flex:1 1 calc(50% - 4px)}"
    "input[type=color]{width:100%;height:44px;border:1px solid #0d5c0d;border-radius:4px;"
    "cursor:pointer;background:#000d00;padding:2px;-webkit-appearance:none}"
    ".sr{display:-webkit-flex;display:flex;-webkit-flex-direction:column;flex-direction:column;gap:8px}"
    ".sv{font-size:.56rem;color:#1a5c1a;display:-webkit-flex;display:flex;"
    "-webkit-justify-content:space-between;justify-content:space-between;margin-bottom:2px}"
    ".spd-val{text-align:center;font-size:.7rem;color:#00ff41;letter-spacing:.1em;padding:4px 0}"
    "input[type=range]{width:100%;cursor:pointer;-webkit-appearance:none;appearance:none;"
    "height:6px;background:#0a3d0a;border-radius:3px;outline:none}"
    "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;"
    "border-radius:50%;background:#00ff41;cursor:pointer;border:3px solid #071f07;"
    "box-shadow:0 0 6px rgba(0,255,65,.5)}"
    ".ab{width:100%;max-width:340px;border-radius:6px;padding:16px 14px;"
    "text-align:center;display:none;font-family:'Orbitron',monospace}"
    ".ad{background:#1a0000;border:2px solid #f00;color:#f44;"
    "text-shadow:0 0 10px rgba(255,0,0,.9);"
    "-webkit-animation:rp .7s ease-in-out infinite alternate;"
    "animation:rp .7s ease-in-out infinite alternate}"
    ".ab2{background:#00001a;border:2px solid #44f;color:#68f;"
    "text-shadow:0 0 10px rgba(68,68,255,.9);"
    "-webkit-animation:bp .7s ease-in-out infinite alternate;"
    "animation:bp .7s ease-in-out infinite alternate}"
    ".ab3{background:#1a1400;border:2px solid #ffcc00;color:#ffcc00;"
    "text-shadow:0 0 10px rgba(255,204,0,.9);"
    "-webkit-animation:yp 1.2s ease-in-out infinite alternate;"
    "animation:yp 1.2s ease-in-out infinite alternate}"
    "@-webkit-keyframes rp{from{box-shadow:0 0 15px rgba(255,0,0,.5)}to{box-shadow:0 0 50px #f00,0 0 80px rgba(255,0,0,.5)}}"
    "@keyframes rp{from{box-shadow:0 0 15px rgba(255,0,0,.5)}to{box-shadow:0 0 50px #f00,0 0 80px rgba(255,0,0,.5)}}"
    "@-webkit-keyframes bp{from{box-shadow:0 0 15px rgba(68,68,255,.5)}to{box-shadow:0 0 50px #44f,0 0 80px rgba(68,68,255,.5)}}"
    "@keyframes bp{from{box-shadow:0 0 15px rgba(68,68,255,.5)}to{box-shadow:0 0 50px #44f,0 0 80px rgba(68,68,255,.5)}}"
    "@-webkit-keyframes yp{from{box-shadow:0 0 8px rgba(255,204,0,.4);opacity:.8}to{box-shadow:0 0 30px #ffcc00,0 0 50px rgba(255,204,0,.4);opacity:1}}"
    "@keyframes yp{from{box-shadow:0 0 8px rgba(255,204,0,.4);opacity:.8}to{box-shadow:0 0 30px #ffcc00,0 0 50px rgba(255,204,0,.4);opacity:1}}"
    ".ai{font-size:1.5rem;display:block;margin-bottom:6px}"
    ".at{font-size:.78rem;font-weight:700;margin-bottom:5px;letter-spacing:.1em}"
    ".as{font-size:.56rem;opacity:.8;line-height:1.7;margin-bottom:10px}"
    ".db{padding:10px;font-size:.62rem;letter-spacing:.1em;border-radius:4px;"
    "cursor:pointer;font-family:'Share Tech Mono',monospace;text-transform:uppercase;"
    "width:100%;background:transparent;-webkit-appearance:none}"
    ".dr{border:1px solid #f00;color:#f44}"
    ".db2{border:1px solid #44f;color:#68f}"
    ".dy{border:1px solid #ffcc00;color:#ffcc00}"
    ".scan-btn{background:#071407;border:1px solid #00ff41;color:#00ff41;"
    "padding:12px;font-size:.72rem;letter-spacing:.1em}"
    ".sst{font-size:.6rem;color:#1a5c1a;text-align:center;min-height:16px}"
    ".ip-note{font-size:.58rem;color:#1a5c1a;text-align:center;padding:6px 0;line-height:1.6}"
    ".rt{width:100%;border-collapse:collapse;font-size:.55rem}"
    ".rt th{color:#1a5c1a;letter-spacing:.1em;padding:6px 4px;"
    "border-bottom:1px solid #0a3d0a;text-align:left;font-weight:normal}"
    ".rt td{padding:5px 4px;border-bottom:1px solid #071407;color:#00c832;word-break:break-all}"
    ".rt tr:last-child td{border-bottom:none}"
    ".rb{display:inline-block;height:6px;border-radius:3px;margin-left:4px;vertical-align:middle}"
    ".empty{font-size:.6rem;color:#1a5c1a;text-align:center;padding:12px}"
    ".log-entry{padding:7px 0;border-bottom:1px solid #071407;font-size:.56rem}"
    ".log-entry:last-child{border-bottom:none}"
    ".log-type{font-size:.52rem;letter-spacing:.1em;margin-bottom:3px}"
    ".log-type.deauth{color:#f44}.log-type.bt{color:#68f}.log-type.axon{color:#ffcc00}"
    ".log-detail{color:#00c832;line-height:1.5}"
    ".log-time{color:#1a5c1a;font-size:.5rem;margin-top:2px}"
    ".log-empty{font-size:.6rem;color:#1a5c1a;text-align:center;padding:12px}"
    ".ft{font-size:.56rem;color:#0a3d0a;letter-spacing:.06em;"
    "text-align:center;padding-top:4px;line-height:2.2}"
    ".hd{color:#1a5c1a;font-size:.6rem}"
    ".bk{-webkit-animation:bk 1s step-end infinite;animation:bk 1s step-end infinite}"
    "@-webkit-keyframes bk{50%{opacity:0}}@keyframes bk{50%{opacity:0}}"
    "</style></head><body>");

  h+=F("<canvas id='mc'></canvas><div class='w'>"
    "<div class='hdr'><div class='lw'>"
    "<svg viewBox='0 0 100 100' fill='none' xmlns='http://www.w3.org/2000/svg' width='68' height='68'>"
    "<circle cx='46' cy='16' r='10' stroke='#9b0025' stroke-width='5' fill='none'/>"
    "<circle cx='46' cy='16' r='3.2' fill='#9b0025'/>"
    "<line x1='46' y1='26' x2='46' y2='52' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='46' y1='29' x2='76' y2='50' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='76' y1='50' x2='52' y2='82' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='20' y1='52' x2='62' y2='52' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='26' y1='63' x2='56' y2='63' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='33' y1='74' x2='50' y2='74' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "</svg></div>"
    "<div class='bn'>BLACK WIRE MILITIA</div>"
    "<div class='bs'>SKID DETECTOR</div></div>"
    "<div class='ip-note'>connect to SKIDDETECTOR then browse to "
    "<strong style='color:#00ff41'>192.168.4.1</strong></div>"
    "<div class='sc'></div>");

  h+=F("<div class='tabs'>"
    "<button class='tab act' id='t0' onclick='showTab(0)'>LIGHTS</button>"
    "<button class='tab' id='t1' onclick='showTab(1)'>WIFI</button>"
    "<button class='tab' id='t2' onclick='showTab(2)'>BLE</button>"
    "<button class='tab' id='t3' onclick='showTab(3)'>LOG</button>"
    "</div>");

  h+=F("<div class='pane act' id='p0'>"
    "<div class='cd'><div class='ct'>// POWER</div>"
    "<button class='tb' id='pb' onclick='doTog()'>[ ON / OFF ]</button></div>"
    "<div class='cd'><div class='ct'>// LUMINOSITY</div>"
    "<div class='sr'><div class='sv'><span>DIM</span><span id='bv'>31%</span><span>BRIGHT</span></div>"
    "<input type='range' min='5' max='255' value='80' "
    "oninput=\"document.getElementById('bv').innerText=Math.round(this.value/255*100)+'%'\" "
    "onchange='setBr(this.value)'></div></div>"
    "<div class='cd'><div class='ct'>// SOLID COLOR</div>"
    "<input type='color' id='pk' value='#ff6400'>"
    "<button id='b-solid' onclick=\"setM('solid')\">&gt; APPLY COLOR</button></div>"
    "<div class='cd'><div class='ct'>// EFFECTS</div><div class='g2'>"
    "<button id='b-rainbow_wave' onclick=\"setM('rainbow_wave')\">WAVE</button>"
    "<button id='b-rainbow_solid' onclick=\"setM('rainbow_solid')\">SOLID SHIFT</button>"
    "<button id='b-rainbow_chase' onclick=\"setM('rainbow_chase')\">CHASE</button>"
    "<button id='b-sparkle' onclick=\"setM('sparkle')\">SPARKLE</button>"
    "<button id='b-meteor' onclick=\"setM('meteor')\">METEOR</button>"
    "<button id='b-breathe' onclick=\"setM('breathe')\">BREATHE</button>"
    "<button id='b-color_cycle' onclick=\"setM('color_cycle')\">COLOR CYCLE</button>"
    "<button id='b-strobe_effect' onclick=\"setM('strobe_effect')\">STROBE</button>"
    "<button id='b-party' onclick=\"setM('party')\">PARTY</button>"
    "<button id='b-fire' onclick=\"setM('fire')\">FIRE</button>"
    "</div></div>"
    "<div class='cd'><div class='ct'>// SPEED</div>"
    "<div class='sr'><div class='spd-val' id='spv'>MEDIUM</div>"
    "<input type='range' min='1' max='11' value='6' id='spdSlider' "
    "oninput='updSpd(this.value)' onchange='setSp(this.value)'>"
    "<div class='sv'><span>SLOWEST</span><span>FASTEST</span></div>"
    "</div></div>");

  h+=F("<div class='ab ad' id='da'><span class='ai'>&#9888;</span>"
    "<div class='at'>!! DEAUTH FLOOD !!</div>"
    "<div class='as'>WiFi deauth flood confirmed<br>"
    "Active forced disconnect attack in progress<br>"
    "LEDs strobing 5 seconds then resuming</div>"
    "<button class='db dr' onclick='dis()'>&#10005; DISMISS &amp; RESUME</button></div>"
    "<div class='ab ab2' id='ba'><span class='ai'>&#9888;</span>"
    "<div class='at'>!! BLE SPAM ATTACK !!</div>"
    "<div class='as'>BLE advertisement spike detected<br>"
    "SourApple or QuickPair spam in progress<br>"
    "LEDs strobing 5 seconds then resuming</div>"
    "<button class='db db2' onclick='dis()'>&#10005; DISMISS &amp; RESUME</button></div>"
    "<div class='ab ab3' id='aa'><span class='ai'>&#128247;</span>"
    "<div class='at'>!! AXON CAM DETECTED !!</div>"
    "<div class='as'>Axon Enterprise OUI detected nearby<br>"
    "Body camera or dock equipment present<br>"
    "LEDs amber 5 seconds then resuming</div>"
    "<button class='db dy' onclick='dis()'>&#10005; DISMISS</button></div>"
    "</div>");

  h+=F("<div class='pane' id='p1'>"
    "<div class='cd'><div class='ct'>// WIFI SCANNER</div>"
    "<button class='scan-btn' onclick='doWifi()'>&gt; SCAN FOR NETWORKS</button>"
    "<div class='sst' id='wst'>tap scan to begin</div></div>"
    "<div class='cd' id='wres' style='display:none'><div class='ct'>// RESULTS</div>"
    "<div id='wtbl'></div></div></div>"
    "<div class='pane' id='p2'>"
    "<div class='cd'><div class='ct'>// BLUETOOTH SCANNER</div>"
    "<button class='scan-btn' onclick='doBLE()'>&gt; SCAN NEARBY DEVICES</button>"
    "<div class='sst' id='bst'>tap scan to begin (2 sec scan)</div></div>"
    "<div class='cd' id='bres' style='display:none'><div class='ct'>// RESULTS</div>"
    "<div id='btbl'></div></div></div>"
    "<div class='pane' id='p3'>"
    "<div class='cd'><div class='ct'>// ALERT LOG</div>"
    "<button class='scan-btn' onclick='loadLog()'>&gt; REFRESH LOG</button>"
    "<div class='sst' id='lst'>tap refresh to load</div></div>"
    "<div class='cd' id='logres' style='display:none'><div class='ct'>// EVENTS</div>"
    "<div id='logtbl'></div></div></div>");

  h+=F("<div class='ft'>i love your face <span class='bk'>_</span><br>"
    "<span style='color:#071407'>&mdash; Your Pal Kal</span><br>"
    "<span class='hd'>@valleytechsolutions</span></div></div>");

  h+=F("<script>"
    "(function(){var c=document.getElementById('mc'),"
    "ctx=c.getContext('2d'),cols,drops,rh=120,rd=1;"
    "function rsz(){c.width=window.innerWidth;c.height=window.innerHeight;"
    "cols=Math.floor(c.width/16);drops=[];"
    "for(var i=0;i<cols;i++)drops[i]=Math.random()*-(c.height/16);}"
    "rsz();window.addEventListener('resize',rsz);"
    "var ch='01アイウエオカキクケコサシスセソタチツテト';"
    "function hs(h,s,l){return 'hsl('+h+','+s+'%,'+l+'%)';}"
    "function drw(){rh+=rd*0.15;if(rh>200)rd=-1;if(rh<80)rd=1;"
    "ctx.fillStyle='rgba(0,0,0,0.04)';ctx.fillRect(0,0,c.width,c.height);"
    "ctx.font='14px monospace';"
    "for(var i=0;i<drops.length;i++){var r=Math.random();"
    "if(r>.95)ctx.fillStyle='#fff';"
    "else if(r>.65)ctx.fillStyle=hs(rh,100,55);"
    "else ctx.fillStyle=hs(rh,100,25);"
    "ctx.fillText(ch[Math.floor(Math.random()*ch.length)],i*16,drops[i]*16);"
    "if(drops[i]*16>c.height&&Math.random()>.975)drops[i]=0;drops[i]+=.5;}}"
    "setInterval(drw,50);})();");

  h+=F("var on=true;"
    "var spdLabels=['','SLOWEST','VERY SLOW','SLOW','RELAXED','MEDIUM-SLOW',"
    "'MEDIUM','MEDIUM-FAST','FAST','VERY FAST','SUPER FAST','FASTEST'];"
    "function showTab(n){for(var i=0;i<4;i++){"
    "document.getElementById('t'+i).className='tab'+(i==n?' act':'');"
    "document.getElementById('p'+i).className='pane'+(i==n?' act':'');}"
    "if(n==3)loadLog();}"
    "function updSpd(v){document.getElementById('spv').innerText=spdLabels[parseInt(v)]||v;}"
    "function doTog(){fetch('/toggle').then(function(r){return r.text();})"
    ".then(function(s){on=s==='on';"
    "var b=document.getElementById('pb');"
    "b.className=on?'tb':'tb off';});}"
    "function setM(m){var q='?mode='+m;"
    "if(m==='solid'){var hx=document.getElementById('pk').value;"
    "q+='&r='+parseInt(hx.slice(1,3),16)"
    "+'&g='+parseInt(hx.slice(3,5),16)"
    "+'&b='+parseInt(hx.slice(5,7),16);}"
    "fetch('/mode'+q).then(function(){"
    "var btns=document.querySelectorAll('[id^=b-]');"
    "for(var i=0;i<btns.length;i++)btns[i].classList.remove('on');"
    "var a=document.getElementById('b-'+m);if(a)a.classList.add('on');});}"
    "function setSp(v){fetch('/speed?v='+v);}"
    "function setBr(v){fetch('/brightness?v='+v);}"
    "function dis(){fetch('/dismiss').then(function(){"
    "document.getElementById('da').style.display='none';"
    "document.getElementById('ba').style.display='none';"
    "document.getElementById('aa').style.display='none';});}"
    "function poll(){fetch('/alerts').then(function(r){return r.json();})"
    ".then(function(d){if(!d.dismissed){"
    "document.getElementById('da').style.display=d.deauth?'block':'none';"
    "document.getElementById('ba').style.display=d.bt?'block':'none';"
    "document.getElementById('aa').style.display=d.axon?'block':'none';}});}"
    "setInterval(poll,2000);poll();");

  h+=F("function bar(r){var p=Math.min(100,Math.max(0,(r+100)*2));"
    "var col=p>60?'#00ff41':p>30?'#ffaa00':'#ff4444';"
    "return '<span class=rb style=\"width:'+Math.round(p*.5)+'px;background:'+col+'\"></span> '+r+'dBm';}"
    "function doWifi(){document.getElementById('wst').innerText='scanning...';"
    "document.getElementById('wres').style.display='none';"
    "fetch('/wifiscan').then(function(r){return r.json();})"
    ".then(function(d){document.getElementById('wst').innerText=d.length+' networks found';"
    "if(d.length===0){document.getElementById('wtbl').innerHTML='<div class=empty>no networks found</div>';}else{"
    "var t='<table class=rt><tr><th>SSID</th><th>MAC</th><th>CH</th><th>ENC</th><th>SIG</th></tr>';"
    "for(var i=0;i<d.length;i++){t+='<tr><td>'+d[i].ssid+'</td><td>'+d[i].mac+'</td>"
    "<td>'+d[i].ch+'</td><td>'+d[i].enc+'</td><td>'+bar(d[i].rssi)+'</td></tr>';}"
    "t+='</table>';document.getElementById('wtbl').innerHTML=t;}"
    "document.getElementById('wres').style.display='block';"
    "}).catch(function(){document.getElementById('wst').innerText='scan failed';});}"
    "function doBLE(){document.getElementById('bst').innerText='scanning 2 seconds...';"
    "document.getElementById('bres').style.display='none';"
    "fetch('/blescan').then(function(r){return r.json();})"
    ".then(function(d){document.getElementById('bst').innerText=d.length+' devices found';"
    "if(d.length===0){document.getElementById('btbl').innerHTML='<div class=empty>no devices found</div>';}else{"
    "var t='<table class=rt><tr><th>MAC</th><th>NAME</th><th>SIG</th></tr>';"
    "for(var i=0;i<d.length;i++){t+='<tr><td>'+d[i].mac+'</td><td>'+d[i].name+'</td>"
    "<td>'+bar(d[i].rssi)+'</td></tr>';}"
    "t+='</table>';document.getElementById('btbl').innerHTML=t;}"
    "document.getElementById('bres').style.display='block';"
    "}).catch(function(){document.getElementById('bst').innerText='scan failed';});}"
    "function loadLog(){document.getElementById('lst').innerText='loading...';"
    "fetch('/alertlog').then(function(r){return r.json();})"
    ".then(function(d){document.getElementById('lst').innerText=d.length+' events recorded';"
    "if(d.length===0){document.getElementById('logtbl').innerHTML='<div class=log-empty>no events yet</div>';}else{"
    "var t='';for(var i=d.length-1;i>=0;i--){"
    "var tc=d[i].type==='DEAUTH'?'deauth':d[i].type==='BT'?'bt':'axon';"
    "t+='<div class=log-entry><div class=\"log-type '+tc+'\">'+d[i].type+'</div>';"
    "t+='<div class=log-detail>'+d[i].detail+'</div>';"
    "t+='<div class=log-time>'+d[i].time+' since boot</div></div>';}"
    "document.getElementById('logtbl').innerHTML=t;}"
    "document.getElementById('logres').style.display='block';"
    "}).catch(function(){document.getElementById('lst').innerText='load failed';});}"
    "window.onload=function(){"
    "var a=document.getElementById('b-rainbow_solid');if(a)a.classList.add('on');"
    "document.getElementById('spv').innerText='MEDIUM';};"
    "</script></body></html>");

  server.send(200,"text/html",h);
}

void runMode(){
  switch(mode){
    case 0: fillAll(strip.Color(currentR,currentG,currentB)); break;
    case 1: for(int i=0;i<NUM_LEDS;i++) strip.setPixelColor(i,hsv(hue+(i*10),255,255)); hue++; break;
    case 2: fillAll(hsv(hue,255,255)); hue++; break;
    case 3:
      strip.clear();
      for(int t=0;t<5;t++){int pos=(meteorPos+t)%NUM_LEDS;strip.setPixelColor(pos,hsv(hue+(t*20),255,255-t*40));}
      meteorPos=(meteorPos+1)%NUM_LEDS; hue++; break;
    case 4: cycleHue+=0.4f; if(cycleHue>=256.0f)cycleHue-=256.0f; fillAll(hsv((uint8_t)cycleHue,255,255)); break;
    case 5:
      fadeToBlack(20);
      for(int s=0;s<3;s++) strip.setPixelColor(random(NUM_LEDS),hsv(sparkleHue+random(30),200,255));
      sparkleHue+=2; break;
    case 6:
      fadeToBlack(40);
      for(int t=0;t<8;t++){int pos=(meteorPos+NUM_LEDS-t)%NUM_LEDS;strip.setPixelColor(pos,hsv(hue,255,max(0,255-t*28)));}
      meteorPos=(meteorPos+1)%NUM_LEDS; hue+=3; break;
    case 7:
      breatheVal+=0.025f*breatheDir;
      if(breatheVal>=1.0f){breatheVal=1.0f;breatheDir=-1.0f;}
      if(breatheVal<=0.0f){breatheVal=0.0f;breatheDir=1.0f;hue+=32;}
      fillAll(hsv(hue,255,(uint8_t)(breatheVal*breatheVal*255.0f))); break;
    case 8: strobeEffStep++; fillAll(strobeEffStep%2==0?strip.Color(255,255,255):strip.Color(0,0,0)); break;
    case 9:{
      uint8_t pc[6][3]={{255,0,0},{0,255,0},{0,0,255},{255,0,255},{0,255,255},{255,165,0}};
      fillAll(strip.Color(pc[partyStep%6][0],pc[partyStep%6][1],pc[partyStep%6][2]));
      partyStep++; break;
    }
    case 10:
      fadeToBlack(15);
      for(int j=0;j<3;j++){int pos=random(10);strip.setPixelColor(pos,strip.Color(255,random(80,160),0));}
      for(int i=NUM_LEDS-1;i>0;i--){
        uint32_t c=strip.getPixelColor(i-1);
        uint8_t r=((c>>16)&0xFF),g=(uint8_t)(((c>>8)&0xFF)*0.85f);
        if(r>4)r-=4;
        strip.setPixelColor(i,strip.Color(r,g,0));
      } break;
  }
  strip.show();
}

void runStrobe(){
  if(millis()-strobeStartTime>=ATTACK_STROBE_MS){strobing=false;return;}
  unsigned long now=millis();
  if(now-lastStrobe<80) return;
  lastStrobe=now; strobeStep++;
  fillAll(strobeStep%2==0?strip.Color(255,0,0):strip.Color(0,0,255));
  strip.show();
}

void runAxonPulse(){
  if(millis()-axonPulseStart>=AXON_PULSE_MS){axonPulse=false;return;}
  float t=(float)(millis()-axonPulseStart)/AXON_PULSE_MS;
  uint8_t bv=(uint8_t)((1.0f-0.5f*t)*220.0f);
  fillAll(strip.Color(bv,(uint8_t)(bv*0.55f),0));
  strip.show();
}

void checkDeauth(){
  unsigned long now=millis();
  if(now-deauthWindow<DEAUTH_WINDOW_MS) return;
  deauthWindow=now;
  int count=deauthCount; deauthCount=0;
  if(count>=DEAUTH_THRESHOLD){
    deauthMissedWindows=0; deauthConfirm++;
    if(deauthConfirm>=DEAUTH_CONFIRM){
      if(!deauthAlert){
        deauthAlert=true;
        addLog("DEAUTH","Flood: "+String(count)+" frames/2s");
      }
      triggerAttackStrobe();
    }
  } else {
    deauthConfirm=0;
    if(deauthAlert){
      deauthMissedWindows++;
      if(deauthMissedWindows>=DEAUTH_CLEAR){
        deauthAlert=false; deauthMissedWindows=0; alertDismissed=false;
      }
    }
  }
}

void checkBLE(){
  if(!bleScanDone) return;

  int packets=0, uniqueMACs=0;
  bool axonSeen=false; String axonMAC="";

  if(xSemaphoreTake(bleMutex,pdMS_TO_TICKS(50))==pdTRUE){
    packets    = bleScanPackets;
    uniqueMACs = bleScanMACs;
    axonSeen   = axonFoundInScan;
    axonMAC    = axonFoundMAC;
    bleScanDone= false;
    xSemaphoreGive(bleMutex);
  } else { return; }

  if(axonSeen){
    axonConfirm++;
    addLog("AXON","BLE OUI: "+axonMAC);
    if(!axonAlert){axonAlert=true;triggerAxonPulse();}
  }

  // Compute rolling average from history
  float pktAvg=0, macAvg=0;
  if(bleHistCount>0){
    int ps=0,ms=0,cnt=min(bleHistCount,BLE_HISTORY_SIZE);
    for(int i=0;i<cnt;i++){ps+=bleHistPkt[i];ms+=bleHistMAC[i];}
    pktAvg=(float)ps/cnt; macAvg=(float)ms/cnt;
  }

  // Update history every window
  bleHistPkt[bleHistIdx]=packets;
  bleHistMAC[bleHistIdx]=uniqueMACs;
  bleHistIdx=(bleHistIdx+1)%BLE_HISTORY_SIZE;
  if(bleHistCount<BLE_HISTORY_SIZE) bleHistCount++;

  // Spike detection: requires 3 history windows, then BOTH metrics must
  // hit absolute minimums AND spike 3.5x above their rolling averages
  bool hasHistory = (bleHistCount>=3);
  bool pktSpike   = (packets>=BLE_ABS_PKT_MIN)&&
                    (pktAvg<2.0f||(float)packets>=pktAvg*BLE_SPIKE_RATIO);
  bool macSpike   = (uniqueMACs>=BLE_ABS_MAC_MIN)&&
                    (macAvg<2.0f||(float)uniqueMACs>=macAvg*BLE_SPIKE_RATIO);
  bool spamDetected = hasHistory && pktSpike && macSpike;

  if(spamDetected){
    bleMissedWindows=0; btSpamConfirm++;
    if(btSpamConfirm>=BLE_CONFIRM){
      if(!btAlert){
        btAlert=true;
        addLog("BT","Spike pkt="+String(packets)+"(avg="+String(pktAvg,1)+")"
               +" MAC="+String(uniqueMACs)+"(avg="+String(macAvg,1)+")");
      }
      triggerAttackStrobe();
    }
  } else {
    btSpamConfirm=0;
    if(btAlert){
      bleMissedWindows++;
      if(bleMissedWindows>=BLE_CLEAR){
        btAlert=false; bleMissedWindows=0; alertDismissed=false;
      }
    }
  }

  if(axonAlert&&axonConfirm==0){
    axonMissedWindows++;
    if(axonMissedWindows>=4){axonAlert=false;axonMissedWindows=0;alertDismissed=false;}
  } else { axonMissedWindows=0; }
  axonConfirm=0;
}

void setup(){
  Serial.begin(115200);
  delay(2000);
  bootTime=millis();

  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
  Serial.println("LEDs OK");
  delay(500);

  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_AP);
  delay(500);

  IPAddress apIP(192,168,4,1);
  WiFi.softAPConfig(apIP,apIP,IPAddress(255,255,255,0));
  bool apOK=WiFi.softAP(ssid,password,6,0,4);
  delay(1000);

  if(apOK){
    Serial.print("AP OK: "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("AP FAILED — rebooting");
    delay(1000);
    ESP.restart();
  }

  server.on("/",           handleRoot);
  server.on("/toggle",     handleToggle);
  server.on("/mode",       handleMode);
  server.on("/speed",      handleSpeed);
  server.on("/brightness", handleBrightness);
  server.on("/alerts",     handleAlerts);
  server.on("/dismiss",    handleDismiss);
  server.on("/wifiscan",   handleWifiScan);
  server.on("/blescan",    handleBLEScan);
  server.on("/alertlog",   handleAlertLog);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println("Web server OK");
  delay(500);

  // Switch to AP+STA for promiscuous mode — after web server is confirmed
  WiFi.mode(WIFI_AP_STA);
  delay(300);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
  esp_wifi_set_channel(channels[0],WIFI_SECOND_CHAN_NONE);
  deauthWindow=millis();
  lastChanHop=millis();
  Serial.println("Sniffer OK");
  delay(300);

  BLEDevice::init("BWM");
  delay(500);
  pBLEScan=BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyBLECallbacks(),true);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(10);
  pBLEScan->setWindow(9);
  Serial.println("BLE OK");
  delay(300);

  bleMutex=xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    bleScanTask,
    "BLEScan",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("Ready — SKIDDETECTOR / 12345678 / 192.168.4.1");
}

void loop(){
  server.handleClient();

  unsigned long now=millis();

  if(now-lastChanHop>3000){
    lastChanHop=now;
    chanIdx=(chanIdx+1)%3;
    esp_wifi_set_channel(channels[chanIdx],WIFI_SECOND_CHAN_NONE);
  }

  checkDeauth();
  checkBLE();

  if(strobing&&!alertDismissed){runStrobe();return;}
  if(axonPulse&&!alertDismissed){runAxonPulse();return;}
  if(!ledsOn) return;
  if(now-lastUpdate<(unsigned long)spd) return;
  lastUpdate=now;
  runMode();
}