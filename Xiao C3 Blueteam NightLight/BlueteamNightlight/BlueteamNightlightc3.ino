#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <set>
#include <string>
#include "esp_wifi.h"

#define DATA_PIN    2
#define NUM_LEDS    60
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

const char* ssid     = "xiaoNightlight";
const char* password = "12345678";

CRGB leds[NUM_LEDS];
WebServer server(80);

bool ledsOn        = true;
uint8_t currentR   = 255, currentG = 100, currentB = 0;
uint8_t brightness = 80;
int mode           = 1;
int spd            = 50;

// Animation state
uint8_t hue        = 0;
float   cycleHue   = 0.0f;
uint8_t meteorPos  = 0;
uint8_t sparkleHue = 0;
float   breatheVal = 0.0f;
float   breatheDir = 1.0f;
unsigned long lastUpdate = 0;

// Alert / strobe state
bool deauthAlert       = false;
bool btAlert           = false;
bool strobing          = false;
bool alertDismissed    = false;  // manual dismiss flag
int  strobeStep        = 0;
unsigned long lastStrobe = 0;

// Deauth tracking
volatile int deauthCount = 0;
unsigned long deauthWindow = 0;
int  deauthMissedWindows = 0;  // how many clean windows since attack

// BLE tracking
BLEScan* pBLEScan = nullptr;
std::set<std::string> bleMacsThisWindow;
unsigned long lastBLEScan    = 0;
int  bleMissedWindows        = 0;

int spdToDelay(int s) {
  if (s == 11) return 800;
  return map(s, 10, 1, 5, 150);
}

// ── WiFi sniffer ─────────────────────────────────────────────
void IRAM_ATTR wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t* payload = pkt->payload;
  uint8_t ftype   = (payload[0] & 0x0C) >> 2;
  uint8_t subtype = (payload[0] & 0xF0) >> 4;
  if (ftype == 0 && subtype == 12) deauthCount++;
}

// ── BLE callback ─────────────────────────────────────────────
class MyBLECallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    bleMacsThisWindow.insert(std::string(dev.getAddress().toString().c_str()));
  }
};

