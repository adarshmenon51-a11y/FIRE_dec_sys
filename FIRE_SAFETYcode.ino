#define BLYNK_TEMPLATE_ID "TMPL36PBfWzn3"       // Your template ID
#define BLYNK_TEMPLATE_NAME "Fire APP"         // Your template name
#define BLYNK_AUTH_TOKEN "L5OHBHzmKkK35Ky10WZUTUu4FJZ7STiq"  // Your auth token
#define BLYNK_PRINT Serial

#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // Use the ESP32-compatible version
#include <ESP32Servo.h>
#include <DHT.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// Blynk Configuration (REPLACE WITH YOUR ACTUAL VALUES)
// WiFi Credentials (REPLACE WITH YOUR NETWORK INFO)
char ssid[] = "ABX";
char pass[] = "12345678";

// Pin Definitions
#define IR_SENSOR 4
#define FLAME_SENSOR 5
#define MQ7_SENSOR 34
#define DHT_PIN 18
#define DHT_TYPE DHT22
#define RELAY_PIN 27
#define BUZZER 25
#define RED_LED 32
#define SERVO_PIN 26
#define GPS_TX 17
#define GPS_RX 16

// Initialize LCD with ESP32-compatible constructor
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Component Initialization
Servo fireExtinguisher;
DHT dht(DHT_PIN, DHT_TYPE);
TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

// State Variables
bool vehicleDetected = false;
bool fireDetected = false;
bool alertActive = false;
bool servoActivated = false;
unsigned long fireStartTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;
unsigned long lastBlynkUpdate = 0;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_TX, GPS_RX);

  // Initialize Blynk with new authentication method
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // Pin Modes
  pinMode(IR_SENSOR, INPUT_PULLUP);
  pinMode(FLAME_SENSOR, INPUT);
  pinMode(MQ7_SENSOR, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  // Initialize components
  lcd.init();
  lcd.backlight();
  dht.begin();

  // Initial states
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER, LOW);
  digitalWrite(RED_LED, LOW);

  Serial.println("System Initialized");
}

void printSensorData() {
  Serial.println("\n=== SENSOR DATA AT ACTIVATION ===");
  
  // GPS Data
  bool gpsValid = false;
  unsigned long gpsStartTime = millis();
  while (millis() - gpsStartTime < 2000 && !gpsValid) {
    while (SerialGPS.available() > 0) {
      if (gps.encode(SerialGPS.read())) {
        if (gps.location.isValid()) {
          Serial.print("GPS: ");
          Serial.print(gps.location.lat(), 6);
          Serial.print(", ");
          Serial.println(gps.location.lng(), 6);
          
          // Send to Blynk
          Blynk.virtualWrite(V2, gps.location.lat());
          Blynk.virtualWrite(V3, gps.location.lng());
          gpsValid = true;
        }
      }
    }
  }
  if (!gpsValid) {
    Serial.println("GPS not available");
    Blynk.virtualWrite(V2, "N/A");
    Blynk.virtualWrite(V3, "N/A");
  }

  // Environment Data
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (!isnan(temp) && !isnan(humidity)) {
    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.print("°C, Humidity: ");
    Serial.print(humidity);
    Serial.println("%");
    
    Blynk.virtualWrite(V0, temp);
    Blynk.virtualWrite(V1, humidity);
    
    if (temp > 30) {
      Blynk.logEvent("temp_alert", String("High Temp: ") + temp + "°C");
    }
  } else {
    Serial.println("Failed to read DHT");
  }
}

void sendFireAlert() {
  Blynk.logEvent("fire_alert", "FIRE DETECTED! Extinguisher activated");
  Blynk.virtualWrite(V4, 1);  // Fire status
}

void loop() {
  Blynk.run();
  unsigned long currentTime = millis();
  
  // Vehicle detection
  static unsigned long lastIrCheck = 0;
  if (currentTime - lastIrCheck > 200) {
    lastIrCheck = currentTime;
    bool irState = digitalRead(IR_SENSOR);
    
    if (irState == LOW && !vehicleDetected) {
      vehicleDetected = true;
      lcd.clear();
      lcd.print("Vehicle Detected");
      Blynk.virtualWrite(V5, 1);
    } 
    else if (irState == HIGH && vehicleDetected && !alertActive) {
      vehicleDetected = false;
      lcd.clear();
      Blynk.virtualWrite(V5, 0);
    }
  }

  // Fire detection
  if (vehicleDetected && !alertActive) {
    if (digitalRead(FLAME_SENSOR) == LOW || digitalRead(MQ7_SENSOR) == LOW) {
      fireDetected = true;
      alertActive = true;
      fireStartTime = currentTime;
      lastBlinkTime = currentTime;
      Serial.println("FIRE DETECTED!");
      lcd.clear();
      lcd.print("FIRE DETECTED!");
      sendFireAlert();
    }
  }

  // Alert sequence
  if (alertActive) {
    unsigned long elapsed = currentTime - fireStartTime;

    // Blinking alerts
    if (currentTime - lastBlinkTime >= 500) {
      lastBlinkTime = currentTime;
      blinkState = !blinkState;
      digitalWrite(BUZZER, blinkState);
      digitalWrite(RED_LED, blinkState);
    }

    // Servo activation at 10s
    if (elapsed >= 10000 && elapsed < 15000 && !servoActivated) {
      servoActivated = true;
      Serial.println("ACTIVATING EXTINGUISHER");
      printSensorData();
      
      lcd.clear();
      lcd.print("ACTIVATING");
      lcd.setCursor(0, 1);
      lcd.print("EXTINGUISHER");

      digitalWrite(RELAY_PIN, HIGH);
      delay(500);
      fireExtinguisher.attach(SERVO_PIN);
      fireExtinguisher.write(90);
      delay(1000);
      fireExtinguisher.write(0);
      delay(1000);
      fireExtinguisher.detach();
      digitalWrite(RELAY_PIN, LOW);
    }

    // Reset at 15s
    if (elapsed >= 15000) {
      alertActive = false;
      fireDetected = false;
      servoActivated = false;
      digitalWrite(BUZZER, LOW);
      digitalWrite(RED_LED, LOW);
      lcd.clear();
      lcd.print("System Ready");
      Blynk.virtualWrite(V4, 0);
    }
  }

  // Regular Blynk updates
  if (currentTime - lastBlynkUpdate >= 2000) {
    lastBlynkUpdate = currentTime;
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (!isnan(temp) && !isnan(humidity)) {
      Blynk.virtualWrite(V0, temp);
      Blynk.virtualWrite(V1, humidity);
    }
  }
}