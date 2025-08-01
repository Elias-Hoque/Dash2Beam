// Bibliotheken einbinden, die für WLAN, Webserver, IR-Sender, OLED-Display und Tastenfeld nötig sind
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <IRremote.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

// OLED-Display definieren
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins für Drehencoder definieren
#define encoderCLK 15
#define encoderDT 12

// WLAN-Zugangsdaten
const char* ssid = "o2-WLAN71";
const char* password = "4884869935669253";

// Aufbau der Tastenmatrix (3x3) mit den zugehörigen Zeichen
const byte ROWS = 3;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' }
};

// Zuordnung der Pins für Zeilen und Spalten
byte rowPins[ROWS] = { 16, 9, 2 };
byte colPins[COLS] = { 14, 12, 13 };

// Keypad initialisieren
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Webserver auf Port 80 erstellen
ESP8266WebServer server(80);

// Pin für Infrarot-Sender und Freeze-LED
int SenderPin = 15;
const int freezeLedPin = 0;

// Lautstärkewerte
int volume = 0;
int maxVolume = 100;

// letzter Status des Encoders (zur Änderungserkennung)
int lastEncoderState = HIGH;

// Statusvariablen
bool LEDstatus = LOW;
bool freezeStatus = false;
float batteryVoltage = 0.0;

// Akkuspannung messen und berechnen
void updateBatteryStatus() {
  int raw = analogRead(A0);               // Rohwert (0–1023)
  batteryVoltage = (raw / 1023.0) * 5.0;  // Umrechnung auf Spannung (mit Faktor 5 wegen Spannungsteiler)
}

// Anzeige aktualisieren (OLED)
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.print("WiFi: ");
  display.println(WiFi.softAPIP());

  display.print("Freeze: ");
  display.println(freezeStatus ? "AN" : "AUS");

  display.print("Akku: ");
  display.print(batteryVoltage, 2);
  display.println(" V");

  // Warnung bei niedriger Spannung
  if (batteryVoltage < 3.3) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.println("!!! LOW BATTERY !!!");
    display.setTextColor(SSD1306_WHITE);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);  // Serielle Ausgabe starten

  // Pins und IR-Sender konfigurieren
  pinMode(SenderPin, OUTPUT);
  pinMode(encoderCLK, INPUT_PULLUP);
  pinMode(encoderDT, INPUT_PULLUP);
  pinMode(freezeLedPin, OUTPUT);
  IrSender.begin(SenderPin);

  // WLAN-Verbindung aufbauen
  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Verbunden! IP-Adresse: ");
  Serial.println(WiFi.localIP());

  // mDNS starten (damit man die IP über Namen erreichen kann)
  if (MDNS.begin("projektor")) {
    Serial.println("mDNS gestartet: http://projektor.local");
  } else {
    Serial.println("mDNS NICHT gestartet");
  }

  // OLED-Display starten
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;)
      ;  // bleibt hängen, wenn Display nicht geht
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED gestartet");
  display.display();
  delay(1000);

  // Webserver-Routen festlegen
  server.on("/", handle_OnConnect);
  server.on("/source", handle_source);
  server.on("/onoff", handle_onoff);
  server.on("/freeze", handle_freeze);
  server.on("/hoch", handle_hoch);
  server.on("/rechts", handle_rechts);
  server.on("/links", handle_links);
  server.on("/runter", handle_runter);
  server.on("/lauter", handle_lauter);
  server.on("/leiser", handle_leiser);
  server.on("/ok", handle_ok);
  server.onNotFound(handle_NotFound);

  server.begin();  // Server starten
  Serial.println("HTTP Server gestartet");
}

void loop() {
  updateBatteryStatus();  // Akkustand aktualisieren

  // Eingaben über Keypad prüfen
  char key = keypad.getKey();
  if (key) {
    if (key == '1') handle_source();
    else if (key == '2') handle_onoff();
    else if (key == '3') {
      handle_freeze();
      freezeStatus = !freezeStatus;
      updateDisplay();
    } else if (key == '4') handle_hoch();
    else if (key == '5') handle_runter();
    else if (key == '6') handle_ok();
    else if (key == '7') handle_links();
    else if (key == '8') handle_rechts();
    else if (key == '9') handle_lauter();
  }

  // Drehencoder auswerten
  int currentEncoderState = digitalRead(encoderCLK);
  if (currentEncoderState != lastEncoderState && currentEncoderState == LOW) {
    if (digitalRead(encoderDT) != currentEncoderState) {
      handle_lauter();
    } else {
      handle_leiser();
    }
  }
  lastEncoderState = currentEncoderState;

  MDNS.update();          // mDNS aktuell halten
  server.handleClient();  // Webserver-Verbindung abarbeiten
  updateDisplay();        // Display regelmäßig aktualisieren
}

