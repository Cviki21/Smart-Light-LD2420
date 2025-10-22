// =============================================================================
// === PAMETNO OSVJETLJENJE - FIRMWARE v1.0 =====================================
// =============================================================================
// Autor: Dejan Habijanec & Gemini AI
// Datum: 22.10.2025.
// Opis: Finalna, stabilna verzija s potpunim web sučeljem i spremanjem
//       postavki u SPIFFS. Koristi provjerenu metodu za stabilnost
//       s Watchdog Timerom.
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

// --- Konfiguracija za Watchdog (Tvoja provjerena metoda) ---
const esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 10000, // 10 sekundi timeout
  .idle_core_mask = 0,
  .trigger_panic = true
};

// --- Hardverski Pinovi ---
// Senzori (UART)
#define RX_PIN_A 13 // Za V2 pločicu
#define TX_PIN_A 12 // Za V2 pločicu
#define RX_PIN_B 10 // Port koji radi na V1
#define TX_PIN_B 11 // Port koji radi na V1
// LED Trake
#define LED_PIN_A 1
#define LED_PIN_B 2
// Taster (za V2 pločicu)
#define TASTER_PIN 7

// Serijski portovi za senzore
HardwareSerial SensorSerialA(2);
HardwareSerial SensorSerialB(1);

// --- Postavke LED Traka ---
#define MAX_LEDS 300 // Maksimalan broj LED-ica koji podržavamo po traci

CRGB ledsA[MAX_LEDS];
CRGB ledsB[MAX_LEDS];

// --- Struktura za spremanje postavki ---
// Koristimo strukturu da sve bude organizirano
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
  CRGB color = CRGB::Blue;
  int colorTemp = 4000;
  uint8_t brightness = 80;
};

// --- Globalne Varijable ---
String mainMode = "auto";
StripSettings strip1_settings;
StripSettings strip2_settings;

// Varijable za rad u loop-u
String lineBufferA = "";
String lineBufferB = "";
int currentDistanceA = 0;
int currentDistanceB = 0;
unsigned long lastMotionTimeA = 0;
unsigned long lastMotionTimeB = 0;

// --- WiFi i Server ---
const char* ssid = "Pametno_Svjetlo_Setup";
WebServer server(80);
DNSServer dnsServer;


