// =============================================================================
// === PAMETNO OSVJETLJENJE - FIRMWARE v2.2 (Captive Portal) ====================
// =============================================================================
// Autor: Dejan Habijanec & Gemini AI
// Datum: 23.10.2025.
// Opis: Dodana funkcionalnost Captive Portala za automatsko otvaranje UI.
// =============================================================================

// --- Biblioteke ---
#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// --- Konfiguracija za Watchdog ---
const esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 10000,
  .idle_core_mask = 0,
  .trigger_panic = true
};

// --- Hardverski Pinovi ---
#define RX_PIN_B 10
#define TX_PIN_B 11
#define LED_PIN_A 1
#define LED_PIN_B 2

HardwareSerial SensorSerialB(1);

// --- Postavke LED Traka ---
#define MAX_LEDS 300

CRGB ledsA[MAX_LEDS];
CRGB ledsB[MAX_LEDS];

// --- Struktura za postavke ---
struct StripSettings {
  bool enabled = false;
  String type = "ws2812b";
  int length = 4;
  int density = 60;
  int numLeds = 240;
  int offset = 0;
  String mode = "fill";
  int followWidth = 20;
  int wipeSpeed = 50;
  int onTime = 5;
  String effect = "solid";
  String colorHex = "#0000FF";
  CRGB color = CRGB::Blue;
  int colorTemp = 4000;
  uint8_t brightness = 80;
};

// --- Globalne Varijable ---
String mainMode = "auto";
StripSettings strip1_settings;
StripSettings strip2_settings;

String lineBufferB = "";
int currentDistanceB = 0;
unsigned long lastMotionTimeB = 0;

// --- WiFi i Server ---
const char* ssid = "Pametno_Svjetlo_Setup";
WebServer server(80);
DNSServer dnsServer;

// =============================================================================
// === POMOĆNE FUNKCIJE ========================================================
// =============================================================================

// Parsira HEX string boju (npr. "#FF00AA") u CRGB objekt
CRGB parseHtmlColor(String colorString) {
  if (colorString.startsWith("#")) {
    colorString = colorString.substring(1);
  }
  long number = strtol(colorString.c_str(), NULL, 16);
  return CRGB(number);
}

// Pretvara CRGB u HEX string (bez #)
String rgbToHex(CRGB color) {
  char hexColor[7];
  sprintf(hexColor, "%02X%02X%02X", color.r, color.g, color.b);
  return String(hexColor);
}

// Izračunava broj LED-ica
void calculateNumLeds(StripSettings &settings) {
    settings.numLeds = settings.length * settings.density;
    if (settings.numLeds > MAX_LEDS) {
        settings.numLeds = MAX_LEDS;
        Serial.printf("[WARN] Izračunati broj LED-ica (%d) premašuje MAX_LEDS (%d). Ograničeno na %d.\n", settings.length * settings.density, MAX_LEDS, MAX_LEDS);
    } else if (settings.numLeds <= 0) {
        settings.numLeds = 1; // Minimalno 1 da izbjegnemo greške
        Serial.println("[WARN] Izračunati broj LED-ica je 0 ili manji. Postavljeno na 1.");
    }
}

