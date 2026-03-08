#include <WiFi.h>

/** * MBEDTLS Compatibility Patch for ESP32 Core 3.x
 * This fixes the 'mbedtls_md5_starts_ret' errors in ESPAsyncWebServer
 */
#define mbedtls_md5_starts_ret mbedtls_md5_starts
#define mbedtls_md5_update_ret mbedtls_md5_update
#define mbedtls_md5_finish_ret mbedtls_md5_finish

#include <ESPAsyncWebServer.h> // Requires ESPAsyncWebServer and AsyncTCP libraries
#include <Preferences.h>

/**
 * Observatory Unified Power & Battery Monitor + Web Dashboard
 * -----------------------------------------------------------
 * v3.8 - TIMER UNDERFLOW & ASCOM CONFORM FIX
 * - Fixed the 4.29 million seconds timer underflow bug
 * - Fixed Conform negative ID tests by explicitly returning 0 for negative inputs
 * - Maintained Multi-threaded architecture for N.I.N.A. stability
 */

// --- NETWORK CONFIGURATION ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* device_hostname = "Observatory-UPS";
const int ALPACA_PORT = 8080;          

// --- PINS ---
const int GRID_PIN = 14;      
const int BATTERY_PIN = 19;   

// --- SETTINGS ---
unsigned long grace_period_ms = 600000; 
int alpaca_device_id = 0;               
const float VOLTAGE_DIVIDER_RATIO = 5.0;      
const float ADC_REF_VOLTAGE = 3.3;            
float voltage_offset = 0.0;
bool enable_low_batt_cutoff = false;
float low_batt_threshold = 11.0;

// --- STATE ---
bool gridPowerDetected = true;
bool deviceConnected = false;           
unsigned long powerLossStartTime = 0;
uint32_t transactionID = 0;

// Async Server instance
AsyncWebServer server(ALPACA_PORT);
Preferences preferences;

// --- 1. CORE UTILITIES ---

float getBatteryVoltage() {
  return ((analogRead(BATTERY_PIN) / 4095.0) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO) + voltage_offset;
}

bool isSystemSafe() {
  // Low battery cutoff check overrides the timer if enabled
  if (enable_low_batt_cutoff && getBatteryVoltage() < low_batt_threshold) {
    return false; 
  }

  bool gridIsUp = (digitalRead(GRID_PIN) == HIGH);
  if (gridIsUp) {
    gridPowerDetected = true;
    powerLossStartTime = 0;
    return true; 
  } else {
    if (gridPowerDetected) {
      gridPowerDetected = false;
      powerLossStartTime = millis();
    }
    return (millis() - powerLossStartTime < grace_period_ms);
  }
}

// Robust parameter search for AsyncWebServer (Checks both Query and Post Body)
String getParamRobust(AsyncWebServerRequest *request, String name) {
  if (request->hasParam(name)) return request->getParam(name)->value();
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  
  int params = request->params();
  for(int i=0; i<params; i++){
    const AsyncWebParameter* p = request->getParam(i);
    if (p->name().equalsIgnoreCase(name)) return p->value();
  }
  return "";
}

uint32_t getClientID(AsyncWebServerRequest *request) {
  String cid = getParamRobust(request, "ClientTransactionID");
  if (cid.length() > 0) {
    // If the tool sends a negative number, Conform expects us to treat it as invalid and return 0
    if (cid.indexOf('-') >= 0) {
      return 0;
    }
    return (uint32_t)atoll(cid.c_str());
  }
  return 0;
}

// --- 2. ALPACA RESPONSES ---

void sendAlpacaResponse(AsyncWebServerRequest *request, String value, int errorNum = 0, String errorMsg = "", int httpStatus = 200) {
  uint32_t cID = getClientID(request);
  String val = (value.length() > 0) ? value : "null";
  
  char buf[512];
  snprintf(buf, sizeof(buf), 
    "{\"ClientTransactionID\":%u,\"ServerTransactionID\":%u,\"Value\":%s,\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\"}",
    cID, transactionID++, val.c_str(), errorNum, errorMsg.c_str());
  
  AsyncWebServerResponse *response = request->beginResponse(httpStatus, "application/json", buf);
  response->addHeader("Connection", "close");
  request->send(response);
}

// --- 3. WEB DASHBOARD ---

