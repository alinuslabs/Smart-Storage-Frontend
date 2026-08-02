// IMPORTS
#include <Arduino.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <RTClib.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>

// WIFI CONFIG (Access Point mode — the ESP32 creates its own network)
// Anyone can connect to this network + open the device's IP in a browser,
// no code access needed. Change these to whatever you like.
// Note: WPA2 requires the password to be at least 8 characters.
const char* ap_ssid = "SmartStorage";
const char* ap_password = "storage123";

// SERVER OBJECT
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// TIMING
unsigned long lastSend = 0;

// SYSTEM LIMIT
const float min_temp = 15;
const float max_temp = 20;
const float min_humidity = 85;
const float max_humidity = 93;

// PIN
int servoPin = 14;
int dhtPin = 13;
int fanPin = 27; //IN 1
int peltierPin = 12; //IN 2
int humidifierPin = 25; //IN 3
int ledPin = 2;
int extDhtPin = 26;

// STATE VARIABLE
String servoState = "0";
String fanState = "0";
String peltierState = "0";
String humidifierState = "0";
bool reset = false;

// AUTO MODE — IDENTIFIER / REASON TRACKIN
// Per-actuator "why" string, e.g. "humidity>95%". Cleared in manual mode.
String ventReason = "";
String fanReason = "";
String peltierReason = "";
String humidifierReason = "";

// Combined, human-readable summary of what's currently active in auto mode.
String autoSummary = "";
// COMPONENT
Servo servoVent;
RTC_DS3231 rtc;
DHT dht(dhtPin, DHT22);
DHT extDht(extDhtPin, DHT22);

LiquidCrystal_I2C lcd(0x27, 20, 4);

// JSON BUFFER
JsonDocument jsonDoc;

// MODE CONTROL
String mode = "auto";

// WEBSOCKET SEND
void notifyClients(String message){
  ws.textAll(message);
}

// HANDLE CLIENT MESSAGE
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len){

  AwsFrameInfo *info = (AwsFrameInfo*)arg;

  if(info->final && info->opcode == WS_TEXT){

    data[len] = 0;
    String msg = (char*)data;

    Serial.println("CMD: " + msg);


    // OPTIONAL COMMAND CONTROL
    if(msg == "fan_on"){
      digitalWrite(fanPin, HIGH);
    }

    if(msg == "fan_off"){
      digitalWrite(fanPin, LOW);
    }

    if(msg == "vent_open"){
      servoVent.write(90);
    }

    if(msg == "vent_close"){
      servoVent.write(0);
    }

    if(msg == "peltier_on"){
      digitalWrite(peltierPin, HIGH);
    }

    if(msg == "peltier_off"){
      digitalWrite(peltierPin, LOW);
    }

    if(msg == "humidifier_on"){
      digitalWrite(humidifierPin, HIGH);
    }

    if(msg == "humidifier_off"){
      digitalWrite(humidifierPin, LOW);
    }

    if(msg == "auto"){
      // Implement auto mode logic here
      mode = "auto";
    }

    if(msg == "manual"){
      // Implement manual mode logic here
      mode = "manual";
      // Manual override: reasons no longer apply, clear them so the
      // frontend's Auto Status panel doesn't show stale info next time.
      ventReason = "";
      fanReason = "";
      peltierReason = "";
      humidifierReason = "";
      autoSummary = "";
    }

    notifyClients("ACK: " + msg);
  }
}

// WEBSOCKET EVENT
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
  if(type == WS_EVT_DATA){
    handleWebSocketMessage(arg, data, len);
  }
}