// Funkcija za spremanje trenutnih postavki u config.json
void saveConfiguration() {
  Serial.println("[CONFIG] Spremam konfiguraciju u SPIFFS...");
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("[ERROR] Nije moguće otvoriti config.json za pisanje.");
    return;
  }

  JsonDocument doc;
  doc["mainMode"] = mainMode;
  int stripCount = 0;
  if(strip1_settings.enabled) stripCount++;
  if(strip2_settings.enabled) stripCount++;
  doc["stripCount"] = stripCount;

  auto saveStripSettings = [&](JsonObject stripObj, const StripSettings& settings) {
      stripObj["type"] = settings.type;
      stripObj["length"] = settings.length;
      stripObj["density"] = settings.density;
      stripObj["offset"] = settings.offset;
      stripObj["mode"] = settings.mode;
      stripObj["followWidth"] = settings.followWidth;
      stripObj["wipeSpeed"] = settings.wipeSpeed;
      stripObj["onTime"] = settings.onTime;
      stripObj["effect"] = settings.effect;
      stripObj["color"] = settings.colorHex;
      stripObj["temp"] = settings.colorTemp;
      stripObj["brightness"] = settings.brightness;
  };

  if (strip1_settings.enabled) {
    JsonObject strip1 = doc.createNestedObject("strip1");
    saveStripSettings(strip1, strip1_settings);
    Serial.println("[CONFIG] Spremljene SVE postavke za Traku 1.");
  }
  if (strip2_settings.enabled) {
    JsonObject strip2 = doc.createNestedObject("strip2");
    saveStripSettings(strip2, strip2_settings);
    Serial.println("[CONFIG] Spremljene SVE postavke za Traku 2.");
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("[ERROR] Nije moguće zapisati u config.json.");
  } else {
    Serial.println("[CONFIG] Konfiguracija uspješno spremljena.");
  }
  configFile.close();
}

// Funkcija koja čita postavke iz config.json pri paljenju
void loadConfiguration() {
  if (SPIFFS.begin(true)) {
    Serial.println("[SYSTEM] SPIFFS montiran.");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("[CONFIG] Pronađen config.json, čitam...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, configFile);
        configFile.close();

        if (error) {
          Serial.print("[ERROR] Greška pri parsiranju JSON-a: ");
          Serial.println(error.c_str());
        } else {
          Serial.println("[CONFIG] Konfiguracija uspješno učitana.");
          mainMode = doc["mainMode"] | "auto";
          int stripCount = doc["stripCount"] | 0;

          auto loadStripSettings = [&](JsonObjectConst stripObj, StripSettings& settings) {
              settings.enabled = true;
              settings.type = stripObj["type"] | "ws2812b";
              settings.length = stripObj["length"] | 4;
              settings.density = stripObj["density"] | 60;
              settings.offset = stripObj["offset"] | 0;
              settings.mode = stripObj["mode"] | "fill";
              settings.followWidth = stripObj["followWidth"] | 20;
              settings.wipeSpeed = stripObj["wipeSpeed"] | 50;
              settings.onTime = stripObj["onTime"] | 5;
              settings.effect = stripObj["effect"] | "solid";
              settings.colorHex = stripObj["color"] | "#0000FF";
              settings.color = parseHtmlColor(settings.colorHex);
              settings.colorTemp = stripObj["temp"] | 4000;
              settings.brightness = stripObj["brightness"] | 80;
              calculateNumLeds(settings);
          };

          if (stripCount >= 1 && doc.containsKey("strip1")) {
            loadStripSettings(doc["strip1"], strip1_settings);
            Serial.println("[CONFIG] Učitane SVE postavke za Traku 1.");
          } else {
            strip1_settings.enabled = false;
          }

          if (stripCount >= 2 && doc.containsKey("strip2")) {
             loadStripSettings(doc["strip2"], strip2_settings);
             Serial.println("[CONFIG] Učitane SVE postavke za Traku 2.");
          } else {
            strip2_settings.enabled = false;
          }

          uint8_t loaded_brightness = 80; // Default brightness
          if (strip1_settings.enabled) loaded_brightness = strip1_settings.brightness;
          else if (strip2_settings.enabled) loaded_brightness = strip2_settings.brightness;
          FastLED.setBrightness(loaded_brightness);
          Serial.printf("[LED] Svjetlina postavljena na %d\n", loaded_brightness);

        }
      }
    } else {
      Serial.println("[CONFIG] config.json ne postoji, kreiram i koristim default postavke.");
      strip1_settings.enabled = true;
      strip2_settings.enabled = false;
      calculateNumLeds(strip1_settings);
      calculateNumLeds(strip2_settings);
      saveConfiguration();
    }
  } else {
    Serial.println("[ERROR] Nije moguće montirati SPIFFS.");
  }
}

