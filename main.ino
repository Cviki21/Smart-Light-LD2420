// PAMETNO OSVJETLJENJE - FIRMWARE v2.5
// Autor: Dejan Habijanec & Gemini AI
// Datum: 24.10.2025.
// Napomene: koristi FastLED, WebSocketsServer (arduinoWebSockets), ArduinoJson

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// --- Watchdog config ---
const esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 10000,
  .idle_core_mask = 0,
  .trigger_panic = true
};

// --- Pins (provjeri svoje pinove) ---
#define RX_PIN_B 10
#define TX_PIN_B 11
#define LED_PIN_A 1
#define LED_PIN_B 2
#define TASTER_PIN 7

// --- Effect Tuning Constants ---
#define FIRE_COOLING 55
#define FIRE_SPARKING 80
#define FIRE_SPARK_HEIGHT 7
#define FIRE_HEAT_MIN 160
#define FIRE_HEAT_MAX 255
#define METEOR_FADE_RATE 128 // 0-255, higher is slower fade (e.g. 255 = no fade, 128 = 50% fade)

HardwareSerial SensorSerialB(1);

// --- LEDs ---
#define MAX_LEDS 3000
// Inicijalizacija s 0 LED-ica, duljina se postavlja kasnije iz postavki
Adafruit_NeoPixel stripA(0, LED_PIN_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(0, LED_PIN_B, NEO_GRB + NEO_KHZ800);

// --- Settings struct ---
struct StripSettings {
  bool enabled = false;
  String type = "ws2812b";
  int length = 4;        // u "metarima" ili jedinicama, ovisi kako mjeriš
  int density = 60;      // leds per unit length
  int numLeds = 240;
  int offset = 0;
  String mode = "fill";
  int followWidth = 20;
  int wipeSpeed = 50;    // ms step
  int onTime = 5;        // seconds
  String effect = "solid";
  String colorHex = "#0000FF";
  uint32_t color = 255; // NeoPixel koristi 32-bitni integer za boju (plava)
  int colorTemp = 4000;
  uint8_t brightness = 80;
};
StripSettings strip1_settings;
StripSettings strip2_settings;
String mainMode = "auto"; // auto, on, off

// --- Runtime variables ---
String lineBufferB = "";
int currentDistanceB = 0;
float smoothedDistanceB = 0.0f;
unsigned long lastMotionTimeB = 0;
uint16_t rainbowFirstPixelHue = 0; // For rainbow effect animation
unsigned long meteorLastUpdate = 0;
int meteorPos = 0;
byte heat[MAX_LEDS];

// --- WiFi AP & Taster variables ---
bool isApOn = true;
unsigned long lastClientConnectedTime = 0;
unsigned long buttonPressTime = 0;
bool longPressHandled = false;
int lastButtonState = HIGH;

// Manual override variables
bool manualOverride = false;
bool led1_on = false;
bool led2_on = false;

// Web / WiFi
const char* ssid = "Pametno_Svjetlo_Setup";
WebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket = WebSocketsServer(81); // Port 81 for browser ws://host:81/

// Helper forward declarations
uint32_t parseHtmlColor(String colorString);
String rgbToHex(uint32_t color);
void calculateNumLeds(StripSettings &settings);
void saveConfiguration();
void loadConfiguration();
void signalAPActive();
bool handleFileRead(String path);
void handleGet();
void handleSave();
void sendStateToClients();
void handleWebSocketMessage(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length);
void applyEffectsAndUpdate();
uint32_t wheel(byte wheelPos);
void effect_rainbow(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed);
void effect_meteor(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed);
void effect_fire(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed);
void signalModeChange(uint32_t color);
void handleButton();
void handleAP();

// --- Implementations ---
uint32_t parseHtmlColor(String colorString) {
  if (colorString.startsWith("#")) colorString = colorString.substring(1);
  long number = strtol(colorString.c_str(), NULL, 16);
  uint8_t r = (number >> 16) & 0xFF;
  uint8_t g = (number >> 8) & 0xFF;
  uint8_t b = number & 0xFF;
  // Vraća boju kao 32-bitni integer koji NeoPixel koristi
  return Adafruit_NeoPixel::Color(r, g, b);
}

String rgbToHex(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  char hexColor[8];
  sprintf(hexColor, "%02X%02X%02X", r, g, b);
  return String(hexColor);
}

void calculateNumLeds(StripSettings &settings) {
  long n = (long)settings.length * (long)settings.density;
  if (n > MAX_LEDS) {
    settings.numLeds = MAX_LEDS;
    Serial.printf("[WARN] Izračun prevelik, ograničeno na %d\n", MAX_LEDS);
  } else if (n <= 0) {
    settings.numLeds = 1;
    Serial.println("[WARN] Izračun <=0, postavljeno na 1");
  } else {
    settings.numLeds = (int)n;
  }
}

// Save/load using ArduinoJson dynamic doc
void saveConfiguration() {
  Serial.println("[CONFIG] Spremam konfiguraciju...");
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) { Serial.println("[ERROR] Ne mogu otvoriti config.json za pisanje"); return; }
  DynamicJsonDocument doc(2048);
  doc["mainMode"] = mainMode;
  doc["manualOverride"] = manualOverride;
  doc["stripCount"] = (strip1_settings.enabled?1:0) + (strip2_settings.enabled?1:0);

  auto saveStrip = [&](JsonObject obj, const StripSettings& s){
    obj["type"]=s.type; obj["length"]=s.length; obj["density"]=s.density;
    obj["offset"]=s.offset; obj["mode"]=s.mode; obj["followWidth"]=s.followWidth;
    obj["wipeSpeed"]=s.wipeSpeed; obj["onTime"]=s.onTime; obj["effect"]=s.effect;
    obj["color"]=s.colorHex; obj["temp"]=s.colorTemp; obj["brightness"]=s.brightness;
  };
  if(strip1_settings.enabled) { JsonObject s1 = doc.createNestedObject("strip1"); saveStrip(s1, strip1_settings); }
  if(strip2_settings.enabled) { JsonObject s2 = doc.createNestedObject("strip2"); saveStrip(s2, strip2_settings); }

  if (serializeJson(doc, configFile) == 0) Serial.println("[ERROR] Neuspjelo pisanje config.json");
  else Serial.println("[CONFIG] Spremljeno");
  configFile.close();
}