// ── HTML ─────────────────────────────────────────────────────
void handleRoot() {
  String p = "";

  p += F("<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Black Wire Blue-Team Nightlight</title>"
    "<link href='https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700&display=swap' rel='stylesheet'>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#000;color:#00ff41;font-family:'Share Tech Mono',monospace;"
    "display:flex;flex-direction:column;align-items:center;"
    "min-height:100vh;padding:20px 16px 40px;gap:12px;"
    "position:relative;overflow-x:hidden}"
    "#mc{position:fixed;top:0;left:0;width:100%;height:100%;"
    "z-index:0;opacity:0.15;pointer-events:none}"
    ".wrap{position:relative;z-index:1;width:100%;"
    "display:flex;flex-direction:column;align-items:center;gap:12px}"
    ".header{display:flex;flex-direction:column;align-items:center;gap:10px;padding-top:8px}"
    ".logo-wrap{width:80px;height:80px;background:#000;border-radius:8px;"
    "display:flex;align-items:center;justify-content:center;"
    "border:1px solid #00ff41;"
    "box-shadow:0 0 14px rgba(0,255,65,0.5),0 0 30px rgba(0,255,65,0.15)}"
    ".brand{font-family:'Orbitron',monospace;font-size:0.76rem;"
    "letter-spacing:0.1em;color:#00ff41;text-align:center;line-height:1.6;"
    "text-shadow:0 0 8px rgba(0,255,65,0.7)}"
    ".brand-sub{font-size:0.52rem;letter-spacing:0.15em;color:#1a5c1a;text-align:center}"
    ".scan-line{width:100%;max-width:340px;height:1px;"
    "background:linear-gradient(to right,transparent,#00ff41,transparent);"
    "box-shadow:0 0 6px rgba(0,255,65,0.6)}"
    ".card{background:rgba(0,8,0,0.93);border:1px solid #0a3d0a;"
    "border-radius:6px;padding:14px;width:100%;max-width:340px;"
    "display:flex;flex-direction:column;gap:10px;"
    "box-shadow:inset 0 0 20px rgba(0,255,65,0.03)}"
    ".ct{font-size:0.56rem;color:#1a5c1a;letter-spacing:0.2em;"
    "border-bottom:1px solid #0a3d0a;padding-bottom:5px}"
    "button{width:100%;padding:11px;border:1px solid #0d5c0d;"
    "border-radius:4px;font-family:'Share Tech Mono',monospace;"
    "font-size:0.72rem;letter-spacing:0.08em;cursor:pointer;"
    "background:#000d00;color:#00c832;transition:all 0.1s;text-transform:uppercase}"
    "button:hover{background:#071407;border-color:#00ff41;color:#00ff41;"
    "box-shadow:0 0 8px rgba(0,255,65,0.35)}"
    "button:active{opacity:0.5}"
    "button.active{border-color:#00ff41;color:#00ff41;background:#071f07;"
    "box-shadow:0 0 10px rgba(0,255,65,0.45)}"
    ".toggle-btn{background:#071407;border:2px solid #00ff41;color:#00ff41;"
    "font-family:'Orbitron',monospace;font-size:0.8rem;font-weight:700;"
    "letter-spacing:0.2em;padding:13px;"
    "box-shadow:0 0 10px rgba(0,255,65,0.3)}"
    ".toggle-btn.off{background:#140400;border-color:#ff4400;color:#ff4400;"
    "box-shadow:0 0 10px rgba(255,68,0,0.35)}"
    ".g2{display:grid;grid-template-columns:1fr 1fr;gap:7px}"
    "input[type=color]{width:100%;height:40px;border:1px solid #0d5c0d;"
    "border-radius:4px;cursor:pointer;background:#000d00;padding:2px}"
    ".sr{display:flex;flex-direction:column;gap:5px}"
    ".sl{font-size:0.56rem;color:#1a5c1a;display:flex;justify-content:space-between}"
    "input[type=range]{width:100%;accent-color:#00ff41;cursor:pointer}"
    ".alert-box{width:100%;max-width:340px;border-radius:6px;"
    "padding:16px 14px;text-align:center;display:none;"
    "font-family:'Orbitron',monospace}"
    ".alert-deauth{background:#1a0000;border:2px solid #ff0000;color:#ff4444;"
    "text-shadow:0 0 10px rgba(255,0,0,0.9);"
    "animation:redpulse 0.7s ease-in-out infinite alternate}"
    ".alert-bt{background:#00001a;border:2px solid #4466ff;color:#6688ff;"
    "text-shadow:0 0 10px rgba(68,102,255,0.9);"
    "animation:bluepulse 0.7s ease-in-out infinite alternate}"
    "@keyframes redpulse{"
    "from{box-shadow:0 0 15px rgba(255,0,0,0.5),inset 0 0 10px rgba(255,0,0,0.1)}"
    "to{box-shadow:0 0 50px rgba(255,0,0,1),0 0 80px rgba(255,0,0,0.5),inset 0 0 30px rgba(255,0,0,0.2)}}"
    "@keyframes bluepulse{"
    "from{box-shadow:0 0 15px rgba(68,102,255,0.5),inset 0 0 10px rgba(68,102,255,0.1)}"
    "to{box-shadow:0 0 50px rgba(68,102,255,1),0 0 80px rgba(68,102,255,0.5),inset 0 0 30px rgba(68,102,255,0.2)}}"
    ".alert-icon{font-size:1.5rem;display:block;margin-bottom:6px}"
    ".alert-title{font-size:0.78rem;font-weight:700;margin-bottom:5px;letter-spacing:0.1em}"
    ".alert-sub{font-size:0.56rem;opacity:0.8;letter-spacing:0.04em;line-height:1.7;margin-bottom:10px}"
    ".dismiss-btn{margin-top:2px;padding:8px;font-size:0.62rem;"
    "letter-spacing:0.1em;border-radius:4px;cursor:pointer;"
    "font-family:'Share Tech Mono',monospace;text-transform:uppercase;"
    "width:100%;background:transparent}"
    ".dismiss-btn.red{border:1px solid #ff0000;color:#ff4444}"
    ".dismiss-btn.red:hover{background:rgba(255,0,0,0.15)}"
    ".dismiss-btn.blue{border:1px solid #4466ff;color:#6688ff}"
    ".dismiss-btn.blue:hover{background:rgba(68,102,255,0.15)}"
    ".footer{font-size:0.56rem;color:#0a3d0a;letter-spacing:0.06em;"
    "text-align:center;padding-top:4px;line-height:2.2}"
    ".handle{color:#1a5c1a;font-size:0.6rem}"
    ".blink{animation:blink 1s step-end infinite}"
    "@keyframes blink{50%{opacity:0}}"
    ".spd-hint{font-size:0.55rem;color:#0d500d;text-align:center;min-height:12px}"
    "</style></head><body>");

  p += F("<canvas id='mc'></canvas><div class='wrap'>");

  // Logo
  p += F("<div class='header'>"
    "<div class='logo-wrap'>"
    "<svg viewBox='0 0 100 100' fill='none' xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
    "<circle cx='50' cy='16' r='10' stroke='#9b0025' stroke-width='5' fill='none'/>"
    "<circle cx='50' cy='16' r='3' fill='#9b0025'/>"
    "<line x1='50' y1='26' x2='50' y2='50' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='50' y1='30' x2='74' y2='50' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='74' y1='50' x2='52' y2='80' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='22' y1='50' x2='62' y2='50' stroke='#9b0025' stroke-width='5' stroke-linecap='round'/>"
    "<line x1='27' y1='61' x2='57' y2='61' stroke='#7a001e' stroke-width='4.5' stroke-linecap='round'/>"
    "<line x1='34' y1='71' x2='50' y2='71' stroke='#5c0016' stroke-width='4' stroke-linecap='round'/>"
    "</svg>"
    "</div>"
    "<div class='brand'>BLACK WIRE<br>BLUE-TEAM</div>"
    "<div class='brand-sub'>// NIGHTLIGHT CONTROLLER v1.0</div>"
    "</div>"
    "<div class='scan-line'></div>");

  // Power
  p += F("<div class='card'><div class='ct'>// POWER</div>"
    "<button class='toggle-btn' id='pb' onclick='doToggle()'>[ ON / OFF ]</button>"
    "</div>");

  // Brightness
  p += F("<div class='card'><div class='ct'>// LUMINOSITY</div>"
    "<div class='sr'>"
    "<div class='sl'><span>DIM</span><span id='bv'>31%</span><span>BRIGHT</span></div>"
    "<input type='range' min='5' max='255' value='80' "
    "oninput=\"document.getElementById('bv').innerText=Math.round(this.value/255*100)+'%'\" "
    "onchange='setBr(this.value)'>"
    "</div></div>");

  // Solid color
  p += F("<div class='card'><div class='ct'>// SOLID COLOR</div>"
    "<input type='color' id='pk' value='#ff6400'>"
    "<button id='btn-solid' onclick=\"setMode('solid')\">&gt; APPLY COLOR</button>"
    "</div>");

  // Effects
  p += F("<div class='card'><div class='ct'>// SPECTRUM EFFECTS</div>"
    "<div class='g2'>"
    "<button id='btn-rainbow_wave' onclick=\"setMode('rainbow_wave')\">WAVE</button>"
    "<button id='btn-rainbow_solid' onclick=\"setMode('rainbow_solid')\">SOLID SHIFT</button>"
    "<button id='btn-rainbow_chase' onclick=\"setMode('rainbow_chase')\">CHASE</button>"
    "<button id='btn-sparkle' onclick=\"setMode('sparkle')\">SPARKLE</button>"
    "<button id='btn-meteor' onclick=\"setMode('meteor')\">METEOR</button>"
    "<button id='btn-breathe' onclick=\"setMode('breathe')\">BREATHE</button>"
    "</div></div>");

  // Color cycle
  p += F("<div class='card'><div class='ct'>// SMOOTH COLOR CYCLE</div>"
    "<button id='btn-color_cycle' onclick=\"setMode('color_cycle')\">&gt; SMOOTH COLOR CYCLE</button>"
    "</div>");

  // Speed
  p += F("<div class='card'><div class='ct'>// SPEED</div>"
    "<div class='sr'>"
    "<div class='sl'><span>&lt;&lt; AMBIENT</span><span id='sv'>MEDIUM</span><span>FAST &gt;&gt;</span></div>"
    "<input type='range' min='1' max='11' value='6' "
    "oninput='updateSpd(this.value)' onchange='setSp(this.value)'>"
    "<div class='spd-hint' id='spdHint'></div>"
    "</div></div>");

  // Alerts
  p += F("<div class='alert-box alert-deauth' id='da'>"
    "<span class='alert-icon'>&#9888;</span>"
    "<div class='alert-title'>!! DEAUTH ATTACK !!</div>"
    "<div class='alert-sub'>"
    "WiFi deauthentication packets detected<br>"
    "Jamming or forced disconnect in progress<br>"
    "Strobing RED + BLUE until attack clears"
    "</div>"
    "<button class='dismiss-btn red' onclick='dismiss()'>&#10005; DISMISS &amp; RESUME</button>"
    "</div>"
    "<div class='alert-box alert-bt' id='ba'>"
    "<span class='alert-icon'>&#9888;</span>"
    "<div class='alert-title'>!! BLUETOOTH ATTACK !!</div>"
    "<div class='alert-sub'>"
    "Anomalous BLE advertisement burst detected<br>"
    "Possible BLE spam / spoof attack nearby<br>"
    "Strobing RED + BLUE until attack clears"
    "</div>"
    "<button class='dismiss-btn blue' onclick='dismiss()'>&#10005; DISMISS &amp; RESUME</button>"
    "</div>");

  // Footer
  p += F("<div class='footer'>"
    "i love your face <span class='blink'>_</span><br>"
    "<span style='color:#071407'>&mdash; Your Pal Kal</span><br>"
    "<span class='handle'>@valleytechsolutions</span>"
    "</div></div>");

  // Script
  p += F("<script>"
    // Matrix rain
    "(function(){"
    "var c=document.getElementById('mc'),ctx=c.getContext('2d'),cols,drops;"
    "function resize(){c.width=window.innerWidth;c.height=window.innerHeight;"
    "cols=Math.floor(c.width/16);drops=[];"
    "for(var i=0;i<cols;i++)drops[i]=Math.random()*-(c.height/16);}"
    "resize();window.addEventListener('resize',resize);"
    "var ch='01アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホ';"
    "function draw(){"
    "ctx.fillStyle='rgba(0,0,0,0.05)';ctx.fillRect(0,0,c.width,c.height);"
    "ctx.font='14px monospace';"
    "for(var i=0;i<drops.length;i++){"
    "var r=Math.random();"
    "ctx.fillStyle=r>0.95?'#ffffff':r>0.7?'#00ff41':'#007a20';"
    "ctx.fillText(ch[Math.floor(Math.random()*ch.length)],i*16,drops[i]*16);"
    "if(drops[i]*16>c.height&&Math.random()>0.975)drops[i]=0;"
    "drops[i]+=0.5;}}"
    "setInterval(draw,50);})();"

    // Controls
    "var isOn=true;"
    "var labels=['','AMBIENT','VERY SLOW','SLOW','RELAXED','MEDIUM-SLOW',"
    "'MEDIUM','MEDIUM-FAST','FAST','VERY FAST','MAX','AMBIENT'];"
    "var hints=['','ultra smooth ambient','','','','','','','','','turbo','ultra smooth ambient'];"
    "function updateSpd(v){"
    "document.getElementById('sv').innerText=labels[v]||v;"
    "document.getElementById('spdHint').innerText=hints[v]||'';}"
    "function doToggle(){"
    "fetch('/toggle').then(r=>r.text()).then(s=>{"
    "isOn=s==='on';"
    "var b=document.getElementById('pb');"
    "b.className=isOn?'toggle-btn':'toggle-btn off';})}"
    "function setMode(m){"
    "var q='?mode='+m;"
    "if(m==='solid'){"
    "var h=document.getElementById('pk').value;"
    "q+='&r='+parseInt(h.slice(1,3),16)"
    "+'&g='+parseInt(h.slice(3,5),16)"
    "+'&b='+parseInt(h.slice(5,7),16);}"
    "fetch('/mode'+q).then(()=>{"
    "document.querySelectorAll('button[id^=\"btn-\"]')"
    ".forEach(b=>b.classList.remove('active'));"
    "var a=document.getElementById('btn-'+m);"
    "if(a)a.classList.add('active');})}"
    "function setSp(v){fetch('/speed?v='+v)}"
    "function setBr(v){fetch('/brightness?v='+v)}"
    "function dismiss(){"
    "fetch('/dismiss').then(()=>{"
    "document.getElementById('da').style.display='none';"
    "document.getElementById('ba').style.display='none';})}"
    "function poll(){"
    "fetch('/alerts').then(r=>r.json()).then(d=>{"
    "if(!d.dismissed){"
    "document.getElementById('da').style.display=d.deauth?'block':'none';"
    "document.getElementById('ba').style.display=d.bt?'block':'none';}})}"
    "setInterval(poll,2000);poll();"
    "</script></body></html>");

  server.send(200, "text/html", p);
}

