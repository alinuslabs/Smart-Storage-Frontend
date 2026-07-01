// ===============================
// IMPORTS
// ===============================
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
#include <ArduinoJson.h>

// ===============================
// WIFI CONFIG
// ===============================
const char* ssid = "Redmi 12C";
const char* password = "@@@$$$###!!...";

// ===============================
// SERVER OBJECTS
// ===============================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===============================
// TIMING
// ===============================
unsigned long lastSend = 0;

// ===============================
// SYSTEM LIMITS
// ===============================
const float min_temp = 12;
const float max_temp = 20;
const float min_humidity = 85;
const float max_humidity = 95;

// ===============================
// PINS
// ===============================
int servoPin = 14;
int dhtPin = 26;
int fanPin = 27;
int peltierPin = 21;
int humidifierPin = 22;
int ledPin = 2;

#define SD_CS 5

// ===============================
// STATE VARIABLES
// ===============================
String servoState = "0";
String fanState = "0";
String peltierState = "0";
String humidifierState = "0";
bool reset = true;

// ===============================
// AUTO MODE — IDENTIFIER / REASON TRACKING
// ===============================
// Per-actuator "why" string, e.g. "humidity>95%". Cleared in manual mode.
String ventReason = "";
String fanReason = "";
String peltierReason = "";
String humidifierReason = "";

// Combined, human-readable summary of what's currently active in auto mode.
String autoSummary = "";

// ===============================
// COMPONENTS
// ===============================
Servo servoVent;
RTC_DS3231 rtc;
DHT dht(dhtPin, DHT22);

// ===============================
// JSON BUFFER
// ===============================
JsonDocument jsonDoc;

// ===============================
// MODE CONTROL
// ===============================
String mode = "manual";


// ===============================
// SD CARD FUNCTIONS
// ===============================

// Write / Append to SD card
void saveToFile(String data, String type){

  File file;

  if(type == "write"){
    file = SD.open("/datalog.txt", FILE_WRITE);
  } else {
    file = SD.open("/datalog.txt", FILE_APPEND);
  }

  if(file){
    file.println(data);
    file.close();
  } else {
    Serial.println("SD write error");
  }
}

// Read SD card
void readFromFile(){

  File file = SD.open("/datalog.txt");

  if(!file){
    Serial.println("SD read error");
    return;
  }

  while(file.available()){
    Serial.write(file.read());
  }

  file.close();
}

// ===============================
// WEBSOCKET SEND
// ===============================
void notifyClients(String message){
  ws.textAll(message);
}

// ===============================
// HANDLE CLIENT MESSAGES
// ===============================
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len){

  AwsFrameInfo *info = (AwsFrameInfo*)arg;

  if(info->final && info->opcode == WS_TEXT){

    data[len] = 0;
    String msg = (char*)data;

    Serial.println("CMD: " + msg);

    // ===============================
    // OPTIONAL COMMAND CONTROL
    // ===============================
    if(msg == "fan_on"){
      digitalWrite(fanPin, HIGH);
    }

    if(msg == "fan_off"){
      digitalWrite(fanPin, LOW);
    }

    if(msg == "vent_open"){
      // servoVent.write(90);
      // delay(300);
      // servoVent.write(180);
      // delay(300);
      // servoVent.write(0);
      // delay(400);
      
      // int count = 18;
      // int angle = 10;

      // for (int i = 0; i < count; i++)
      // {
      //   servoVent.write(angle);
      //   angle += 10;
      //   delay(50);
      // }
  
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

// ===============================
// WEBSOCKET EVENTS
// ===============================
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
  if(type == WS_EVT_DATA){
    handleWebSocketMessage(arg, data, len);
  }
}

// ===============================
// SETUP
// ===============================
void setup(){

  Serial.begin(9600);
  delay(1000);

  // -------------------------------
  // SERVO
  // -------------------------------
  servoVent.attach(servoPin);
  servoVent.write(0);

  // -------------------------------
  // FAN
  // -------------------------------
  pinMode(fanPin, OUTPUT);
  digitalWrite(fanPin, LOW);

  // -------------------------------
  // PELTIER
  // -------------------------------
  pinMode(peltierPin, OUTPUT);
  digitalWrite(peltierPin, LOW);

  // -------------------------------
  // HUMIDIFIER
  // -------------------------------
  pinMode(humidifierPin, OUTPUT);
  digitalWrite(humidifierPin, LOW);

  // -------------------------------
  // LED
  // -------------------------------
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  
  // -------------------------------
  // DHT
  // -------------------------------
  dht.begin();

  // -------------------------------
  // I2C (RTC)
  // -------------------------------
  Wire.begin(32, 33); // SDA -> 32, SCL -> 33 pins for ESP32

  if(!rtc.begin()){
    Serial.println("RTC missing");
    while(1);
  }

  if(rtc.lostPower()){
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // -------------------------------
  // SD CARD
  // -------------------------------
  if(!SD.begin(SD_CS)){
    Serial.println("SD failed");
    while(1);
  }

  // -------------------------------
  // WIFI
  // -------------------------------
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected: " + WiFi.localIP().toString());

  // -------------------------------
  // WEBSOCKET
  // -------------------------------
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.begin();
}

// ===============================
// LOOP
// ===============================
void loop(){

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  DateTime now = rtc.now();

  // ===============================
  // CONTROL LOGIC (actuator behavior unchanged — only now also records
  // an identifier/reason string for whichever actuator(s) it touches)
  // ===============================
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
  

  // ===============================
  // LOGGING
  // ===============================
  String date = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year());
  String time = String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());

  servoState = (servoVent.read() > 0) ? "1" : "0";
  fanState = (digitalRead(fanPin) == HIGH) ? "1" : "0";
  peltierState = (digitalRead(peltierPin) == HIGH) ? "1" : "0";
  humidifierState = (digitalRead(humidifierPin) == HIGH) ? "1" : "0";

  String logData = date + ", " + time + ", " + String(temp) + "C, " + String(hum) + "%, " + servoState + ", " + fanState + ", " + peltierState + ", " + humidifierState;

  saveToFile(logData, "append");

  // ===============================
  // AUTO SUMMARY — single combined line for the frontend's log strip,
  // e.g. "Humidifier: humidity<85%  Fan: temp>20C"
  // ===============================
  autoSummary = "";
  if(mode == "auto"){
    if(servoState == "1")       autoSummary += "Vent: " + ventReason + " \n";
    if(fanState == "1")         autoSummary += "Fan: " + fanReason + " \n";
    if(peltierState == "1")     autoSummary += "Peltier: " + peltierReason + " \n";
    if(humidifierState == "1")  autoSummary += "Humidifier: " + humidifierReason + " \n";

    if(autoSummary == ""){
      autoSummary = "All readings within range - no actuators active";
    }
  }

  // ===============================
  // JSON STREAM TO FRONTEND
  // ===============================
  jsonDoc["date"] = date;
  jsonDoc["time"] = time;
  jsonDoc["temperature"] = temp;
  jsonDoc["humidity"] = hum;
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
  if(millis() - lastSend > 10){
    lastSend = millis();
    notifyClients(json);
  }
}