// SETUP
void setup(){

  Serial.begin(9600);
  delay(1000);

  // SERVO
  servoVent.attach(servoPin);
  servoVent.write(0);

  // FAN
  pinMode(fanPin, OUTPUT);
  digitalWrite(fanPin, LOW);

  // PELTIER
  pinMode(peltierPin, OUTPUT);
  digitalWrite(peltierPin, LOW);

  // HUMIDIFIER
  pinMode(humidifierPin, OUTPUT);
  digitalWrite(humidifierPin, LOW);

  // LED
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  
  // DHT
  dht.begin();
  extDht.begin();

  // I2C (RTC)
  Wire.begin(21, 22); // SDA -> 21, SCL -> 22 pins for ESP32

  // LCD
  lcd.init();
  lcd.backlight();

  if(!rtc.begin()){
    Serial.println("RTC missing");
    lcd.setCursor(0, 0);
    lcd.print("RTC missing");
    while(1);
  }

  if(rtc.lostPower()){
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // WIFI — start our own Access Point instead of joining an existing network.
  // Devices connect to this network directly (no code access required),
  // then browse to the IP printed below (typically 192.168.4.1).
  lcd.setCursor(0, 0);
  lcd.print("Starting AP...");

  WiFi.softAP(ap_ssid, ap_password);
  IPAddress apIP = WiFi.softAPIP();

  Serial.println("\nAccess Point started");
  Serial.println("SSID: " + String(ap_ssid));
  Serial.println("AP IP address: " + apIP.toString());

  for (int i = 0; i < 4; i++)
  {
    lcd.setCursor(0, i);
    lcd.print("                    ");
  }
  
  // WEBSOCKET
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.begin();
}

// LOOP
void loop(){

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  float extTemp = extDht.readTemperature();
  float extHum = extDht.readHumidity();
  DateTime now = rtc.now();

  // CONTROL LOGIC 
  if(mode == "auto"){
    // Turn on LED Indicator
    digitalWrite(ledPin, HIGH);
    
    if(temp < min_temp){
      // Serial.println("Temp low");
      digitalWrite(peltierPin, LOW);
      digitalWrite(fanPin, HIGH);

      String reason = "temp < " + String(min_temp) + "C";
      peltierReason = reason;
      fanReason = reason;
    }
    else if(temp > max_temp){
      // Serial.println("Temp high");
      digitalWrite(peltierPin, HIGH);
      digitalWrite(fanPin, LOW);

      String reason = "temp > " + String(max_temp) + "C";
      peltierReason = reason;
      fanReason = reason;
    }
    // else: temp within range — leave fan/peltier and their reasons as-is
    // (matches original behavior: no explicit off-switch on "in range")

    if(hum < min_humidity){
      servoVent.write(0);

      // Turn off Fan
      digitalWrite(fanPin, LOW);

      // Turn on Humidifier
      digitalWrite(humidifierPin, HIGH);

      String reason = "humidity < " + String(min_humidity) + "%";
      humidifierReason = reason;
      ventReason = reason;
    }
    else if(hum > max_humidity){
      servoVent.write(90);

      // Turn on Fan
      digitalWrite(fanPin, HIGH);

      // Turn off Humidifier
      digitalWrite(humidifierPin, LOW);

      String reason = "humidity > " + String(max_humidity) + "%";
      humidifierReason = reason;
      ventReason = reason;
    }
    // else: humidity within range — leave vent/humidifier and their
    // reasons as-is (matches original behavior)
  }
  else {
    // Turn off LED Indicator
    digitalWrite(ledPin, LOW);

    // Manual mode: auto reasons don't apply.
    ventReason = "";
    fanReason = "";
    peltierReason = "";
    humidifierReason = "";
  }
  

  // LOGGING
  String date = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year());
  String time = String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());

  servoState = (servoVent.read() > 0) ? "1" : "0";
  fanState = (digitalRead(fanPin) == HIGH) ? "1" : "0";
  peltierState = (digitalRead(peltierPin) == HIGH) ? "1" : "0";
  humidifierState = (digitalRead(humidifierPin) == HIGH) ? "1" : "0";

  // AUTO SUMMARY — single combined line for the frontend's log strip,
  // e.g. "Humidifier: humidity<85%  Fan: temp>20C"
  autoSummary = "";
  if(mode == "auto"){
    if(servoState == "1") autoSummary += "Vent: " + ventReason + " \n";
    if(fanState == "1") autoSummary += "Fan: " + fanReason + " \n";
    if(peltierState == "1") autoSummary += "Peltier: " + peltierReason + " \n";
    if(humidifierState == "1") autoSummary += "Humidifier: " + humidifierReason + " \n";

    if(autoSummary == ""){
      autoSummary = "All readings within range - no actuators active";
    }
  }

  // JSON STREAM TO FRONTEND
  jsonDoc["date"] = date;
  jsonDoc["time"] = time;
  jsonDoc["temperature"] = temp;
  jsonDoc["humidity"] = hum;
  jsonDoc["external_temperature"] = extTemp;
  jsonDoc["external_humidity"] = extHum;
  jsonDoc["mode"] = mode;
  jsonDoc["servoState"] = servoState;
  jsonDoc["fanState"] = fanState;
  jsonDoc["peltierState"] = peltierState;
  jsonDoc["humidifierState"] = humidifierState;
  jsonDoc["ventReason"] = ventReason;
  jsonDoc["fanReason"] = fanReason;
  jsonDoc["peltierReason"] = peltierReason;
  jsonDoc["humidifierReason"] = humidifierReason;
  jsonDoc["autoSummary"] = autoSummary;

  String json;
  serializeJson(jsonDoc, json);

  ws.cleanupClients();

  // Send updates to clients every (value/1000) second
  if(millis() - lastSend > 1000){
    lastSend = millis();
    notifyClients(json);
  }
  
  // LCD DISPLAY
  lcd.setCursor(0, 0);
  lcd.print("IP: " + WiFi.softAPIP().toString());
  lcd.setCursor(0, 1);
  lcd.print("T:" + String(temp) + "C  H:" + String(hum) + "%");
  lcd.setCursor(0, 2);
  lcd.print("Mode: " + mode);
  if (mode == "auto"){ // to prevent the lcd from printing autoal (manual -> autoal)
    lcd.setCursor(10, 2);
    lcd.print("  ");
  }
  lcd.setCursor(0, 3);
  lcd.print(" V:" + servoState + "  F:" + fanState + "  P:" + peltierState + "  H:" + humidifierState);
}