void loadConfiguration() {
  if (!SPIFFS.begin(true)) { Serial.println("[ERROR] SPIFFS mount failed"); return; }
  Serial.println("[SYSTEM] SPIFFS montiran.");
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json","r");
    if (!configFile) { Serial.println("[WARN] config.json ne moze otvoriti za citanje"); return; }
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, configFile);
    configFile.close();
    if (err) { Serial.print("[ERROR] JSON parse: "); Serial.println(err.c_str()); return; }
    mainMode = doc["mainMode"] | "auto";
    manualOverride = doc["manualOverride"] | false;
    int stripCount = doc["stripCount"] | 0;
    auto loadStrip = [&](JsonObjectConst obj, StripSettings &s){
      s.enabled = true;
      s.type = obj["type"] | s.type;
      s.length = obj["length"] | s.length;
      s.density = obj["density"] | s.density;
      s.offset = obj["offset"] | s.offset;
      s.mode = obj["mode"] | s.mode;
      s.followWidth = obj["followWidth"] | s.followWidth;
      s.wipeSpeed = obj["wipeSpeed"] | s.wipeSpeed;
      s.onTime = obj["onTime"] | s.onTime;
      s.effect = obj["effect"] | s.effect;
      s.colorHex = obj["color"] | s.colorHex;
      s.color = parseHtmlColor(s.colorHex);
      s.colorTemp = obj["temp"] | s.colorTemp;
      s.brightness = obj["brightness"] | s.brightness;
      calculateNumLeds(s);
    };
    if (doc.containsKey("strip1")) loadStrip(doc["strip1"].as<JsonObjectConst>(), strip1_settings); else strip1_settings.enabled=false;
    if (doc.containsKey("strip2")) loadStrip(doc["strip2"].as<JsonObjectConst>(), strip2_settings); else strip2_settings.enabled=false;

    uint8_t b = 80;
    if (strip1_settings.enabled) b = strip1_settings.brightness;
    else if (strip2_settings.enabled) b = strip2_settings.brightness;
    stripA.setBrightness(b);
    stripB.setBrightness(b);
    Serial.printf("[LED] Brightness set to %d\n", b);
    Serial.println("[CONFIG] Ucitavanje konfiguracije gotovo.");
  } else {
    Serial.println("[CONFIG] config.json ne postoji; kreiram default.");
    strip1_settings.enabled = true;
    strip2_settings.enabled = false;
    calculateNumLeds(strip1_settings);
    calculateNumLeds(strip2_settings);
    saveConfiguration();
  }
}