// ── Route handlers ───────────────────────────────────────────
void handleToggle() {
  ledsOn = !ledsOn;
  if (!ledsOn) { FastLED.clear(); FastLED.show(); }
  server.send(200, "text/plain", ledsOn ? "on" : "off");
}

void handleMode() {
  String m = server.arg("mode");
  ledsOn      = true;
  strobing    = false;
  alertDismissed = false;  // new mode clears dismiss so future attacks still trigger
  if      (m=="solid")         { mode=0; currentR=server.arg("r").toInt(); currentG=server.arg("g").toInt(); currentB=server.arg("b").toInt(); }
  else if (m=="rainbow_wave")  mode=1;
  else if (m=="rainbow_solid") mode=2;
  else if (m=="rainbow_chase") mode=3;
  else if (m=="color_cycle")   mode=4;
  else if (m=="sparkle")       mode=5;
  else if (m=="meteor")        mode=6;
  else if (m=="breathe")       mode=7;
  server.send(200, "text/plain", "ok");
}

void handleSpeed() {
  spd = spdToDelay(server.arg("v").toInt());
  server.send(200, "text/plain", "ok");
}

void handleBrightness() {
  brightness = server.arg("v").toInt();
  FastLED.setBrightness(brightness);
  server.send(200, "text/plain", "ok");
}