// =============================================================================
// === HTML, CSS & JavaScript KOD ZA KORISNIČKO SUČELJE =========================
// =============================================================================
const char HTML_PROGMEM[] = R"rawliteral(
<!DOCTYPE html>
<html lang="hr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Postavke Svjetla</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --background-color: #121212;
            --surface-color: #1e1e1e;
            --primary-color: #bb86fc;
            --primary-variant-color: #a166f7;
            --secondary-color: #03dac6;
            --text-color: #e0e0e0;
            --control-background: #333;
            --border-color: #444;
        }
        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background-color: var(--background-color);
            color: var(--text-color);
            display: flex;
            justify-content: center;
            align-items: flex-start;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
            box-sizing: border-box;
        }
        .container {
            background-color: var(--surface-color);
            padding: 25px;
            border-radius: 16px;
            box-shadow: 0 8px 30px rgba(0,0,0,0.5);
            width: 100%;
            max-width: 450px;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        .language-dropdown {
            position: relative;
            display: inline-block;
            margin-top: 15px;
        }
        .language-selector-btn {
            display: flex;
            align-items: center;
            gap: 8px;
            background-color: transparent;
            border: 1px solid var(--border-color);
            color: #aaa;
            padding: 8px 15px;
            border-radius: 20px;
            cursor: pointer;
            font-size: 14px;
            transition: background-color 0.2s, color 0.2s;
        }
        .language-selector-btn:hover {
            background-color: var(--control-background);
            color: var(--text-color);
        }
        .language-selector-btn .globe-icon {
            width: 18px;
            height: 18px;
            stroke: currentColor;
        }
        .language-selector-btn .arrow {
            font-size: 10px;
            transition: transform 0.2s;
        }
        .language-menu {
            display: none;
            position: absolute;
            top: 120%;
            left: 50%;
            transform: translateX(-50%);
            background-color: var(--control-background);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 5px;
            z-index: 10;
            min-width: 130px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.4);
        }
        .language-menu a {
            color: var(--text-color);
            text-decoration: none;
            display: block;
            padding: 8px 12px;
            border-radius: 5px;
        }
        .language-menu a:hover {
            background-color: var(--primary-color);
            color: var(--background-color);
        }
        .language-menu.show { display: block; }
        .language-selector-btn.open .arrow { transform: rotate(180deg); }
        
        h1, h2, h3 {
            text-align: center;
            margin-top: 0;
            font-weight: 700;
        }
        h1 { color: var(--primary-color); margin-bottom: 5px; font-size: 28px; }
        h2 {
            color: var(--secondary-color);
            margin-top: 30px;
            margin-bottom: 20px;
            border-top: 1px solid var(--border-color);
            padding-top: 25px;
        }
        h3 { color: var(--text-color); font-weight: 500; margin-bottom: 20px; border-top: 1px solid var(--border-color); padding-top: 25px;}
        .control-group { margin-bottom: 25px; }
        label, .label-span { display: block; margin-bottom: 10px; font-weight: 500; }
        select, input[type="number"], input[type="color"], input[type="range"] {
            width: 100%;
            padding: 12px;
            background-color: var(--control-background);
            color: var(--text-color);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            font-size: 16px;
            box-sizing: border-box;
        }
        input[type="range"] { padding: 0; }
        .dynamic-options { display: none; margin-top: 20px; }
        .strip-settings {
            display: none;
            opacity: 0;
            transition: opacity 0.5s ease-in-out;
            overflow: hidden;
            max-height: 0;
        }
        .strip-settings.visible {
            display: block;
            opacity: 1;
            max-height: 2000px;
            transition: opacity 0.5s ease-in-out, max-height 1s ease-in-out;
        }
        button {
            width: 100%;
            padding: 15px;
            background-color: var(--primary-color);
            color: var(--background-color);
            border: none;
            border-radius: 8px;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
            transition: background-color 0.2s;
            -webkit-tap-highlight-color: transparent;
            margin-top: 20px;
        }
        .copy-button {
             background-color: var(--secondary-color);
             font-size: 14px;
             padding: 10px;
             margin-bottom: 20px;
        }
        button:active { background-color: var(--primary-variant-color); }
        .status { text-align: center; margin-top: 20px; color: var(--secondary-color); height: 20px; font-weight: 500; }
        .range-value { font-weight: normal; color: #aaa; margin-left: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 data-lang="mainTitle">Postavke Osvjetljenja</h1>
             <div class="language-dropdown">
                <button id="language-selector-btn" class="language-selector-btn" onclick="toggleLanguageMenu()">
                    <svg class="globe-icon" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                    <span id="current-lang-text">Hrvatski</span>
                    <span class="arrow">▼</span>
                </button>
                <div id="language-menu" class="language-menu">
                    <a href="#" onclick="changeLanguage('hr'); return false;">🇭🇷 Hrvatski</a>
                    <a href="#" onclick="changeLanguage('en'); return false;">🇬🇧 English</a>
                </div>
            </div>
        </div>
        
        <h3 data-lang="globalSettings">Globalne Postavke</h3>
        <div class="control-group">
            <label for="mainMode" data-lang="mainMode">Glavni način rada</label>
            <select id="mainMode">
                <option value="auto" selected data-lang="mainModeAuto">Automatski (preko senzora)</option>
                <option value="on" data-lang="mainModeOn">Uvijek upaljeno (Ambijentalni)</option>
                <option value="off" data-lang="mainModeOff">Uvijek ugašeno</option>
            </select>
        </div>

        <div class="control-group">
            <label for="stripCount" data-lang="stripCountLabel">Koliko LED traka koristite?</label>
            <select id="stripCount" onchange="updateUI()">
                <option value="0" data-lang="selectOption">Odaberi...</option>
                <option value="1" data-lang="oneStrip">Jedna traka</option>
                <option value="2" data-lang="twoStrips">Dvije trake</option>
            </select>
        </div>
        
        <div id="strip1Settings" class="strip-settings">
            <h2 data-lang="strip1Title">Postavke za Traku 1</h2>
            <h3 data-lang="physicalSettings">Fizičke Postavke</h3>
            <div class="control-group">
                <label for="strip1Type" data-lang="stripType">Tip trake</label>
                <select id="strip1Type" onchange="updateUI()">
                    <option value="none" data-lang="selectType">Odaberi tip...</option>
                    <option value="ws2812b">RGB (WS2812B)</option>
                    <option value="white" data-lang="whiteStrip">Jednobojna Bijela</option>
                </select>
            </div>
            <div class="control-group">
                <label for="strip1Length" data-lang="stripLength">Dužina trake (m)</label>
                <input type="number" id="strip1Length" min="1" max="20" value="4">
            </div>
            <div class="control-group">
                <label for="strip1Density" data-lang="stripDensity">Gustoća LED-ica (LED/m)</label>
                <select id="strip1Density">
                    <option value="30">30 LED/m</option>
                    <option value="60" selected>60 LED/m</option>
                    <option value="144">144 LED/m</option>
                </select>
            </div>
            <div class="control-group">
                <label for="strip1Offset" data-lang="sensorOffset">Pomak senzora (cm)</label>
                <input type="number" id="strip1Offset" value="0">
            </div>
            
            <div id="strip1DynamicOptions" class="dynamic-options">
                <h3 data-lang="behaviorSettings">Ponašanje Senzora</h3>
                <div class="control-group">
                    <label for="strip1Mode" data-lang="activationMode">Način paljenja</label>
                    <select id="strip1Mode" onchange="updateUI()">
                        <option value="fill" data-lang="modeFill">Cijela traka do korisnika</option>
                        <option value="follow" data-lang="modeFollow">Prati korisnika</option>
                        <option value="depart" data-lang="modeDepart">Odlazak (pali se ispred)</option>
                    </select>
                </div>
                <div id="strip1FollowOptions" class="dynamic-options">
                     <div class="control-group">
                        <label for="strip1FollowWidth" data-lang="segmentWidth">Širina segmenta (broj LED-ica)</label>
                        <input type="number" id="strip1FollowWidth" min="1" max="50" value="20">
                    </div>
                </div>
                <div class="control-group">
                    <label for="strip1WipeSpeed" data-lang="animationSpeed">Brzina animacije (ms)</label>
                    <input type="number" id="strip1WipeSpeed" value="50" min="1">
                </div>
                <div class="control-group">
                    <label for="strip1OnTime" data-lang="turnOffTime">Vrijeme gašenja (sek)</label>
                    <input type="number" id="strip1OnTime" value="5" min="0">
                </div>

                <h3 data-lang="appearanceSettings">Izgled i Efekti</h3>
                <div id="strip1RgbOptions" class="dynamic-options">
                    <div class="control-group">
                        <label for="strip1Effect" data-lang="effect">Efekt</label>
                        <select id="strip1Effect" onchange="updateUI()">
                            <option value="solid" data-lang="effectSolid">Statična Boja</option>
                            <option value="rainbow" data-lang="effectRainbow">Duga</option>
                            <option value="meteor" data-lang="effectMeteor">Meteor Kiša</option>
                            <option value="fire" data-lang="effectFire">Vatra</option>
                        </select>
                    </div>
                    <div id="strip1ColorContainer" class="control-group">
                        <label for="strip1Color" data-lang="color">Boja</label>
                        <input type="color" id="strip1Color" value="#0000FF">
                    </div>
                </div>
                <div id="strip1WhiteOptions" class="dynamic-options">
                    <div class="control-group">
                        <label for="strip1Temp" data-lang="colorTemp">Temperatura Boje <span id="strip1TempValue" class="range-value">4000K</span></label>
                        <input type="range" id="strip1Temp" min="2700" max="6500" value="4000" oninput="updateRangeValue('strip1TempValue', this.value + 'K')">
                    </div>
                </div>
                <div class="control-group">
                    <label for="strip1Brightness" data-lang="brightness">Svjetlina <span id="strip1BrightnessValue" class="range-value">80</span></label>
                    <input type="range" id="strip1Brightness" min="10" max="255" value="80" oninput="updateRangeValue('strip1BrightnessValue', this.value)">
                </div>
            </div>
        </div>

        <div id="strip2Settings" class="strip-settings">
            <h2 data-lang="strip2Title">Postavke za Traku 2</h2>
            <button class="copy-button" onclick="copySettings(1, 2)" data-lang="copyButton">Kopiraj postavke s Trake 1</button>
            <h3 data-lang="physicalSettings">Fizičke Postavke</h3>
            <div class="control-group">
                <label for="strip2Type" data-lang="stripType">Tip trake</label>
                <select id="strip2Type" onchange="updateUI()">
                    <option value="none" data-lang="selectType">Odaberi tip...</option>
                    <option value="ws2812b">RGB (WS2812B)</option>
                    <option value="white" data-lang="whiteStrip">Jednobojna Bijela</option>
                </select>
            </div>
            <div class="control-group">
                <label for="strip2Length" data-lang="stripLength">Dužina trake (m)</label>
                <input type="number" id="strip2Length" min="1" max="20" value="4">
            </div>
            <div class="control-group">
                <label for="strip2Density" data-lang="stripDensity">Gustoća LED-ica (LED/m)</label>
                <select id="strip2Density">
                    <option value="30">30 LED/m</option>
                    <option value="60" selected>60 LED/m</option>
                    <option value="144">144 LED/m</option>
                </select>
            </div>
            <div class="control-group">
                <label for="strip2Offset" data-lang="sensorOffset">Pomak senzora (cm)</label>
                <input type="number" id="strip2Offset" value="0">
            </div>
            
             <div id="strip2DynamicOptions" class="dynamic-options">
                <h3 data-lang="behaviorSettings">Ponašanje Senzora</h3>
                <div class="control-group">
                    <label for="strip2Mode" data-lang="activationMode">Način paljenja</label>
                    <select id="strip2Mode" onchange="updateUI()">
                        <option value="fill" data-lang="modeFill">Cijela traka do korisnika</option>
                        <option value="follow" data-lang="modeFollow">Prati korisnika</option>
                        <option value="depart" data-lang="modeDepart">Odlazak (pali se ispred)</option>
                    </select>
                </div>
                <div id="strip2FollowOptions" class="dynamic-options">
                     <div class="control-group">
                        <label for="strip2FollowWidth" data-lang="segmentWidth">Širina segmenta (broj LED-ica)</label>
                        <input type="number" id="strip2FollowWidth" min="1" max="50" value="20">
                    </div>
                </div>
                <div class="control-group">
                    <label for="strip2WipeSpeed" data-lang="animationSpeed">Brzina animacije (ms)</label>
                    <input type="number" id="strip2WipeSpeed" value="50" min="1">
                </div>
                <div class="control-group">
                    <label for="strip2OnTime" data-lang="turnOffTime">Vrijeme gašenja (sek)</label>
                    <input type="number" id="strip2OnTime" value="5" min="0">
                </div>
                
                <h3 data-lang="appearanceSettings">Izgled i Efekti</h3>
                <div id="strip2RgbOptions" class="dynamic-options">
                    <div class="control-group">
                        <label for="strip2Effect" data-lang="effect">Efekt</label>
                        <select id="strip2Effect" onchange="updateUI()">
                            <option value="solid" data-lang="effectSolid">Statična Boja</option>
                            <option value="rainbow" data-lang="effectRainbow">Duga</option>
                            <option value="meteor" data-lang="effectMeteor">Meteor Kiša</option>
                            <option value="fire" data-lang="effectFire">Vatra</option>
                        </select>
                    </div>
                    <div id="strip2ColorContainer" class="control-group">
                        <label for="strip2Color" data-lang="color">Boja</label>
                        <input type="color" id="strip2Color" value="#0000FF">
                    </div>
                </div>
                <div id="strip2WhiteOptions" class="dynamic-options">
                    <div class="control-group">
                        <label for="strip2Temp" data-lang="colorTemp">Temperatura Boje <span id="strip2TempValue" class="range-value">4000K</span></label>
                        <input type="range" id="strip2Temp" min="2700" max="6500" value="4000" oninput="updateRangeValue('strip2TempValue', this.value + 'K')">
                    </div>
                </div>
                <div class="control-group">
                    <label for="strip2Brightness" data-lang="brightness">Svjetlina <span id="strip2BrightnessValue" class="range-value">80</span></label>
                    <input type="range" id="strip2Brightness" min="10" max="255" value="80" oninput="updateRangeValue('strip2BrightnessValue', this.value)">
                </div>
            </div>
        </div>

        <div id="saveButtonContainer" style="display: none;">
            <button onclick="saveSettings()" data-lang="saveButton">Spremi</button>
            <div id="status" class="status"></div>
        </div>
    </div>

    <script>
        const translations = {
            hr: {
                currentLang: "Hrvatski", mainTitle: "Postavke Osvjetljenja", globalSettings: "Globalne Postavke", mainMode: "Glavni način rada", mainModeAuto: "Automatski (preko senzora)", mainModeOn: "Uvijek upaljeno (Ambijentalni)", mainModeOff: "Uvijek ugašeno", stripCountLabel: "Koliko LED traka koristite?", selectOption: "Odaberi...", oneStrip: "Jedna traka", twoStrips: "Dvije trake", strip1Title: "Postavke za Traku 1", strip2Title: "Postavke za Traku 2", physicalSettings: "Fizičke Postavke", stripType: "Tip trake", selectType: "Odaberi tip...", whiteStrip: "Jednobojna Bijela", stripLength: "Dužina trake (m)", stripDensity: "Gustoća LED-ica (LED/m)", sensorOffset: "Pomak senzora (cm)", behaviorSettings: "Ponašanje Senzora", activationMode: "Način paljenja", modeFill: "Cijela traka do korisnika", modeFollow: "Prati korisnika", modeDepart: "Odlazak (pali se ispred)", segmentWidth: "Širina segmenta (broj LED-ica)", animationSpeed: "Brzina animacije (ms)", turnOffTime: "Vrijeme gašenja (sek)", appearanceSettings: "Izgled i Efekti", effect: "Efekt", effectSolid: "Statična Boja", effectRainbow: "Duga", effectMeteor: "Meteor Kiša", effectFire: "Vatra", color: "Boja", colorTemp: "Temperatura Boje", brightness: "Svjetlina", copyButton: "Kopiraj postavke s Trake 1", saveButton: "Spremi", savingStatus: "Spremam...", savedStatus: "Postavke spremljene!", errorStatus: "Greška pri spremanju!"
            },
            en: {
                currentLang: "English", mainTitle: "Light Settings", globalSettings: "Global Settings", mainMode: "Main operating mode", mainModeAuto: "Automatic (via sensor)", mainModeOn: "Always On (Ambient)", mainModeOff: "Always Off", stripCountLabel: "How many LED strips are you using?", selectOption: "Select...", oneStrip: "One strip", twoStrips: "Two strips", strip1Title: "Strip 1 Settings", strip2Title: "Strip 2 Settings", physicalSettings: "Physical Setup", stripType: "Strip type", selectType: "Select type...", whiteStrip: "Single Color White", stripLength: "Strip length (m)", stripDensity: "LED density (LED/m)", sensorOffset: "Sensor offset (cm)", behaviorSettings: "Sensor Behavior", activationMode: "Activation mode", modeFill: "Fill up to user", modeFollow: "Follow the user", modeDepart: "Depart (lights up ahead)", segmentWidth: "Segment width (number of LEDs)", animationSpeed: "Animation speed (ms)", turnOffTime: "Turn-off time (sec)", appearanceSettings: "Appearance & Effects", effect: "Effect", effectSolid: "Solid Color", effectRainbow: "Rainbow", effectMeteor: "Meteor Shower", effectFire: "Fire", color: "Color", colorTemp: "Color Temperature", brightness: "Brightness", copyButton: "Copy settings from Strip 1", saveButton: "Save", savingStatus: "Saving...", savedStatus: "Settings saved!", errorStatus: "Error saving!"
            }
        };

        function toggleLanguageMenu() {
            document.getElementById('language-menu').classList.toggle('show');
            document.getElementById('language-selector-btn').classList.toggle('open');
        }
        
        function changeLanguage(lang) {
            document.querySelectorAll('[data-lang]').forEach(el => {
                const key = el.dataset.lang;
                if (translations[lang] && translations[lang][key]) el.textContent = translations[lang][key];
            });
            document.getElementById('current-lang-text').textContent = translations[lang].currentLang;
            document.documentElement.lang = lang;
            toggleLanguageMenu();
        }

        function updateRangeValue(elementId, value) {
            const element = document.getElementById(elementId);
            if (element) element.textContent = value;
        }
        
        function updateUI() {
            const stripCount = document.getElementById('stripCount').value;
            
            function handleStripVisibility(stripNum) {
                const stripSettings = document.getElementById(`strip${stripNum}Settings`);
                if (!stripSettings) return;

                const stripType = document.getElementById(`strip${stripNum}Type`).value;
                const dynamicOptions = document.getElementById(`strip${stripNum}DynamicOptions`);
                const rgbOptions = document.getElementById(`strip${stripNum}RgbOptions`);
                const whiteOptions = document.getElementById(`strip${stripNum}WhiteOptions`);
                const stripMode = document.getElementById(`strip${stripNum}Mode`).value;
                const followOptions = document.getElementById(`strip${stripNum}FollowOptions`);
                const stripEffect = document.getElementById(`strip${stripNum}Effect`).value;
                const colorContainer = document.getElementById(`strip${stripNum}ColorContainer`);

                if (stripCount >= stripNum) {
                    stripSettings.classList.add('visible');
                    
                    if (stripType !== 'none') {
                        dynamicOptions.style.display = 'block';
                        rgbOptions.style.display = (stripType === 'ws2812b') ? 'block' : 'none';
                        whiteOptions.style.display = (stripType === 'white') ? 'block' : 'none';
                        
                        if (stripType === 'ws2812b' && colorContainer) colorContainer.style.display = (stripEffect === 'solid') ? 'block' : 'none';
                        if(followOptions) followOptions.style.display = (stripMode === 'follow') ? 'block' : 'none';

                    } else {
                        dynamicOptions.style.display = 'none';
                    }
                } else {
                    stripSettings.classList.remove('visible');
                }
            }

            handleStripVisibility(1);
            handleStripVisibility(2);

            document.getElementById('saveButtonContainer').style.display = (stripCount > 0) ? 'block' : 'none';
        }
        
        function copySettings(sourceNum, destNum) {
            const fields = ['Type', 'Effect', 'Color', 'Temp', 'Brightness', 'WipeSpeed', 'OnTime', 'Length', 'Density', 'Offset', 'Mode', 'FollowWidth'];
            fields.forEach(field => {
                const sourceEl = document.getElementById(`strip${sourceNum}${field}`);
                const destEl = document.getElementById(`strip${destNum}${field}`);
                if (sourceEl && destEl) {
                    destEl.value = sourceEl.value;
                    if (destEl.type === 'range') destEl.dispatchEvent(new Event('input'));
                }
            });
            updateUI();
            const statusDiv = document.getElementById('status');
            const lang = document.documentElement.lang || 'hr';
            statusDiv.textContent = translations[lang].savedStatus;
            setTimeout(() => { statusDiv.textContent = ''; }, 2000);
        }

        function getSettingsAsJson() {
            let settings = {
                mainMode: document.getElementById('mainMode').value,
                stripCount: parseInt(document.getElementById('stripCount').value)
            };

            for (let i = 1; i <= settings.stripCount; i++) {
                settings[`strip${i}`] = {
                    type: document.getElementById(`strip${i}Type`).value,
                    length: parseInt(document.getElementById(`strip${i}Length`).value),
                    density: parseInt(document.getElementById(`strip${i}Density`).value),
                    offset: parseInt(document.getElementById(`strip${i}Offset`).value),
                    mode: document.getElementById(`strip${i}Mode`).value,
                    followWidth: parseInt(document.getElementById(`strip${i}FollowWidth`).value),
                    wipeSpeed: parseInt(document.getElementById(`strip${i}WipeSpeed`).value),
                    onTime: parseInt(document.getElementById(`strip${i}OnTime`).value),
                    effect: document.getElementById(`strip${i}Effect`).value,
                    color: document.getElementById(`strip${i}Color`).value,
                    temp: parseInt(document.getElementById(`strip${i}Temp`).value),
                    brightness: parseInt(document.getElementById(`strip${i}Brightness`).value)
                };
            }
            return JSON.stringify(settings);
        }

        function applySettingsFromJson(settings) {
             if (!settings) return; // Provjera ako su postavke null
            document.getElementById('mainMode').value = settings.mainMode || 'auto';
            document.getElementById('stripCount').value = settings.stripCount || 0;

            for (let i = 1; i <= 2; i++) { // Uvijek prođi kroz obje trake
                const strip = settings[`strip${i}`];
                if (strip) {
                    document.getElementById(`strip${i}Type`).value = strip.type || 'none';
                    document.getElementById(`strip${i}Length`).value = strip.length || 4;
                    document.getElementById(`strip${i}Density`).value = strip.density || 60;
                    document.getElementById(`strip${i}Offset`).value = strip.offset || 0;
                    document.getElementById(`strip${i}Mode`).value = strip.mode || 'fill';
                    document.getElementById(`strip${i}FollowWidth`).value = strip.followWidth || 20;
                    document.getElementById(`strip${i}WipeSpeed`).value = strip.wipeSpeed || 50;
                    document.getElementById(`strip${i}OnTime`).value = strip.onTime || 5;
                    document.getElementById(`strip${i}Effect`).value = strip.effect || 'solid';
                    document.getElementById(`strip${i}Color`).value = strip.color || '#0000FF';
                    document.getElementById(`strip${i}Temp`).value = strip.temp || 4000;
                    document.getElementById(`strip${i}Brightness`).value = strip.brightness || 80;
                }
            }
            updateUI();
            ['1', '2'].forEach(num => {
                const tempEl = document.getElementById(`strip${num}Temp`);
                if (tempEl) updateRangeValue(`strip${num}TempValue`, tempEl.value + 'K');
                const brightEl = document.getElementById(`strip${num}Brightness`);
                if (brightEl) updateRangeValue(`strip${num}BrightnessValue`, brightEl.value);
            });
        }

        async function saveSettings() {
            const statusDiv = document.getElementById('status');
            const lang = document.documentElement.lang || 'hr';
            statusDiv.textContent = translations[lang].savingStatus;
            
            const settingsJson = getSettingsAsJson();
            console.log("Slanje postavki:", settingsJson);

            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: settingsJson
                });
                if (response.ok) {
                    statusDiv.textContent = translations[lang].savedStatus;
                } else {
                    throw new Error('Server response was not ok.');
                }
            } catch (error) {
                console.error('Greška:', error);
                statusDiv.textContent = translations[lang].errorStatus;
            } finally {
                setTimeout(() => { statusDiv.textContent = ''; }, 2000);
            }
        }

        async function loadSettings() {
            try {
                const response = await fetch('/get');
                if (!response.ok) { throw new Error('Network response was not ok'); }
                const settings = await response.json();
                console.log("Primljene postavke:", settings);
                applySettingsFromJson(settings);
            } catch (error) {
                console.error('Greška pri učitavanju postavki:', error);
                // Ako ne uspijemo učitati, samo primijeni default UI stanje
                updateUI();
            }
        }
        
        window.onclick = function(event) {
            if (!event.target.matches('.language-selector-btn, .language-selector-btn *')) {
                const menu = document.getElementById("language-menu");
                if (menu.classList.contains('show')) {
                    menu.classList.remove('show');
                    document.getElementById('language-selector-btn').classList.remove('open');
                }
            }
        }

        document.addEventListener('DOMContentLoaded', () => {
            changeLanguage('hr');
            toggleLanguageMenu();
            loadSettings();
        });
    </script>