// Funkcija za signalizaciju
void signalAPActive() {
  Serial.println("[WIFI] Signaliziram da je AP aktivan (treptanje)...");
  int numLedsToShow = strip1_settings.enabled ? strip1_settings.numLeds : (strip2_settings.enabled ? strip2_settings.numLeds : 10);
  CRGB* ledsToShow = strip1_settings.enabled ? ledsB : (strip2_settings.enabled ? ledsA : nullptr); // Koristi B za strip1, A za strip2

   if (ledsToShow && numLedsToShow > 0) { // Provjeri da li je ledsToShow validan i da li ima LED-ica
    uint8_t currentBrightness = FastLED.getBrightness(); // Spremi trenutnu svjetlinu
    FastLED.setBrightness(50); // Smanji za signalizaciju
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 10 && j < numLedsToShow; j++) ledsToShow[j] = CRGB::White;
        FastLED.show();
        delay(200);
        for (int j = 0; j < 10 && j < numLedsToShow; j++) ledsToShow[j] = CRGB::Black;
        FastLED.show();
        delay(200);
    }
    FastLED.setBrightness(currentBrightness); // Vrati originalnu svjetlinu
   } else {
       Serial.println("[WIFI] Nema aktivnih LED traka za signalizaciju.");
   }
}

// =============================================================================
// === WEB SERVER HANDLERI =====================================================
// =============================================================================

// Funkcija koja poslužuje datoteke iz SPIFFS memorije
bool handleFileRead(String path) {
  Serial.print("[WEB] Tražena datoteka: ");
  Serial.println(path);
  if (path.endsWith("/")) path = "/index.html"; // Ispravak: korijen je index.html

  String contentType = "text/html";
  // Odredi Content-Type na temelju ekstenzije
  if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".jpg")) contentType = "image/jpeg";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  else if (path.endsWith(".gz")) contentType = "application/x-gzip"; // Za komprimirane datoteke

  if (SPIFFS.exists(path) || SPIFFS.exists(path + ".gz")) { // Provjeri i za komprimiranu verziju
    if(SPIFFS.exists(path + ".gz")){
       path += ".gz"; // Ako postoji .gz, posluži nju
       Serial.print("[WEB] Poslužujem komprimiranu verziju: "); Serial.println(path);
    }
    File file = SPIFFS.open(path, "r");
    // Postavi Cache-Control header za bolji performance
    server.sendHeader("Cache-Control", "max-age=86400"); // Cache na 1 dan
    if (path.endsWith(".gz")) server.sendHeader("Content-Encoding", "gzip");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("[WEB] Datoteka poslužena.");
    return true;
  }
  Serial.println("[WEB] Greška: Datoteka nije pronađena.");
  return false;
}

