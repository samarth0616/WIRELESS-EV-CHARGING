
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define TRIG_PIN  D5   // GPIO14
#define ECHO_PIN  D6   // GPIO12 (after voltage divider)
#define RELAY_PIN D7   // GPIO13 (LOW trigger)
#define BUZZER_PIN D8  // GPIO15

LiquidCrystal_I2C lcd(0x27, 16, 2);
ESP8266WebServer server(80);

// CONFIG
const char* AP_SSID = "EV_Charge_AP";
const char* AP_PASS = "evcharger123";
const char* UPI_ID   = "yourupiid@bank"; // <-- REPLACE
const float COST_PER_MIN = 5.0; // Rs 5 per minute
const unsigned int DIST_THRESHOLD_CM = 30; // vehicle detected if <= 30cm
const unsigned long STOP_GRACE_MS = 3000; // 3s grace
const unsigned long WELCOME_MS = 2000;    // 2s welcome screen
const unsigned long THANKYOU_MS = 2000;   // 2s thank you screen

// STATE
bool charging = false;
unsigned long chargeStartMs = 0;
unsigned long elapsedSec = 0;
float lastCost = 0.0;
String lastUpiUrl = "";

// Helper: read HC-SR04 distance (cm). returns -1 on timeout/no-echo.
long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long dur = pulseIn(ECHO_PIN, HIGH, 30000); // max 30ms
  if (dur == 0) return -1;
  long cm = dur / 58; // approximate conversion
  return cm;
}

// Relay helpers: relay is LOW-trigger
void relayOn()  { digitalWrite(RELAY_PIN, LOW); }
void relayOff() { digitalWrite(RELAY_PIN, HIGH); }

// LCD screens
void lcdWelcome(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("  WELCOME TO  ");
  lcd.setCursor(0,1); lcd.print(" EV CHARGING  ");
  delay(WELCOME_MS);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("System Ready");
  lcd.setCursor(0,1); lcd.print("Waiting...");
}

void lcdCharging() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("CHARGING...");
  lcd.setCursor(0,1); lcd.print("Time: 00:00");
}

void lcdThankYou(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Charging Ended");
  lcd.setCursor(0,1); lcd.print("Thank You!");
  delay(THANKYOU_MS);
  lcdWelcome();
}

// Lightweight HTML pages (no heavy JS). main page auto-refresh via meta=1
String mainPage() {
  unsigned long elapsed = charging ? (millis() - chargeStartMs) / 1000 : 0;
  unsigned int h = elapsed / 3600;
  unsigned int m = (elapsed % 3600) / 60;
  unsigned int s = elapsed % 60;
  char tbuf[16];
  sprintf(tbuf, "%02u:%02u:%02u", h, m, s);

  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='1'>";
  html += "<title>EV Charging</title></head><body style='font-family:Arial;text-align:center;padding:18px'>";
  html += "<h1 style='color:#1e88e5'>EV CHARGING</h1>";
  html += "<p>Status: <b>" + String(charging ? "CHARGING" : "WAITING") + "</b></p>";
  html += "<p style='font-size:28px;color:#e53935;'>" + String(tbuf) + "</p>";
  if(!charging && lastCost > 0.0) {
    String qr = "https://api.qrserver.com/v1/create-qr-code/?size=260x260&data=" + urlEncode(lastUpiUrl);
    char buf[200];
    snprintf(buf, sizeof(buf), "<p>Amount: Rs %.2f</p><p><img src='%s'></p>", lastCost, qr.c_str());
    html += String(buf);
  }
  html += "<p><a href='/pay'>Pay</a></p>";
  html += "</body></html>";
  return html;
}

String payPage() {
  if (lastCost <= 0.0) return "<html><body><p>No payment due</p><p><a href='/'>Back</a></p></body></html>";
  String q = "https://api.qrserver.com/v1/create-qr-code/?size=300x300&data=" + urlEncode(lastUpiUrl);
  char buf[256];
  snprintf(buf, sizeof(buf), "<html><body style='text-align:center;font-family:Arial'><h3>Pay Rs %.2f</h3><img src='%s'><p><a href='/'>Back</a></p></body></html>", lastCost, q.c_str());
  return String(buf);
}

String urlEncode(const String &str) {
  String encoded = "";
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') encoded += c;
    else { sprintf(buf, "%%%02X", (unsigned char)c); encoded += buf; }
  }
  return encoded;
}

// HTTP handlers
void handleRoot(){ server.send(200, "text/html", mainPage()); }
void handlePay(){ server.send(200, "text/html", payPage()); }

//////////////////////////////////////////////////////////////////////////
// Setup
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  relayOff();
  digitalWrite(BUZZER_PIN, LOW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcdWelcome();

  // AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  Serial.println();
  Serial.print("AP SSID: "); Serial.println(AP_SSID);
  Serial.print("AP IP: "); Serial.println(myIP);

  // server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/pay", HTTP_GET, handlePay);
  server.begin();
  Serial.println("HTTP server started (AP mode).");
  Serial.print("Open: http://"); Serial.println(myIP);
}

//////////////////////////////////////////////////////////////////////////
// loop logic
unsigned long lastStableMs = 0;
int lastPresence = 0; // 0 = no vehicle, 1 = present
unsigned long missingSince = 0;

void loop() {
  server.handleClient();

  long dist = readDistanceCm(); // -1 if no echo
  Serial.print("Distance: "); Serial.println(dist); // debug reading
  bool present = (dist > 0 && dist <= (long)DIST_THRESHOLD_CM); // vehicle detected if <=30 cm

  // simple debounce
  if (present != (lastPresence==1)) {
    lastStableMs = millis();
    lastPresence = present ? 1 : 0;
  }

  if (millis() - lastStableMs > 80) { // stable reading
    if (present && !charging) {
      // start charging
      charging = true;
      chargeStartMs = millis();
      relayOn(); // LOW triggers relay
      digitalWrite(BUZZER_PIN, HIGH); delay(120); digitalWrite(BUZZER_PIN, LOW);
      lcdWelcome();     // show welcome every time
      lcdCharging();
      lastCost = 0.0; lastUpiUrl = "";
      Serial.println("Charging started.");
      missingSince = 0;
    } else if (!present && charging) {
      if (missingSince == 0) missingSince = millis();
      if (millis() - missingSince > STOP_GRACE_MS) {
        // stop charging
        charging = false;
        relayOff();
        unsigned long mins = (millis() - chargeStartMs) / 60000UL;
        if (mins == 0) mins = 1;
        lastCost = (float)mins * COST_PER_MIN;
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "upi://pay?pa=%s&am=%.2f&cu=INR", UPI_ID, lastCost);
        lastUpiUrl = String(tmp);
        digitalWrite(BUZZER_PIN, HIGH); delay(160); digitalWrite(BUZZER_PIN, LOW);
        lcdThankYou();   // show thank you
        chargeStartMs = 0;
        missingSince = 0;
        Serial.printf("Charging stopped. %lu min | Rs %.2f\n", mins, lastCost);
      }
    }
  }

  // update elapsed for display
  if (charging) {
    elapsedSec = (millis() - chargeStartMs) / 1000;

    // live LCD timer
    unsigned int h = elapsedSec / 3600;
    unsigned int m = (elapsedSec % 3600) / 60;
    unsigned int s = elapsedSec % 60;
    char tbuf[16];
    sprintf(tbuf, "%02u:%02u:%02u", h, m, s);
    lcd.setCursor(0,1);
    lcd.print("Time: "); lcd.print(tbuf);
  } else {
    elapsedSec = 0;
  }

  delay(20);
}