const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>
<style>
body{font-family:sans-serif; background:#0a0a0a; color:#eee; text-align:center; padding:15px;}
.card{background:#161616; border-radius:10px; padding:15px; margin:10px auto; max-width:350px; border:1px solid #333;}
.val{font-size:2.2em; font-weight:bold; color:#00e676;}
.status{padding:8px; border-radius:15px; font-weight:bold; display:block; margin-top:5px;}
.safe{background:#1b5e20;} .unsafe{background:#b71c1c;} .warn{background:#e65100;}
input{background:#222; color:#fff; border:1px solid #444; padding:6px; border-radius:4px; width:50px;}
button{background:#00e676; border:none; padding:10px; border-radius:5px; cursor:pointer; font-weight:bold; color:#000;}
</style>
<script>
function up(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('v').innerText = d.v + 'V';
    document.getElementById('g').innerText = d.g ? 'GRID OK' : 'GRID DOWN';
    document.getElementById('g').className = 'status ' + (d.g ? 'safe' : 'warn');
    document.getElementById('s').innerText = d.s ? 'SAFE' : 'UNSAFE';
    document.getElementById('s').className = 'status ' + (d.s ? 'safe' : 'unsafe');
    document.getElementById('r').innerText = d.r;
  }).catch(e=>console.log("Telemetry lost"));
}
setInterval(up, 2500);
</script>
</head><body onload='up()'>
<h2>Observatory Power (Async)</h2>
<div class='card'>Safety: <span id='s' class='status'>--</span></div>
<div class='card'>Battery: <div id='v' class='val'>--V</div></div>
<div class='card'>Grid: <span id='g' class='status'>--</span><p id='r' style='color:#ff5252'></p></div>
<div class='card'><form action='/update' method='POST'>
Min: <input type='number' name='grace' value='%GRACE%'> ID: <input type='number' name='deviceid' value='%ID%'><br><br>
Offset (V): <input type='number' step='0.01' name='offset' value='%OFF%'><br><br>
Cutoff: <select name='batt_en'><option value='0'>Off</option><option value='1' %C_SEL%>On</option></select>
at <input type='number' step='0.1' name='batt_th' value='%B_TH%' style='width:60px'>V<br><br>
<button type='submit'>Save & Restart</button></form></div>
<p style='font-size:0.7em; color:#444;'>Async Alpaca v3.9</p>
</body></html>
)=====";

// --- 4. SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  pinMode(GRID_PIN, INPUT_PULLDOWN);
  analogReadResolution(12);

  preferences.begin("power-mon", true);
  grace_period_ms = (unsigned long)preferences.getUInt("grace_min", 10) * 60000;
  alpaca_device_id = preferences.getInt("device_id", 0);
  voltage_offset = preferences.getFloat("v_offset", 0.0);
  low_batt_threshold = preferences.getFloat("batt_th", 11.0);
  enable_low_batt_cutoff = preferences.getBool("batt_en", false);
  preferences.end();

  WiFi.setHostname(device_hostname);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  WiFi.setSleep(false);

  // Define static paths for Alpaca
  static String base = "/api/v1/safetymonitor/" + String(alpaca_device_id) + "/";
  
  // --- ALPACA ASYNC HANDLERS ---
  
  server.on((base + "issafe").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){
    sendAlpacaResponse(request, isSystemSafe() ? "true" : "false");
  });

  server.on((base + "connected").c_str(), HTTP_ANY, [](AsyncWebServerRequest *request){
    if (request->method() == HTTP_PUT) {
      String c = getParamRobust(request, "Connected");
      if (c.equalsIgnoreCase("true") || c.equalsIgnoreCase("false")) { 
        deviceConnected = c.equalsIgnoreCase("true"); 
        sendAlpacaResponse(request, ""); 
      } else {
        sendAlpacaResponse(request, "", 1025, "Invalid boolean", 400); 
      }
    } else {
      sendAlpacaResponse(request, deviceConnected ? "true" : "false");
    }
  });

  server.on((base + "voltage").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){
    sendAlpacaResponse(request, String(getBatteryVoltage(), 2));
  });

  server.on((base + "interfaceversion").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "1"); });
  server.on((base + "name").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "\"Pecron Monitor\""); });
  server.on((base + "description").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "\"UPS Safety Monitor\""); });
  server.on((base + "driverinfo").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "\"ESP32 Async Alpaca v3.8\""); });
  server.on((base + "driverversion").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "\"3.8\""); });
  server.on((base + "supportedactions").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "[]"); });
  server.on((base + "setupdialog").c_str(), HTTP_GET, [](AsyncWebServerRequest *request){ sendAlpacaResponse(request, "false"); });
  
  // MANAGEMENT API
  server.on("/management/v1/configureddevices", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "[{\"DeviceName\":\"Power Center\",\"DeviceType\":\"SafetyMonitor\",\"DeviceNumber\":" + String(alpaca_device_id) + ",\"UniqueID\":\"p-001\"}]");
  });

  server.on("/management/apiversions", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "{\"Value\":[1],\"ClientTransactionID\":0,\"ServerTransactionID\":0,\"ErrorNumber\":0,\"ErrorMessage\":\"\"}");
  });

  // --- UI HANDLERS ---
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String h = String(INDEX_HTML);
    h.replace("%GRACE%", String(grace_period_ms / 60000));
    h.replace("%ID%", String(alpaca_device_id));
    h.replace("%OFF%", String(voltage_offset, 2));
    h.replace("%B_TH%", String(low_batt_threshold, 1));
    h.replace("%C_SEL%", enable_low_batt_cutoff ? "selected" : "");
    request->send(200, "text/html", h);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    bool g = (digitalRead(GRID_PIN) == HIGH);
    char timerBuf[32] = "";
    
    // SAFE TIMEOUT CALCULATION (Prevents Underflow)
    if (!g && powerLossStartTime > 0) {
      unsigned long elapsed = millis() - powerLossStartTime;
      unsigned long rem = 0;
      if (elapsed < grace_period_ms) {
        rem = (grace_period_ms - elapsed) / 1000;
      }
      snprintf(timerBuf, sizeof(timerBuf), "Timer: %lus", rem);
    }
    
    char jsonBuf[192];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"v\":%.2f,\"g\":%s,\"s\":%s,\"r\":\"%s\"}", 
             getBatteryVoltage(), g?"true":"false", isSystemSafe()?"true":"false", timerBuf);
    request->send(200, "application/json", jsonBuf);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    preferences.begin("power-mon", false);
    for(int i=0; i<params; i++){
      const AsyncWebParameter* p = request->getParam(i);
      if(p->name() == "grace") preferences.putUInt("grace_min", p->value().toInt());
      if(p->name() == "deviceid") preferences.putInt("device_id", p->value().toInt());
      if(p->name() == "offset") preferences.putFloat("v_offset", p->value().toFloat());
      if(p->name() == "batt_th") preferences.putFloat("batt_th", p->value().toFloat());
      if(p->name() == "batt_en") preferences.putBool("batt_en", p->value().toInt() == 1);
    }
    preferences.end();
    request->send(200, "text/plain", "Done. Restarting...");
    delay(500);
    ESP.restart();
  });

  server.begin();
  Serial.printf("\nAsync v3.8 Ready at: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
}