// Handler koji šalje TRENUTNE postavke web stranici
void handleGet() {
  Serial.println("[WEB] Zahtjev za dohvaćanje postavki ('/get').");
  JsonDocument doc;

  doc["mainMode"] = mainMode;
  int stripCount = 0;
  if(strip1_settings.enabled) stripCount++;
  if(strip2_settings.enabled) stripCount++;
  doc["stripCount"] = stripCount;

  auto fillStripJson = [&](JsonObject stripObj, const StripSettings& settings) {
      stripObj["type"] = settings.type;
      stripObj["length"] = settings.length;
      stripObj["density"] = settings.density;
      stripObj["offset"] = settings.offset;
      stripObj["mode"] = settings.mode;
      stripObj["followWidth"] = settings.followWidth;
      stripObj["wipeSpeed"] = settings.wipeSpeed;
      stripObj["onTime"] = settings.onTime;
      stripObj["effect"] = settings.effect;
      stripObj["color"] = settings.colorHex;
      stripObj["temp"] = settings.colorTemp;
      stripObj["brightness"] = settings.brightness;
  };

  if (strip1_settings.enabled) {
    JsonObject strip1 = doc.createNestedObject("strip1");
    fillStripJson(strip1, strip1_settings);
  }
   if (strip2_settings.enabled) {
    JsonObject strip2 = doc.createNestedObject("strip2");
    fillStripJson(strip2, strip2_settings);
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// Handler koji prima postavke s web stranice i sprema ih
void handleSave() {
  Serial.println("[WEB] Primljen zahtjev za spremanje postavki ('/save').");
  if (server.hasArg("plain") == false) { 
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  String body = server.arg("plain");
  Serial.print("[WEB] Primljeni podaci: ");
  Serial.println(body);
  
  JsonDocument doc; 
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.print("[ERROR] Greška pri parsiranju JSON-a sa stranice: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  mainMode = doc["mainMode"] | "auto";
  int stripCount = doc["stripCount"] | 0;

  auto parseStripSettings = [&](JsonObjectConst stripObj, StripSettings& settings) {
      settings.enabled = true; 
      settings.type = stripObj["type"] | settings.type;
      settings.length = stripObj["length"] | settings.length;
      settings.density = stripObj["density"] | settings.density;
      settings.offset = stripObj["offset"] | settings.offset;
      settings.mode = stripObj["mode"] | settings.mode;
      settings.followWidth = stripObj["followWidth"] | settings.followWidth;
      settings.wipeSpeed = stripObj["wipeSpeed"] | settings.wipeSpeed;
      settings.onTime = stripObj["onTime"] | settings.onTime;
      settings.effect = stripObj["effect"] | settings.effect;
      settings.colorHex = stripObj["color"] | settings.colorHex;
      settings.color = parseHtmlColor(settings.colorHex);
      settings.colorTemp = stripObj["temp"] | settings.colorTemp;
      settings.brightness = stripObj["brightness"] | settings.brightness;
      calculateNumLeds(settings);
      Serial.printf("[CONFIG] Ažurirane postavke: %d LED-ica\n", settings.numLeds);
  };

  if (stripCount >= 1 && doc.containsKey("strip1")) {
      parseStripSettings(doc["strip1"], strip1_settings);
      FastLED.setBrightness(strip1_settings.brightness); 
      Serial.println("[CONFIG] Postavke za Traku 1 ažurirane.");
  } else {
      strip1_settings.enabled = false;
      Serial.println("[CONFIG] Traka 1 onemogućena.");
  }

  if (stripCount == 2 && doc.containsKey("strip2")) {
      parseStripSettings(doc["strip2"], strip2_settings);
      if (!strip1_settings.enabled) FastLED.setBrightness(strip2_settings.brightness);
      Serial.println("[CONFIG] Postavke za Traku 2 ažurirane.");
  } else {
      strip2_settings.enabled = false;
      Serial.println("[CONFIG] Traka 2 onemogućena.");
  }
  
  saveConfiguration(); // Spremi nove postavke u SPIFFS
  server.send(200, "text/plain", "OK");
}


// =============================================================================
// === SETUP ===================================================================
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- Pokretanje Sistema v2.2 (Captive Portal) ---");

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  Serial.println("[SYSTEM] Watchdog inicijaliziran.");
  
  loadConfiguration(); // Učitaj spremljene postavke

  // Ispravak: Provjeri da li je numLeds > 0 prije inicijalizacije
  if (strip1_settings.numLeds > 0) {
      FastLED.addLeds<WS2812B, LED_PIN_A, GRB>(ledsA, strip1_settings.numLeds);
  } else {
       FastLED.addLeds<WS2812B, LED_PIN_A, GRB>(ledsA, 1); // Fallback na 1 LED
  }
  if (strip2_settings.numLeds > 0) {
     FastLED.addLeds<WS2812B, LED_PIN_B, GRB>(ledsB, strip2_settings.numLeds);
  } else {
      FastLED.addLeds<WS2812B, LED_PIN_B, GRB>(ledsB, 1); // Fallback na 1 LED
  }
  
  FastLED.clear(true);
  FastLED.show();
  Serial.println("[LED] Trake inicijalizirane.");

  SensorSerialB.begin(115200, SERIAL_8N1, RX_PIN_B, TX_PIN_B);
  Serial.println("[SENSOR] Senzor B inicijaliziran.");
  
  WiFi.softAP(ssid);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.print("[WIFI] AP pokrenut. IP adresa: ");
  Serial.println(WiFi.softAPIP());
  
  signalAPActive();

  // Postavljamo handlere za API
  server.on("/get", HTTP_GET, handleGet);
  server.on("/save", HTTP_POST, handleSave);
  
  // Svi ostali zahtjevi se tretiraju kao zahtjevi za datotekom
  // OVO JE KLJUČNA PROMJENA ZA CAPTIVE PORTAL:
  server.onNotFound([](){ 
    // Ako datoteka ne postoji, ili ako je tražen korijen "/",
    // uvijek posluži index.html
    if (!handleFileRead(server.uri())) {
       Serial.println("[WEB] Datoteka nije pronađena, poslužujem index.html (Captive Portal)");
       handleFileRead("/index.html"); 
    }
  });
  
  server.begin();
  Serial.println("[WEB] Server pokrenut. Sistem je spreman.");
}
// =============================================================================

// =============================================================================
// === GLAVNA PETLJA (LOOP) =====================================================
// =============================================================================
void loop() {
  esp_task_wdt_reset();
  server.handleClient();
  dnsServer.processNextRequest(); // Ovo je ključno za Captive Portal

  // 1. Provjera Glavnog Načina Rada
  if (mainMode == "off") {
      fill_solid(ledsA, MAX_LEDS, CRGB::Black);
      fill_solid(ledsB, MAX_LEDS, CRGB::Black);
      FastLED.show();
      return; 
  }

  if (mainMode == "on") {
      if(strip1_settings.enabled) fill_solid(ledsB, strip1_settings.numLeds, strip1_settings.color);
      else fill_solid(ledsB, MAX_LEDS, CRGB::Black);
      
      if(strip2_settings.enabled) fill_solid(ledsA, strip2_settings.numLeds, strip2_settings.color);
      else fill_solid(ledsA, MAX_LEDS, CRGB::Black);
      
      FastLED.show();
      return; 
  }
  
  // 2. Automatski Način Rada (ako je mainMode == "auto")
  
  // Čitanje Senzora B (za Traku 1 - ledsB)
  if (strip1_settings.enabled) {
      while (SensorSerialB.available() > 0) {
        char incomingChar = SensorSerialB.read();
        if (incomingChar == '\n') {
          if (lineBufferB.startsWith("Range")) {
            String distanceString = lineBufferB.substring(6);
            currentDistanceB = distanceString.toInt();
            if (currentDistanceB > 0) {
               lastMotionTimeB = millis(); 
            }
          }
          lineBufferB = "";
        } else if (incomingChar >= 32) {
          lineBufferB += incomingChar;
        }
      }

      int ledsToLightB = 0; 
      if (millis() - lastMotionTimeB < (strip1_settings.onTime * 1000UL)) {
          // TODO: Ovdje dodati logiku za različite modove (fill, follow, depart) i offset
          ledsToLightB = map(currentDistanceB - strip1_settings.offset, 0, strip1_settings.length * 100, 0, strip1_settings.numLeds);
          ledsToLightB = constrain(ledsToLightB, 0, strip1_settings.numLeds); 
      }
      
      for (int i = 0; i < strip1_settings.numLeds; i++) {
        // TODO: Ovdje dodati logiku za efekte
        ledsB[i] = (i < ledsToLightB) ? strip1_settings.color : CRGB::Black;
      }
  } else {
      fill_solid(ledsB, MAX_LEDS, CRGB::Black);
  }

  // Čitanje Senzora A (za Traku 2 - ledsA) - KADA SE IMPLEMENTIRA V2
  if (strip2_settings.enabled) {
     fill_solid(ledsA, strip2_settings.numLeds, strip2_settings.color); // Placeholder
  } else {
     fill_solid(ledsA, MAX_LEDS, CRGB::Black);
  }

  FastLED.show();
  delay(10);
}
// =============================================================================