void handleAlerts() {
  String j = "{\"deauth\":";
  j += (deauthAlert && !alertDismissed) ? "true" : "false";
  j += ",\"bt\":";
  j += (btAlert && !alertDismissed) ? "true" : "false";
  j += ",\"dismissed\":";
  j += alertDismissed ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

void handleDismiss() {
  alertDismissed = true;
  strobing       = false;
  // Don't clear deauthAlert/btAlert — they reflect real state.
  // alertDismissed suppresses strobe + UI until attack actually stops.
  server.send(200, "text/plain", "ok");
}

// ── Lighting modes ───────────────────────────────────────────
void runMode() {
  switch (mode) {
    case 0:
      for (int i=0;i<NUM_LEDS;i++) leds[i]=CRGB(currentR,currentG,currentB);
      break;
    case 1:
      for (int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(hue+(i*10),255,255);
      hue++;
      break;
    case 2:
      for (int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(hue,255,255);
      hue++;
      break;
    case 3:
      FastLED.clear();
      for (int t=0;t<5;t++) {
        int pos=(meteorPos+t)%NUM_LEDS;
        leds[pos]=CHSV(hue+(t*20),255,255-t*40);
      }
      meteorPos=(meteorPos+1)%NUM_LEDS;
      hue++;
      break;
    case 4: // Smooth color cycle — float hue increments every tick
      cycleHue += 0.15f;
      if (cycleHue >= 256.0f) cycleHue -= 256.0f;
      {
        uint8_t h = (uint8_t)cycleHue;
        for (int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(h,255,255);
      }
      break;
    case 5:
      fadeToBlackBy(leds,NUM_LEDS,20);
      for (int s=0;s<3;s++)
        leds[random(NUM_LEDS)]=CHSV(sparkleHue+random(30),200,255);
      sparkleHue+=2;
      break;
    case 6:
      fadeToBlackBy(leds,NUM_LEDS,40);
      for (int t=0;t<8;t++) {
        int pos=(meteorPos+NUM_LEDS-t)%NUM_LEDS;
        leds[pos]=CHSV(hue,255,max(0,255-t*28));
      }
      meteorPos=(meteorPos+1)%NUM_LEDS;
      hue+=3;
      break;
    case 7:
      breatheVal += 0.025f * breatheDir;
      if (breatheVal>=1.0f){breatheVal=1.0f;breatheDir=-1.0f;}
      if (breatheVal<=0.0f){breatheVal=0.0f;breatheDir=1.0f;hue+=32;}
      {
        uint8_t b=(uint8_t)(breatheVal*breatheVal*255.0f);
        for (int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(hue,255,b);
      }
      break;
  }
}

void runStrobe() {
  unsigned long now=millis();
  if (now-lastStrobe<100) return;
  lastStrobe=now;
  strobeStep++;
  bool red=(strobeStep%2==0);
  for (int i=0;i<NUM_LEDS;i++) leds[i]=red?CRGB(255,0,0):CRGB(0,0,255);
  FastLED.show();
}

// ── Attack detection ─────────────────────────────────────────
void checkDeauth() {
  unsigned long now=millis();
  if (now-deauthWindow < 3000) return;
  deauthWindow=now;

  if (deauthCount >= 5) {
    // Attack ongoing
    deauthAlert        = true;
    deauthMissedWindows = 0;
    if (!alertDismissed) strobing=true;
  } else {
    // No packets this window
    if (deauthAlert) {
      deauthMissedWindows++;
      // Require 2 consecutive clean windows before clearing
      if (deauthMissedWindows >= 2) {
        deauthAlert         = false;
        deauthMissedWindows = 0;
        alertDismissed      = false; // reset dismiss so next attack shows
        if (!btAlert) strobing=false;
      }
    }
  }
  deauthCount=0;
}

void checkBLE() {
  if (!pBLEScan) return;
  unsigned long now=millis();
  if (now-lastBLEScan < 4000) return;
  lastBLEScan=now;

  bleMacsThisWindow.clear();
  BLEScanResults* results=pBLEScan->start(1,false);
  if (results) {
    int count=results->getCount();
    for (int i=0;i<count;i++) {
      BLEAdvertisedDevice d=results->getDevice(i);
      bleMacsThisWindow.insert(std::string(d.getAddress().toString().c_str()));
    }
    pBLEScan->clearResults();
  }

  int seen=(int)bleMacsThisWindow.size();
  if (seen >= 15) {
    // Attack ongoing
    btAlert         = true;
    bleMissedWindows = 0;
    if (!alertDismissed) strobing=true;
  } else if (btAlert) {
    bleMissedWindows++;
    if (bleMissedWindows >= 2) {
      btAlert          = false;
      bleMissedWindows = 0;
      alertDismissed   = false;
      if (!deauthAlert) strobing=false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
  deauthWindow=millis();

  BLEDevice::init("");
  pBLEScan=BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyBLECallbacks());
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(50);
  pBLEScan->setWindow(40);
  lastBLEScan=millis();

  server.on("/",           handleRoot);
  server.on("/toggle",     handleToggle);
  server.on("/mode",       handleMode);
  server.on("/speed",      handleSpeed);
  server.on("/brightness", handleBrightness);
  server.on("/alerts",     handleAlerts);
  server.on("/dismiss",    handleDismiss);
  server.begin();

  Serial.println("Ready — connect to xiaoNightlight then go to 192.168.4.1");
}

void loop() {
  server.handleClient();
  checkDeauth();
  checkBLE();

  // Strobe only if actively attacking AND not dismissed
  if (strobing && !alertDismissed) {
    runStrobe();
    return;
  }

  if (!ledsOn) return;

  unsigned long now=millis();
  if (now-lastUpdate < (unsigned long)spd) return;
  lastUpdate=now;

  runMode();
  FastLED.show();
}