// Webserver: Startseite anzeigen
void handle_OnConnect() {
  server.send(200, "text/html", updateWebpage(LEDstatus, freezeStatus));
}

// Funktionen für IR-Befehle (z. B. Quelle wählen, Gerät an/aus, Freeze usw.)
void handle_source() {
  IrSender.sendNEC(0x5583, 0x8C, 1);
  delay(700);
}

void handle_onoff() {
  IrSender.sendNEC(0x5583, 0x90, 1);
  delay(700);
}

void handle_freeze() {
  IrSender.sendNEC(0x5583, 0x92, 1);
  freezeStatus = !freezeStatus;
  delay(700);
  server.send(200, "text/html", updateWebpage(LEDstatus, freezeStatus));
  IrSender.sendNEC(0x5583, 0x92, 1);
  delay(700);
  freezeStatus = !freezeStatus;
  digitalWrite(freezeLedPin, freezeStatus ? HIGH : LOW);
}

void handle_hoch() {
  IrSender.sendNEC(0x5583, 0xB0, 1);
  delay(700);
}

void handle_rechts() {
  IrSender.sendNEC(0x5583, 0xB1, 1);
  delay(700);
}

void handle_links() {
  IrSender.sendNEC(0x5583, 0xB2, 1);
  delay(700);
}

void handle_runter() {
  IrSender.sendNEC(0x5583, 0xB3, 1);
  delay(700);
}

void handle_lauter() {
  digitalWrite(4, HIGH);  // LED oder anderes Feedback
  IrSender.sendNEC(0x5583, 0x85, 1);
  delay(700);
}

void handle_leiser() {
  digitalWrite(4, HIGH);
  IrSender.sendNEC(0x5583, 0x98, 1);
  delay(700);
}

void handle_ok() {
  digitalWrite(4, HIGH);
  IrSender.sendNEC(0x5583, 0x99, 1);
  delay(700);
}