void signalAPActive() {
  Serial.println("[WIFI] Signaliziram AP (treptanje)...");
  Adafruit_NeoPixel* stripToShow = nullptr;
  if (strip1_settings.enabled) {
    stripToShow = &stripB;
  } else if (strip2_settings.enabled) {
    stripToShow = &stripA;
  }

  if (stripToShow == nullptr || stripToShow->numPixels() == 0) {
    Serial.println("[WIFI] Nema LED traka za signal");
    return;
  }

  uint8_t cur = stripToShow->getBrightness();
  stripToShow->setBrightness(50);
  
  int num_to_blink = min(10, (int)stripToShow->numPixels());

  for (int k=0; k<3; k++) {
        esp_task_wdt_reset(); // Resetiraj watchdog
        for (int i=0; i<num_to_blink; i++) {
          stripToShow->setPixelColor(i, Adafruit_NeoPixel::Color(255, 255, 255));
        }
        stripToShow->show();
        delay(200);
        
        stripToShow->clear();
        stripToShow->show();
        delay(200);
      }
  
  stripToShow->setBrightness(cur);
}

bool handleFileRead(String path) {
  if (!isApOn) return false; // Ne poslužuj datoteke ako je AP ugašen
  Serial.print("[WEB] Trazi: "); Serial.println(path);
  if (path.endsWith("/")) path = "/index.html";
  String contentType = "text/html";
  if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".jpg")) contentType = "image/jpeg";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  if (SPIFFS.exists(path) || SPIFFS.exists(path + ".gz")) {
    if (SPIFFS.exists(path + ".gz")) path += ".gz";
    File file = SPIFFS.open(path, "r");
    server.sendHeader("Cache-Control","max-age=86400");
    if (path.endsWith(".gz")) server.sendHeader("Content-Encoding", "gzip");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleGet() {
  lastClientConnectedTime = millis(); // Resetiraj timer na bilo koji web zahtjev
  Serial.println("[WEB] /get");
  DynamicJsonDocument doc(1024);
  doc["mainMode"] = mainMode;
  int stripCount = (strip1_settings.enabled?1:0) + (strip2_settings.enabled?1:0);
  doc["stripCount"] = stripCount;
  if (strip1_settings.enabled) {
    JsonObject s1 = doc.createNestedObject("strip1");
    s1["type"]=strip1_settings.type; s1["length"]=strip1_settings.length; s1["density"]=strip1_settings.density;
    s1["offset"]=strip1_settings.offset; s1["mode"]=strip1_settings.mode; s1["effect"]=strip1_settings.effect;
    s1["color"]=strip1_settings.colorHex; s1["brightness"]=strip1_settings.brightness;
  }
  if (strip2_settings.enabled) {
    JsonObject s2 = doc.createNestedObject("strip2");
    s2["type"]=strip2_settings.type; s2["length"]=strip2_settings.length; s2["density"]=strip2_settings.density;
    s2["offset"]=strip2_settings.offset; s2["mode"]=strip2_settings.mode; s2["effect"]=strip2_settings.effect;
    s2["color"]=strip2_settings.colorHex; s2["brightness"]=strip2_settings.brightness;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSave() {
  lastClientConnectedTime = millis(); // Resetiraj timer na bilo koji web zahtjev
  // Privremeno postavi na max duljinu da se sigurno sve ugasi
  stripA.updateLength(MAX_LEDS);
  stripB.updateLength(MAX_LEDS);
  stripA.clear();
  stripB.clear();
  stripA.show();
  stripB.show();

  Serial.println("[WEB] /save");
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request"); return; }
  String body = server.arg("plain");
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);
  if (err) { Serial.print("[ERROR] serializeJson: "); Serial.println(err.c_str()); server.send(400,"text/plain","Invalid JSON"); return; }
  
  mainMode = doc["mainMode"] | mainMode;
  int stripCount = doc["stripCount"] | 0;

  auto parseStrip = [&](JsonObjectConst obj, StripSettings &s){
    s.enabled = true;
    s.type = obj["type"] | s.type; s.length = obj["length"] | s.length; s.density = obj["density"] | s.density; 
    s.offset = obj["offset"] | s.offset; s.mode = obj["mode"] | s.mode; s.followWidth = obj["followWidth"] | s.followWidth; s.effect = obj["effect"] | s.effect;
    s.colorHex = obj["color"] | s.colorHex; s.color = parseHtmlColor(s.colorHex);
    s.brightness = obj["brightness"] | s.brightness; 
    Serial.printf("[CONFIG] strip1_settings.followWidth nakon parsiranja JSON-a iz web sucelja: %d\n", s.followWidth);

    calculateNumLeds(s);
  };

  if (stripCount >= 1 && doc.containsKey("strip1")) { 
    parseStrip(doc["strip1"], strip1_settings); 
  } else {
    strip1_settings.enabled = false;
  }
  if (stripCount == 2 && doc.containsKey("strip2")) { 
    parseStrip(doc["strip2"], strip2_settings); 
  }  else {
    strip2_settings.enabled = false;
  }

  // Apply new lengths
  stripB.updateLength(strip1_settings.enabled ? strip1_settings.numLeds : 0);
  stripA.updateLength(strip2_settings.enabled ? strip2_settings.numLeds : 0);
  Serial.printf("[LED] Duljina trake B: %d, A: %d\n", stripB.numPixels(), stripA.numPixels());

  // Apply new brightness
  uint8_t b = 80;
  if (strip1_settings.enabled) b = strip1_settings.brightness;
  else if (strip2_settings.enabled) b = strip2_settings.brightness;
  stripA.setBrightness(b);
  stripB.setBrightness(b);
  Serial.printf("[CONFIG] strip1_settings.followWidth prije spremanja: %d\n", strip1_settings.followWidth);
  Serial.printf("[LED] Svjetlina postavljena na %d\n", b);

  saveConfiguration();
  server.send(200,"text/plain","OK");
}

// --- WebSocket send state to all clients ---
void sendStateToClients() {
  if (!isApOn) return;
  DynamicJsonDocument doc(256);
  // presence booleans: for simplicity presence = recent motion within onTime for strip1
  bool presence1 = (millis() - lastMotionTimeB) < (unsigned long)(strip1_settings.onTime * 1000UL);
  bool presence2 = false; // placeholder, nema senzora A u ovom firmwareu (možeš dodati)
  doc["presence1"] = presence1;
  doc["presence2"] = presence2;
  doc["manualOverride"] = manualOverride;
  doc["led1_status"] = led1_on;
  doc["led2_status"] = led2_on;
  String out; serializeJson(doc, out);
  webSocket.broadcastTXT(out);
}

void handleWebSocketMessage(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  lastClientConnectedTime = millis(); // Resetiraj timer na bilo koji web zahtjev
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    // parsiranje jednostavnog JSON-a ili plain command
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, msg);
    String cmd = "";
    if (!err && doc.containsKey("command")) cmd = String((const char*)doc["command"]);
    else cmd = msg;
    Serial.printf("[WS] Client %d command: %s\n", clientNum, cmd.c_str());
    if (cmd == "toggle_manual") {
      manualOverride = !manualOverride;
      if (!manualOverride) {
        // exit manual: reset manual flags to false to let auto control manage LEDs
        led1_on = false; led2_on = false;
      }
    } else if (cmd == "toggle_led1") {
      if (manualOverride) led1_on = !led1_on;
      else Serial.println("[WS] Ignored toggle_led1 - not in manual mode");
    } else if (cmd == "toggle_led2") {
      if (manualOverride) led2_on = !led2_on;
      else Serial.println("[WS] Ignored toggle_led2 - not in manual mode");
    } else {
      Serial.println("[WS] Nepoznata komanda");
    }
    // odmah pošalji stanje natrag
    sendStateToClients();
  }
}