</body>
</html>
)rawliteral";
// =============================================================================
// === FUNKCIJE ZA SPREMANJE I ČITANJE POSTAVKI (SPIFFS) ========================
// =============================================================================

// Funkcija koja parsira HEX string boju (npr. "#FF00AA") u CRGB objekt
CRGB parseHtmlColor(String colorString) {
  if (colorString.startsWith("#")) {
    colorString = colorString.substring(1);
  }
  long number = strtol(colorString.c_str(), NULL, 16);
  return CRGB(number);
}

// Funkcija koja sprema trenutne postavke u config.json
void saveConfiguration() {
  Serial.println("Spremam konfiguraciju u SPIFFS...");
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Greška: Nije moguće otvoriti config.json za pisanje");
    return;
  }

  JsonDocument doc;
  doc["mainMode"] = mainMode;
  doc["stripCount"] = (strip1_settings.enabled ? 1 : 0) + (strip2_settings.enabled ? 1 : 0);

  if (strip1_settings.enabled) {
    JsonObject strip1 = doc.createNestedObject("strip1");
    strip1["type"] = strip1_settings.type;
    strip1["length"] = strip1_settings.length;
    // ... dodaj sve ostale postavke za strip1
  }
  // ... dodaj logiku i za strip2

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Greška: Nije moguće zapisati u config.json");
  } else {
    Serial.println("Konfiguracija uspješno spremljena.");
  }
  configFile.close();
}

