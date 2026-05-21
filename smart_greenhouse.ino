// =============================================
// Smart Greenhouse - ESP32
// GitHub: [Ahmed-Salah-Ahmed-AbdulHamid]/smart-greenhouse
// =============================================

#include "config.h"  

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <FirebaseESP32.h>


// --- Network Credentials ---
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "YOUR_WIFI_SSID"; 
char pass[] = "YOUR_WIFI_PASSWORD";

// --- Firebase Settings ---
#define FIREBASE_HOST "YOUR_PROJECT_ID.firebaseio.com"

// --- Pins ---
#define DHTPIN 4
#define DHTTYPE DHT11
#define LDR_PIN 35
#define RELAY_PIN 5
#define SOIL_PIN 34

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

FirebaseData firebaseData;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;


int lastTemp = -1, lastHum = -1;
bool isAutoMode = true;
bool actualPumpState = false;

// --- Blynk (V5) ---
BLYNK_WRITE(V5) {
  isAutoMode = (param.asInt() == 1);
  Serial.print("Current Mode: ");
  Serial.println(isAutoMode ? "AUTO (AI Control)" : "MANUAL (Button Control)");

  digitalWrite(RELAY_PIN, HIGH);
  Blynk.virtualWrite(V4, 0);
}

// --- Blynk (V4) ---
BLYNK_WRITE(V4) {
  if (!isAutoMode) {
    int pinValue = param.asInt();
    digitalWrite(RELAY_PIN, pinValue ? LOW : HIGH);
    Serial.println(pinValue ? "Manual Pump: ON" : "Manual Pump: OFF");
  } else {
    Serial.println("Action Denied: System is in AUTO mode.");
    int currentState = (digitalRead(RELAY_PIN) == LOW) ? 1 : 0;
    Blynk.virtualWrite(V4, currentState);
  }
}

// --- Blynk ---
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5);
}

//--- Arithmetic Mean Function ---
int getAverageReading(int pin, int samples = 20) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return sum / samples;
}

// --- Basic function for data collection ---
void sendData() {
  int ldrRaw = getAverageReading(LDR_PIN);
  int currentLight = map(ldrRaw, 0, 4095, 0, 100);

  int soilRaw = getAverageReading(SOIL_PIN);
  int currentSoil = map(soilRaw, 4095, 0, 0, 100);

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  
  if (isnan(t)) t = 25.0;
  if (isnan(h)) h = 60.0;

  Blynk.virtualWrite(V0, currentSoil);
  Blynk.virtualWrite(V1, (int)t);
  Blynk.virtualWrite(V2, (int)h);
  Blynk.virtualWrite(V3, currentLight);

  // --- Screen Refresh ---
  int currentTemp = (int)t;
  int currentHum = (int)h;

  if (currentTemp != lastTemp || currentHum != lastHum) {
    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print(currentTemp); lcd.print("C H:"); lcd.print(currentHum); lcd.print("%   ");
    lastTemp = currentTemp;
    lastHum = currentHum;
  }

  lcd.setCursor(0, 1);
  lcd.print("S:"); lcd.print(currentSoil); lcd.print("% L:"); lcd.print(currentLight); lcd.print("% ");
  lcd.print(isAutoMode ? "A" : "M");
}

// --- Firebase ---
void sendToFirebase() {
  int soilRaw = getAverageReading(SOIL_PIN);
  int currentSoil = map(soilRaw, 4095, 0, 0, 100);
  int currentLight = map(getAverageReading(LDR_PIN), 0, 4095, 0, 100);
  int relayStatus = (digitalRead(RELAY_PIN) == LOW) ? 1 : 0;

  FirebaseJson json;
  json.set("Temperature", dht.readTemperature());
  json.set("Humidity",    dht.readHumidity());
  json.set("Light",       currentLight);
  json.set("Soil_Moisture", currentSoil);
  json.set("Pump_State", relayStatus);
  json.set("Mode", isAutoMode ? "Auto" : "Manual");

  if (Firebase.pushJSON(firebaseData, "/SensorLogs", json)) {
    Serial.println("Data logged to Firebase!");
  } else {
    Serial.println("Firebase Error: " + firebaseData.errorReason());
  }
}

// ---AI ---
void checkAIDecision() {
  if (Firebase.getInt(firebaseData, "/AI_Decision/pump")) {
    int aiDecision = firebaseData.intData();

    if (isAutoMode) {
      if (aiDecision == 1) {
        digitalWrite(RELAY_PIN, LOW);
        Blynk.virtualWrite(V4, 1);
        Serial.println("AI: Pump ON");
      } else {
        digitalWrite(RELAY_PIN, HIGH);
        Blynk.virtualWrite(V4, 0);
        Serial.println("AI: Pump OFF");
      }
    }
  }
}

// --- Initial Settings ---
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  lcd.begin();
  lcd.backlight();
  dht.begin();

  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS);

  firebaseConfig.database_url = FIREBASE_HOST;
  firebaseConfig.signer.test_mode = true;
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);

  lcd.setCursor(0, 0);
  lcd.print("Smart GreenHouse");
  lcd.setCursor(0, 1);
  lcd.print("System Ready!   ");
  delay(2000);
  lcd.clear();

  timer.setInterval(2000L,  sendData);
  timer.setInterval(60000L, sendToFirebase);
  timer.setInterval(10000L, checkAIDecision);
}


void loop() {
  Blynk.run();
  timer.run();
}