void signalModeChange(uint32_t color) {
  Adafruit_NeoPixel* stripToShow = nullptr;
  if (strip1_settings.enabled) stripToShow = &stripB;
  else if (strip2_settings.enabled) stripToShow = &stripA;

  if (stripToShow == nullptr || stripToShow->numPixels() == 0) return;

  uint8_t oldBrightness = stripToShow->getBrightness();
  stripToShow->setBrightness(80);
  
  int num_to_blink = min(5, (int)stripToShow->numPixels());

  // Turn off strip before blinking to make it noticeable
  stripToShow->clear();
  stripToShow->show();
  delay(50);

  for (int k = 0; k < 2; k++) {
    esp_task_wdt_reset(); // Reset watchdog during blocking blinks
    for (int i = 0; i < num_to_blink; i++) {
      stripToShow->setPixelColor(i, color);
    }
    stripToShow->show();
    delay(150);
    stripToShow->clear();
    stripToShow->show();
    delay(150);
  }
  
  stripToShow->setBrightness(oldBrightness);
  // The main loop will repaint the strip with the correct state afterwards
}

void handleButton() {
    int reading = digitalRead(TASTER_PIN);

    if (reading == LOW) { // Button is held down
        if (lastButtonState == HIGH) { // Just pressed
            buttonPressTime = millis();
            longPressHandled = false;
        }
        
        // Check for long press (6 seconds)
        if (!longPressHandled && (millis() - buttonPressTime > 6000)) {
            Serial.println("[BUTTON] Dug pritisak detektiran.");
            if (!isApOn) {
                Serial.println("[WIFI] Palim AP...");
                WiFi.softAP(ssid);
                dnsServer.start(53, "*", WiFi.softAPIP());
                isApOn = true;
                lastClientConnectedTime = millis();
                signalAPActive(); // Visual feedback
            }
            longPressHandled = true; // Mark long press as handled to prevent re-triggering
        }
    } else { // Button is released (reading == HIGH)
        if (lastButtonState == LOW) { // Just released
            // It's a short press if a long press wasn't handled
            if (!longPressHandled) {
                Serial.println("[BUTTON] Kratak pritisak.");
                if (mainMode == "auto") {
                  mainMode = "on";
                  Serial.println("[BUTTON] Novi mod: Uvijek Upaljeno");
                  signalModeChange(Adafruit_NeoPixel::Color(0, 255, 0)); // Green for ON
                } else if (mainMode == "on") {
                  mainMode = "off";
                  Serial.println("[BUTTON] Novi mod: Uvijek Ugašeno");
                  signalModeChange(Adafruit_NeoPixel::Color(255, 0, 0)); // Red for OFF
                } else { // mainMode was "off"
                  mainMode = "auto";
                  Serial.println("[BUTTON] Novi mod: Automatski");
                  signalModeChange(Adafruit_NeoPixel::Color(0, 0, 255)); // Blue for AUTO
                }
                saveConfiguration(); // Save the new mode
            }
        }
    }
    lastButtonState = reading; // Update state for next loop
}