// 404-Seite bei nicht existierender Route
void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// HTML-Webseite erzeugen, zeigt z. B. Freeze-Status und Buttons
String updateWebpage(uint8_t LEDstatus, bool freezeStatus) {
  // Liefert dynamische HTML-Seite zurück, angepasst an den Status
  String ptr = "<!DOCTYPE html><html lang=\"de\"><head>";
  ptr += "<meta charset=\"UTF-8\"/>";
  ptr += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/>";
  ptr += "<title>Dash2Beam - Room 405</title>";
  ptr += "<style>";
  ptr += ":root{--primary-color:rgba(139,69,19,0.7);--primary-color-hover:rgba(139,69,19,0.8);--primary-color-shadow:rgba(139,69,19,0.2);--primary-color-shadow-hover:rgba(139,69,19,0.3);--primary-color-shadow-large:rgba(139,69,19,0.25);--primary-color-shadow-large-hover:rgba(139,69,19,0.35)}";
  ptr += "*{margin:0;padding:0;box-sizing:border-box}";
  ptr += "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:#ffffff;min-height:100vh;display:flex;flex-direction:column;color:#333333}";
  ptr += ".header{position:relative;text-align:center;padding:20px;background:#ffffff;border-bottom:2px solid #f0f0f0;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
  ptr += ".header-left{position:absolute;left:20px;top:50%;transform:translateY(-50%);font-family:'Impact','Arial Black',sans-serif;font-size:24px;font-weight:400;color:#000000;letter-spacing:2px}";
  ptr += ".header-right{position:absolute;right:20px;top:50%;transform:translateY(-50%)}";
  ptr += ".logo{height:50px;width:auto;filter:drop-shadow(2px 2px 4px rgba(0,0,0,0.1))}";
  ptr += "h1{font-size:32px;font-weight:700;margin-bottom:8px;color:#000000}";
  ptr += ".subtitle{font-size:16px;color:#666666;font-weight:300}";
  ptr += ".status-container{padding:15px;text-align:center}";
  ptr += ".status-box{display:inline-block;padding:12px 24px;border-radius:25px;font-size:18px;font-weight:600;transition:all 0.3s ease;border:2px solid #ddd;color:white}";
  ptr += ".status-inactive{background:rgba(46,204,113,0.8);box-shadow:0 4px 15px rgba(46,204,113,0.3)}";
  ptr += ".status-active{background:rgba(231,76,60,0.9);box-shadow:0 4px 15px rgba(231,76,60,0.4);animation:pulse 2s infinite}";
  ptr += "@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.05)}}";
  ptr += ".button-grid{flex:1;display:grid;grid-template-columns:1fr 1fr 1fr;grid-template-rows:auto auto auto auto;gap:20px;padding:20px;max-width:1200px;margin:0 auto;width:100%;grid-template-areas:'power freeze freeze' 'standby freeze freeze' 'dpad source volume' '. . .'}";
  ptr += ".button{padding:25px;font-size:22px;font-weight:600;border:none;border-radius:15px;cursor:pointer;color:white;text-decoration:none;display:flex;align-items:center;justify-content:center;transition:all 0.3s ease;background:var(--primary-color);box-shadow:0 4px 15px var(--primary-color-shadow);min-height:50px;text-shadow:1px 1px 2px rgba(0,0,0,0.3)}";
  ptr += ".button:hover{transform:translateY(-2px);box-shadow:0 6px 20px var(--primary-color-shadow-hover);background:var(--primary-color-hover)}";
  ptr += ".button:active{transform:translateY(0);box-shadow:0 2px 10px var(--primary-color-shadow-large)}";
  ptr += ".button-large{font-size:28px;min-height:100px;font-weight:700;background:var(--primary-color);box-shadow:0 6px 25px var(--primary-color-shadow-large)}";
  ptr += ".button-large:hover{background:var(--primary-color-hover);box-shadow:0 8px 30px var(--primary-color-shadow-large-hover)}";
  ptr += ".button-power{grid-area:power}";
  ptr += ".button-standby{grid-area:standby}";
  ptr += ".button-freeze{grid-area:freeze;grid-row:1/3}";
  ptr += ".button-source{grid-area:source}";
  ptr += ".dpad-container{grid-area:dpad;display:flex;align-items:center;justify-content:center}";
  ptr += ".dpad{position:relative;width:200px;height:200px;display:grid;grid-template-areas:'. up .' 'left ok right' '. down .';grid-template-columns:1fr 1fr 1fr;grid-template-rows:1fr 1fr 1fr;gap:5px}";
  ptr += ".dpad-btn{background:var(--primary-color);border:none;border-radius:8px;color:white;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.2s ease;display:flex;align-items:center;justify-content:center;text-shadow:1px 1px 2px rgba(0,0,0,0.3)}";
  ptr += ".dpad-btn:hover{background:var(--primary-color-hover);transform:scale(1.05)}";
  ptr += ".dpad-btn:active{transform:scale(0.95)}";
  ptr += ".dpad-up{grid-area:up}";
  ptr += ".dpad-down{grid-area:down}";
  ptr += ".dpad-left{grid-area:left}";
  ptr += ".dpad-right{grid-area:right}";
  ptr += ".dpad-ok{grid-area:ok;border-radius:50%;font-size:18px;font-weight:700}";
  ptr += ".volume-container{grid-area:volume;display:flex;flex-direction:row;align-items:center;justify-content:center;gap:20px}";
  ptr += ".volume-display{display:flex;align-items:center;gap:15px}";
  ptr += ".volume-bar{width:20px;height:200px;background:#f0f0f0;border-radius:10px;border:2px solid #333;overflow:hidden;position:relative}";
  ptr += ".volume-fill{position:absolute;bottom:0;width:100%;background:#000000;height:";
  ptr += String((volume * 100) / maxVolume);
  ptr += "%;transition:height 0.3s ease;border-radius:8px}";
  ptr += ".volume-controls{display:flex;flex-direction:column;gap:10px}";
  ptr += ".volume-btn{background:var(--primary-color);border:none;border-radius:8px;color:white;font-size:18px;font-weight:600;cursor:pointer;transition:all 0.2s ease;padding:20px 40px;text-shadow:1px 1px 2px rgba(0,0,0,0.3);min-width:140px}";
  ptr += ".volume-btn:hover{background:var(--primary-color-hover);transform:translateY(-1px)}";
  ptr += ".freeze-active{background:rgba(231,76,60,0.9)!important;box-shadow:0 8px 32px rgba(231,76,60,0.3)!important;animation:buttonPulse 1.5s infinite}";
  ptr += "@keyframes buttonPulse{0%,100%{box-shadow:0 8px 32px rgba(231,76,60,0.3)}50%{box-shadow:0 12px 40px rgba(231,76,60,0.5)}}";
  ptr += ".freeze-active:hover{background:rgba(231,76,60,1)!important;transform:translateY(-2px)}";
  ptr += "@media (max-width:768px){.button-grid{grid-template-columns:1fr;grid-template-areas:'power' 'standby' 'freeze' 'dpad' 'source' 'volume';gap:15px;padding:15px}.button{font-size:18px;padding:20px;min-height:70px}.button-large{font-size:22px;min-height:80px}.button-freeze{grid-row:auto}.dpad{width:150px;height:150px}.volume-bar{width:15px;height:150px}.volume-btn{padding:15px 30px;min-width:120px}h1{font-size:28px}.header-left{font-size:18px;left:10px}.logo{height:40px}.header-right{right:10px}}";
  ptr += "@media (max-width:480px){.dpad{width:120px;height:120px}.volume-bar{width:12px;height:120px}.volume-btn{padding:12px 25px;min-width:100px;font-size:16px}.header-left{position:static;transform:none;margin-bottom:10px;font-size:16px}.header-right{position:static;transform:none;margin-top:10px}.header{padding:15px}}";
  ptr += "</style></head><body>";

  ptr += "<div class=\"header\">";
  ptr += "<div class=\"header-left\">Droste X FG</div>";
  ptr += "<h1>Dash2Beam – Room 405</h1>";
  ptr += "<p class=\"subtitle\">created by Elias Hoque KS1</p>";
  ptr += "<div class=\"header-right\">";
  ptr += "<img src=\"https://upload.wikimedia.org/wikipedia/commons/f/f5/Logo_Friedrich-Gymnasium_Freiburg_01.png\" alt=\"FG Logo\" class=\"logo\">";
  ptr += "</div>";
  ptr += "</div>";

  ptr += "<div class=\"status-container\">";
  ptr += "<div class=\"status-box ";
  ptr += freezeStatus ? "status-active" : "status-inactive";
  ptr += "\">Freeze Status: ";
  ptr += freezeStatus ? "AKTIV" : "AUS";
  ptr += "</div>";
  ptr += "</div>";

  ptr += "<div class=\"button-grid\">";
  ptr += "<a class=\"button button-power\" href=\"/onoff\">POWER</a>";
  ptr += "<a class=\"button button-standby\" href=\"/standby\">STANDBY</a>";
  ptr += "<a class=\"button button-large button-freeze ";
  ptr += freezeStatus ? "freeze-active" : "";
  ptr += "\" href=\"/freeze\">FREEZE</a>";

  ptr += "<div class=\"dpad-container\">";
  ptr += "<div class=\"dpad\">";
  ptr += "<a class=\"dpad-btn dpad-up\" href=\"/hoch\">▲</a>";
  ptr += "<a class=\"dpad-btn dpad-left\" href=\"/links\">◄</a>";
  ptr += "<a class=\"dpad-btn dpad-ok\" href=\"/ok\">OK</a>";
  ptr += "<a class=\"dpad-btn dpad-right\" href=\"/rechts\">►</a>";
  ptr += "<a class=\"dpad-btn dpad-down\" href=\"/runter\">▼</a>";
  ptr += "</div>";
  ptr += "</div>";

  ptr += "<a class=\"button button-source\" href=\"/source\">Source</a>";

  ptr += "<div class=\"volume-container\">";
  ptr += "<div class=\"volume-display\">";
  ptr += "<div class=\"volume-bar\">";
  ptr += "<div class=\"volume-fill\"></div>";
  ptr += "</div>";
  ptr += "</div>";
  ptr += "<div class=\"volume-controls\">";
  ptr += "<a class=\"volume-btn\" href=\"/lauter\">Lauter ▲</a>";
  ptr += "<a class=\"volume-btn\" href=\"/leiser\">Leiser ▼</a>";
  ptr += "</div>";
  ptr += "</div>";

  ptr += "</div>";
  ptr += "</body></html>";

  return ptr;
}

// Alternative Funktion zur Akkumessung (mit anderem Spannungsteiler)
float readBatteryVoltage() {
  int raw = analogRead(A0);
  float voltage = (raw / 1023.0) * 1.0;  // Maximal 1.0 V am Pin
  return voltage * 6.0;                  // Umrechnen auf echte Akkuspannung
}