// Funkcija koja čita postavke iz config.json pri paljenju
void loadConfiguration() {
  if (SPIFFS.begin(true)) {
    Serial.println("SPIFFS montiran.");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("Čitam config.json...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, configFile);
        if (error) {
          Serial.println("Greška pri parsiranju JSON-a!");
        } else {
          Serial.println("Konfiguracija uspješno učitana.");
          mainMode = doc["mainMode"] | "auto";
          int stripCount = doc["stripCount"] | 0;

          if (stripCount >= 1 && doc.containsKey("strip1")) {
            strip1_settings.enabled = true;
            JsonObject strip1 = doc["strip1"];
            strip1_settings.type = strip1["type"] | "ws2812b";
            strip1_settings.length = strip1["length"] | 4;
            // ... učitaj sve ostale postavke
          }
           // ... dodaj logiku i za strip2
        }
        configFile.close();
      }
    } else {
      Serial.println("config.json ne postoji, koristim default postavke.");
    }
  } else {
    Serial.println("Greška: Nije moguće montirati SPIFFS.");
  }
}


// Ostale funkcije (signalAPActive, handle... itd.)
void signalAPActive() { /* ... */ }

void handleRoot() {
  server.send_P(200, "text/html", HTML_PROGMEM);
}

void handleGet() {
  JsonDocument doc;
  doc["mainMode"] = mainMode;
  int stripCount = 0;
  if (strip1_settings.enabled) stripCount++;
  if (strip2_settings.enabled) stripCount++;
  doc["stripCount"] = stripCount;

  if (strip1_settings.enabled) {
    JsonObject strip1 = doc.createNestedObject("strip1");
    strip1["type"] = strip1_settings.type;
    strip1["length"] = strip1_settings.length;
    strip1["density"] = strip1_settings.density;
    // ... dodaj sve ostale
  }
   if (strip2_settings.enabled) {
    // ... dodaj sve za strip2
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSave() {
  if (server.hasArg("plain") == false) { 
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.println("Greška pri parsiranju JSON-a sa stranice!");
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  mainMode = doc["mainMode"] | "auto";
  int stripCount = doc["stripCount"] | 0;

  strip1_settings.enabled = (stripCount >= 1);
  strip2_settings.enabled = (stripCount == 2);

  if (strip1_settings.enabled) {
      JsonObject strip1 = doc["strip1"];
      strip1_settings.type = strip1["type"] | "ws2812b";
      strip1_settings.length = strip1["length"] | 4;
      strip1_settings.density = strip1["density"] | 60;
      strip1_settings.numLeds = strip1_settings.length * strip1_settings.density;
      if (strip1_settings.numLeds > MAX_LEDS) strip1_settings.numLeds = MAX_LEDS;
      // ... spremi sve ostale
  }
  // ... spremi sve ostale za strip2

  FastLED.setBrightness(strip1_settings.brightness); // Za sada samo za traku 1
  
  saveConfiguration();
  server.send(200, "text/plain", "OK");
}


// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- Pokretanje Sistema v2.2 ---");

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  
  loadConfiguration(); // Učitaj spremljene postavke

  FastLED.addLeds<WS2812B, LED_PIN_A, GRB>(ledsA, strip1_settings.numLeds > 0 ? strip1_settings.numLeds : 1);
  FastLED.addLeds<WS2812B, LED_PIN_B, GRB>(ledsB, strip2_settings.numLeds > 0 ? strip2_settings.numLeds : 1);
  FastLED.setBrightness(strip1_settings.brightness); // Postavi učitanu svjetlinu
  FastLED.clear(true);
  FastLED.show();

  SensorSerialB.begin(115200, SERIAL_8N1, RX_PIN_B, TX_PIN_B);
  
  WiFi.softAP(ssid);
  dnsServer.start(53, "*", WiFi.softAPIP());
  signalAPActive();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/get", HTTP_GET, handleGet);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([](){ server.send(404, "text/plain", "Not Found"); });
  
  server.begin();
  Serial.println("OK: Web server pokrenut. Sustav je spreman.");
}
// =============================================================================

// =============================================================================
void loop() {
  esp_task_wdt_reset();
  server.handleClient();
  dnsServer.processNextRequest();

  // Ovdje će ići provjera za mainMode
  if (mainMode == "off") {
      FastLED.clear();
      FastLED.show();
      return; // Ne radi ništa drugo
  }

  if (mainMode == "on") {
      // Logika za "Uvijek upaljeno"
      // Npr. fill_solid(ledsB, strip1_settings.numLeds, strip1_settings.color);
  }
  
  if (mainMode == "auto" && strip1_settings.enabled) { // Za sada samo za traku 1
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

      int ledsToLight = 0;
      if (millis() - lastMotionTimeB < (strip1_settings.onTime * 1000UL)) {
          ledsToLight = map(currentDistanceB, 0, strip1_settings.length * 100, 0, strip1_settings.numLeds);
          ledsToLight = constrain(ledsToLight, 0, strip1_settings.numLeds); 
      }
      
      for (int i = 0; i < strip1_settings.numLeds; i++) {
        ledsB[i] = (i < ledsToLight) ? strip1_settings.color : CRGB::Black;
      }
  }

  if(!strip2_settings.enabled){
    for (int i = 0; i < MAX_LEDS; i++) {
      ledsA[i] = CRGB::Black;
    }
  }
  // Dodati logiku za strip2...

  FastLED.show();
  delay(10);
}
// =============================================================================