void handleAP() {
  if (isApOn) {
    // Check if anyone is connected
    if (WiFi.softAPgetStationNum() > 0) {
      // If so, reset the timer
      lastClientConnectedTime = millis();
    } else {
      // If no one is connected, check if 3 minutes have passed
      if (millis() - lastClientConnectedTime > 180000) { // 3 minutes
        WiFi.softAPdisconnect(true);
        dnsServer.stop();
        isApOn = false;
        Serial.println("[WIFI] AP ugašen zbog neaktivnosti.");
      }
    }
  }
}

// Effect helpers
void effect_solid(Adafruit_NeoPixel &strip, const StripSettings &s, int &lastWritten) {
  strip.fill(s.color);
  lastWritten = s.numLeds;
}

unsigned long lastWipeMillis = 0;
int wipePos = 0;
uint32_t wheel(byte wheelPos) {
  wheelPos = 255 - wheelPos;
  if(wheelPos < 85) {
    return Adafruit_NeoPixel::Color(255 - wheelPos * 3, 0, wheelPos * 3);
  }
  if(wheelPos < 170) {
    wheelPos -= 85;
    return Adafruit_NeoPixel::Color(0, wheelPos * 3, 255 - wheelPos * 3);
  }
  wheelPos -= 170;
  return Adafruit_NeoPixel::Color(wheelPos * 3, 255 - wheelPos * 3, 0);
}

// --- FastLED-like helper functions for fire effect ---
#define qsub8(i, d) max(0L, (long)i - (long)d)
#define qadd8(i, d) min(255L, (long)i + (long)d)
#define scale8(i, scale) (((int)i * (int)scale) >> 8)
#define random8() random(256)
#define random8_2(min, max) random(min, max)

void effect_rainbow(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed) {
  if (endLed <= startLed) { // Nothing to light
      strip.fill(0);
      return;
  }
  for (int i = 0; i < s.numLeds; i++) {
    if (i >= startLed && i < endLed) {
      int pixelHue = rainbowFirstPixelHue + (i * 65536L / s.numLeds);
      strip.setPixelColor(i, strip.gamma32(Adafruit_NeoPixel::ColorHSV(pixelHue)));
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  rainbowFirstPixelHue += 256; // Animate the rainbow
}

void effect_meteor(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed) {
  unsigned long now = millis();

  // Clear pixels outside the active range first
  for(int i=0; i<startLed; i++) strip.setPixelColor(i, 0);
  for(int i=endLed; i<s.numLeds; i++) strip.setPixelColor(i, 0);

  if (endLed <= startLed) { // Nothing to light
      return;
  }

  // Control the speed of the effect using wipeSpeed
  if (now - meteorLastUpdate > s.wipeSpeed) {
    meteorLastUpdate = now;

    // Fade the active pixels to create a trail
    for (int i = startLed; i < endLed; i++) {
      uint32_t c = strip.getPixelColor(i);
      uint8_t r = scale8((c >> 16) & 0xFF, METEOR_FADE_RATE);
      uint8_t g = scale8((c >> 8) & 0xFF, METEOR_FADE_RATE);
      uint8_t b = scale8(c & 0xFF, METEOR_FADE_RATE);
      strip.setPixelColor(i, strip.Color(r, g, b));
    }

    // Reset position if it's out of the current range
    if (meteorPos < startLed || meteorPos >= endLed) {
        meteorPos = startLed;
    }
    
    // Draw the meteor head
    strip.setPixelColor(meteorPos, s.color);

    // Advance the position for the next frame
    meteorPos++;
  }
}

void effect_fire(Adafruit_NeoPixel &strip, const StripSettings &s, int startLed, int endLed) {
  if (endLed <= startLed) { // Nothing to light
      strip.fill(0);
      return;
  }
  
  int stripSize = endLed - startLed;
  if (stripSize <= 0) return;

  // Step 1. Cool down every cell a little
  for (int i = 0; i < stripSize; i++) {
    heat[startLed + i] = qsub8(heat[startLed + i], random8_2(0, FIRE_COOLING));
  }

  // Step 2. Heat from each cell drifts 'up' and diffuses a little
  for (int k = stripSize - 1; k >= 2; k--) {
    heat[startLed + k] = (heat[startLed + k - 1] + heat[startLed + k - 2] + heat[startLed + k - 2]) / 3;
  }
  
  // Step 3. Randomly ignite new 'sparks' of heat near the bottom
  if (random8() < FIRE_SPARKING) {
    int y = startLed + random(FIRE_SPARK_HEIGHT);
    heat[y] = qadd8(heat[y], random8_2(FIRE_HEAT_MIN, FIRE_HEAT_MAX));
  }

  // Step 4. Map heat to LED colors
  for (int j = 0; j < stripSize; j++) {
    byte colorindex = scale8(heat[startLed + j], 240);
    uint32_t color = 0;
    if(colorindex < 85) { // Black to Red
        color = strip.Color(colorindex * 3, 0, 0);
    } else if (colorindex < 170) { // Red to Yellow
        color = strip.Color(255, (colorindex - 85) * 3, 0);
    } else { // Yellow to White
        color = strip.Color(255, 255, (colorindex - 170) * 3);
    }
    strip.setPixelColor(startLed + j, color);
  }

  // Clear outside range
  for(int i=0; i<startLed; i++) strip.setPixelColor(i, 0);
  for(int i=endLed; i<s.numLeds; i++) strip.setPixelColor(i, 0);
}

void applyEffectsAndUpdate() {
  static int lastWrittenA = -1;
  static int lastWrittenB = -1;
  static int prevStartLedB = -1, prevEndLedB = -1;
  bool updatedA = false;
  bool updatedB = false;

  // --- Main Mode Overrides ---
  if (mainMode == "off") {
    if (lastWrittenA != 0) { stripA.fill(0); updatedA = true; lastWrittenA = 0; }
    if (lastWrittenB != 0) { stripB.fill(0); updatedB = true; lastWrittenB = 0; }
    if (updatedA) stripA.show();
    if (updatedB) stripB.show();
    return;
  }

  if (mainMode == "on") {
    // In "on" mode, we show the selected effect across the whole strip
    bool isAnimated = (strip1_settings.effect != "solid");
    if (strip1_settings.enabled) {
        if (isAnimated || lastWrittenB != strip1_settings.numLeds) {
            if (strip1_settings.effect == "rainbow") effect_rainbow(stripB, strip1_settings, 0, strip1_settings.numLeds);
            else if (strip1_settings.effect == "meteor") effect_meteor(stripB, strip1_settings, 0, strip1_settings.numLeds);
            else if (strip1_settings.effect == "fire") effect_fire(stripB, strip1_settings, 0, strip1_settings.numLeds);
            else stripB.fill(strip1_settings.color);
            updatedB = true;
        }
        lastWrittenB = strip1_settings.numLeds;
    }
    if (strip2_settings.enabled) {
        // Similar logic for strip 2 if it gets effects
        if(lastWrittenA != strip2_settings.numLeds) {
            stripA.fill(strip2_settings.color);
            lastWrittenA = strip2_settings.numLeds;
            updatedA = true;
        }
    }
    if (updatedA) stripA.show();
    if (updatedB) stripB.show();
    return;
  }

  // --- Automatic Mode ---
  if (strip1_settings.enabled) {
    if (manualOverride) {
      if (led1_on) {
        if (lastWrittenB != strip1_settings.numLeds) {
            stripB.fill(strip1_settings.color);
            lastWrittenB = strip1_settings.numLeds;
            updatedB = true;
        }
      } else {
        if (lastWrittenB != 0) {
            stripB.fill(0);
            lastWrittenB = 0;
            updatedB = true;
        }
      }
    } else { // AUTOMATIC SENSOR MODE
      int startLed = 0;
      int endLed = 0;
      bool motionDetected = smoothedDistanceB > 0;

      if (strip1_settings.mode == "fill") {
        int ledsToLight = 0;
        if (motionDetected) ledsToLight = map((int)smoothedDistanceB - strip1_settings.offset, 0, strip1_settings.length * 100, 0, strip1_settings.numLeds);
        endLed = constrain(ledsToLight, 0, strip1_settings.numLeds);
      } else if (strip1_settings.mode == "depart") {
        if (motionDetected) {
          int ledsToTurnOff = map((int)smoothedDistanceB - strip1_settings.offset, 0, strip1_settings.length * 100, 0, strip1_settings.numLeds);
          startLed = constrain(ledsToTurnOff, 0, strip1_settings.numLeds);
          endLed = strip1_settings.numLeds;
        } else { 
          startLed = 0;
          endLed = strip1_settings.numLeds;
        }
      } else if (strip1_settings.mode == "follow") {
        if (motionDetected) {
          int centerLed = map((int)smoothedDistanceB - strip1_settings.offset, 0, strip1_settings.length * 100, 0, strip1_settings.numLeds);
          int halfWidth = strip1_settings.followWidth / 2;
          startLed = constrain(centerLed - halfWidth, 0, strip1_settings.numLeds);
          endLed = constrain(centerLed + halfWidth, 0, strip1_settings.numLeds);
        } else {
          startLed = 0;
          endLed = 0;
        }
      }

      bool isAnimated = (strip1_settings.effect != "solid");
      if (isAnimated || startLed != prevStartLedB || endLed != prevEndLedB) {
        updatedB = true; 
        if (strip1_settings.effect == "rainbow") effect_rainbow(stripB, strip1_settings, startLed, endLed);
        else if (strip1_settings.effect == "meteor") effect_meteor(stripB, strip1_settings, startLed, endLed);
        else if (strip1_settings.effect == "fire") effect_fire(stripB, strip1_settings, startLed, endLed);
        else {
          for (int i = 0; i < strip1_settings.numLeds; i++) {
            stripB.setPixelColor(i, (i >= startLed && i < endLed) ? strip1_settings.color : 0);
          }
        }
      }
      prevStartLedB = startLed;
      prevEndLedB = endLed;
      lastWrittenB = endLed; // Update state tracking
    }
  } else { // strip1 disabled
    if (lastWrittenB != 0) { stripB.fill(0); lastWrittenB = 0; updatedB = true; }
  }

  // STRIP2 -> stripA (currently no auto logic)
  if (strip2_settings.enabled) {
      if (lastWrittenA != 0 && mainMode == "auto" && !manualOverride) {
          stripA.fill(0);
          lastWrittenA = 0;
          updatedA = true;
      }
  } else {
    if (lastWrittenA != 0) { stripA.fill(0); lastWrittenA = 0; updatedA = true; }
  }

  if (updatedA) stripA.show();
  if (updatedB) stripB.show();
}

// --- setup / loop ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Pokretanje Sistema v2.5 ---");

  // De-inicijaliziraj watchdog ako je ostao aktivan od prijašnjeg rušenja
  esp_task_wdt_deinit();
  // Ponovno inicijaliziraj watchdog
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  Serial.println("[SYSTEM] Watchdog inicijaliziran.");

  loadConfiguration();

  // Inicijalizacija NeoPixel traka
  stripA.begin();
  stripB.begin();
  stripA.updateLength(strip2_settings.enabled ? strip2_settings.numLeds : 0); // Strip 2 is A
  stripB.updateLength(strip1_settings.enabled ? strip1_settings.numLeds : 0); // Strip 1 is B
  stripA.clear();
  stripB.clear();
  stripA.show();
  stripB.show();
  Serial.println("[LED] NeoPixel Init done.");

  pinMode(TASTER_PIN, INPUT_PULLUP);
  Serial.println("[SYSTEM] Taster pin inicijaliziran.");

  SensorSerialB.begin(115200, SERIAL_8N1, RX_PIN_B, TX_PIN_B);
  Serial.println("[SENSOR] Sensor B init.");

  WiFi.softAP(ssid);
  dnsServer.start(53, "*", WiFi.softAPIP());
  lastClientConnectedTime = millis(); // Inicijalizacija timera
  Serial.print("[WIFI] AP started IP: "); Serial.println(WiFi.softAPIP());

  signalAPActive();

  server.on("/get", HTTP_GET, handleGet);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([](){ if (!handleFileRead(server.uri())) handleFileRead("/index.html"); });
  server.begin();

  // init websocket
  webSocket.begin();
  webSocket.onEvent([](uint8_t num, WStype_t t, uint8_t *p, size_t l){ handleWebSocketMessage(num,t,p,l); });
  Serial.println("[WS] WebSocket server started on port 81.");

  Serial.println("[SYSTEM] Setup complete.");
}

unsigned long lastStateSend = 0;
unsigned long lastSensorProcess = 0;
void loop() {
  esp_task_wdt_reset();
  
  if(isApOn) { // Procesiraj web konekcije samo ako je AP upaljen
    server.handleClient();
    dnsServer.processNextRequest();
    webSocket.loop();
  }

  handleAP(); // Provjeri treba li ugasiti AP
  handleButton();

  // handle incoming serial from sensor B
  while (SensorSerialB.available() > 0) {
    char c = SensorSerialB.read();
    if (c == '\n') {
      if (lineBufferB.startsWith("Range")) {
        String ds = lineBufferB.substring(5);
        int d = ds.toInt();
        if (d > 0) { currentDistanceB = d; lastMotionTimeB = millis(); }
      }
      lineBufferB = "";
    } else if (c >= 32) lineBufferB += c;
  }

  // update smoothedDistanceB (simple lerp towards currentDistanceB)
  unsigned long now = millis();
  float alpha = 0.25f; // smoothing factor (0..1) - prilagodljivo
  if ((now - lastMotionTimeB) > (unsigned long)(strip1_settings.onTime * 1000UL)) {
    // decay to zero slowly
    smoothedDistanceB = smoothedDistanceB * 0.90f;
    if (smoothedDistanceB < 0.5f) smoothedDistanceB = 0;
  } else {
    // approach currentDistanceB
    smoothedDistanceB = smoothedDistanceB + alpha * ((float)currentDistanceB - smoothedDistanceB);
  }

  // apply effects / updates
  applyEffectsAndUpdate();

  // send state to clients 2x per second
  if (millis() - lastStateSend > 500) {
    sendStateToClients();
    lastStateSend = millis();
  }

  delay(10);
}

// --- END OF FILE ---
