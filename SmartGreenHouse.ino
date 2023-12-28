#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <PCF8574.h>
#include <Wire.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>

#define WIFI_SSID "Anh Tuan 5G"
#define WIFI_PASSWORD "@24071973"

#define FIREBASE_HOST "smart-green-house-caf45-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "HyaPeAFRbW5QUCe6oVLDaF1HcC8rwzrrOVku5in4"

#define DHTTYPE DHT11

PCF8574 pcf8574(0x20);
LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo doorServo;
Servo roofServo;

FirebaseJson firebaseJson;
FirebaseData firebaseData;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

int soilPin = A0;
int roofServoPin = D8;
int relayPumpPin = D7;
int doorServoPin = D6;
int DHT11Pin = D5;
int ledPin = D4;
int relayFanPin = D0;

int flamePin = 7;
int rainPin = 6;
int lightPin = 5;
int touchPin4 = 4;
int touchPin3 = 3;
int touchPin2 = 2;
int infraredPin1 = 1;
int infraredPin0 = 0;

DHT dht(DHT11Pin, DHTTYPE);
String pathInputs = "Inputs/";
String pathOutputs = "Outputs/";

int doorAngle = 0;
int roofAngle = 0;
bool doorOpen = false;
bool roofOpen = false;
bool fanOpen = false;
bool pumpOpen = false;
bool pumpSet = false;

bool touchState4 = false;
bool touchState3 = false;
bool touchState2 = false;
bool infraredState = false;
bool DHT11DataSaved = false;

bool roofState = false;

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Attempting to connect to WiFi...");
    delay(1000);
  }
  Serial.println("Connected to WiFi");

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  timeClient.begin();
  timeClient.setTimeOffset(7 * 60 * 60);

  Wire.begin();

  lcd.init();
  lcd.backlight();

  pinMode(relayPumpPin, OUTPUT);
  digitalWrite(relayPumpPin, HIGH);
  pinMode(relayFanPin, OUTPUT);
  digitalWrite(relayFanPin, HIGH);
  pinMode(ledPin, OUTPUT);

  doorServo.attach(doorServoPin);
  doorServo.write(40);
  roofServo.attach(roofServoPin);
  roofServo.write(180);

  Firebase.setBool(firebaseData, pathOutputs + "Door", false);
  Firebase.setBool(firebaseData, pathOutputs + "Pump", false);
  Firebase.setBool(firebaseData, pathOutputs + "Fan", false);
  Firebase.setInt(firebaseData, pathOutputs + "FanHours", -1);
  Firebase.setInt(firebaseData, pathOutputs + "FanMinutes", -1);
}

void loop() {
  timeClient.update();
  int epochTime = timeClient.getEpochTime();
  int day = timeClient.getDay();
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();

  float tempValue = dht.readTemperature();
  float humidityValue = dht.readHumidity();

  int soilValue = analogRead(soilPin);
  int soilValuePercent = map(soilValue, 0, 1024, 0, 100);
  int flameValue = pcf8574.read(flamePin);
  int rainValue = pcf8574.read(rainPin);
  int lightValue = pcf8574.read(lightPin);
  int touchValue4 = pcf8574.read(touchPin4);
  int touchValue3 = pcf8574.read(touchPin3);
  int touchValue2 = pcf8574.read(touchPin2);
  int infraredValue1 = pcf8574.read(infraredPin1);
  int infraredValue0 = pcf8574.read(infraredPin0);

  DHT11Sensor(tempValue, humidityValue, hours, epochTime);
  soilSensor(soilValue);
  // rainSensor(rainValue);
  lightSensor(lightValue);
  flameSensor(flameValue);
  infraredSensor(infraredValue0, infraredValue1);
  touchSensor4(touchValue4, touchValue3, touchValue2, infraredValue0, infraredValue1);
  touchSensor3(touchValue4, touchValue3, touchValue2);
  touchSensor2(touchValue4, touchValue3, touchValue2, hours, minutes);
  lcdDisplay(tempValue, humidityValue, soilValuePercent, pumpSet, hours, minutes, seconds);
}

void DHT11Sensor(float tempValue, float humidityValue, int hours, int epochTime) {
  if (hours == 0 || hours == 6 || hours == 12 || hours == 18) {
    if (!DHT11DataSaved && !isnan(tempValue) && !isnan(humidityValue) && humidityValue < 100) {
      firebaseJson.set("Temp", tempValue);
      firebaseJson.set("Humidity", humidityValue);
      String historyPath = pathInputs + "DHT11/" + epochTime;
      Firebase.setJSON(firebaseData, historyPath.c_str(), firebaseJson);
      DHT11DataSaved = true;
    }
  } else {
    DHT11DataSaved = false;
  }
}

void soilSensor(int soilValue) {
  Firebase.setInt(firebaseData, pathInputs + "Soil", soilValue);
  if (soilValue >= 750 && soilValue < 1024 && !pumpOpen) {
    openPump();
  } else if (soilValue < 750 && pumpOpen) {
    closePump();
  } else {
    closePump();
  }
}

void rainSensor(int rainValue) {
  if (rainValue == 0 && roofOpen && !touchState3) {
    closeRoof();
  } else if (rainValue == 1 && !roofOpen && !touchState3) {
    openRoof();
  }
}

void lightSensor(int lightValue) {
  if (lightValue == 1) {
    Firebase.setBool(firebaseData, pathInputs + "Light", false);
    digitalWrite(ledPin, HIGH);
  } else if (lightValue == 0) {
    Firebase.setBool(firebaseData, pathInputs + "Light", true);
    digitalWrite(ledPin, LOW);
  }
}

void flameSensor(int flameValue) {
  if (flameValue == 0) {
    Firebase.setBool(firebaseData, pathInputs + "Flame", true);
    if (!doorOpen && !touchState4) {
      openDoor();
      touchState4 = true;
      Firebase.setBool(firebaseData, pathOutputs + "Door", true);
    }
    if (!pumpOpen) {
      openPump();
    }
    if (!roofOpen) {
      openRoof();
    }
  } else {
    Firebase.setBool(firebaseData, pathInputs + "Flame", false);
  }
}

void infraredSensor(int infraredValue0, int infraredValue1) {
  if (infraredValue0 == 0 && infraredValue1 == 1 && !doorOpen) {
    openDoor();
  } else if (infraredValue0 == 1 && infraredValue1 == 0 && !doorOpen && !touchState4) {
    openDoor();
  } else if (infraredValue0 == 0 && infraredValue1 == 0 && doorOpen && !touchState4) {
    closeDoor();
  } else if (infraredValue0 == 1 && infraredValue1 == 1 && doorOpen && !touchState4) {
    closeDoor();
  }
}

void touchSensor4(int touchValue4, int touchValue3, int touchValue2, int infraredValue0, int infraredValue1) {
  Firebase.getBool(firebaseData, pathOutputs + "Door");
  bool firebaseDoor = firebaseData.boolData();
  if (infraredValue0 == 1 && infraredValue1 == 1 && touchValue4 == 1 && touchValue3 == 0 && touchValue2 == 0 &&
   !doorOpen && !touchState4 && !firebaseDoor) {
    openDoor();
    touchState4 = true;
    Firebase.setBool(firebaseData, pathOutputs + "Door", true);
  } else if (infraredValue0 == 1 && infraredValue1 == 1 && touchValue4 == 1 && touchValue3 == 0 &&
   touchValue2 == 0 && doorOpen && touchState4 && firebaseDoor) {
    closeDoor();
    touchState4 = false;
    Firebase.setBool(firebaseData, pathOutputs + "Door", false);
  } else if (infraredValue0 == 1 && infraredValue1 == 1 && !doorOpen && !touchState4 && firebaseDoor) {
    openDoor();
    touchState4 = true;
  } else if (infraredValue0 == 1 && infraredValue1 == 1 && doorOpen && touchState4 && !firebaseDoor) {
    closeDoor();
    touchState4 = false;
  }
}

void touchSensor3(int touchValue4, int touchValue3, int touchValue2) {
  Firebase.getBool(firebaseData, pathOutputs + "Pump");
  bool firebasePump = firebaseData.boolData();
  if (touchValue4 == 0 && touchValue3 == 1 && touchValue2 == 0 && !touchState3 && !pumpSet && !firebasePump) {
    touchState3 = true;
    pumpSet = true;
    Firebase.setBool(firebaseData, pathOutputs + "Pump", true);
  } else if (touchValue4 == 0 && touchValue3 == 1 && touchValue2 == 0 && touchState3 && pumpSet && firebasePump) {
    touchState3 = false;
    pumpSet = false;
    Firebase.setBool(firebaseData, pathOutputs + "Pump", false);
  } else if (!touchState3 && !pumpSet && firebasePump) {
    touchState3 = true;
    pumpSet = true;
  } else if (touchState3 && pumpSet && !firebasePump) {
    touchState3 = false;
    pumpSet = false;
  }
}

void touchSensor2(int touchValue4, int touchValue3, int touchValue2, int hours, int minutes) {
  Firebase.getBool(firebaseData, pathOutputs + "Fan");
  bool firebaseFan = firebaseData.boolData();
  Firebase.getInt(firebaseData, pathOutputs + "FanHours");
  int firebaseFanHours = firebaseData.intData();
  Firebase.getInt(firebaseData, pathOutputs + "FanMinutes");
  int firebaseFanMinutes = firebaseData.intData();
  if (touchValue4 == 0 && touchValue3 == 0 && touchValue2 == 1 && !fanOpen && !touchState2 && !firebaseFan) {
    openFan();
    touchState2 = true;
    Firebase.setBool(firebaseData, pathOutputs + "Fan", true);
  } else if (touchValue4 == 0 && touchValue3 == 0 && touchValue2 == 1 && fanOpen && touchState2 && firebaseFan) {
    closeFan();
    touchState2 = false;
    Firebase.setBool(firebaseData, pathOutputs + "Fan", false);
  } else if (!touchState2 && !fanOpen && firebaseFan && firebaseFanHours == -1 && firebaseFanMinutes == -1) {
    openFan();
    touchState2 = true;
  } else if (touchState2 && fanOpen && !firebaseFan) {
    closeFan();
    touchState2 = false;
  } else if (!touchState2 && !fanOpen && firebaseFan && firebaseFanHours == hours && firebaseFanMinutes == minutes) {
    openFan();
    touchState2 = true;
  }
}

void openDoor() {
  for (doorAngle = 40; doorAngle < 150; doorAngle += 3) {
    doorServo.write(doorAngle);
    delay(15);
  }
  doorOpen = true;
}

void closeDoor() {
  for (doorAngle = 150; doorAngle >= 40; doorAngle -= 3) {
    doorServo.write(doorAngle);
    delay(15);
  }
  doorOpen = false;
}

void openRoof() {
  for (roofAngle = 180; roofAngle >= 20; roofAngle -= 5) {
    roofServo.write(roofAngle);
    delay(15);
  }
  roofOpen = true;
}

void closeRoof() {
  for (roofAngle = 20; roofAngle < 180; roofAngle += 5) {
    roofServo.write(roofAngle);
    delay(15);
  }
  roofOpen = false;
}

void openPump() {
  if (pumpSet) {
    digitalWrite(relayPumpPin, LOW);
    pumpOpen = true;
  }
}

void closePump() {
  if (pumpSet) {
    digitalWrite(relayPumpPin, HIGH);
    pumpOpen = false;
  }
}

void openFan() {
  digitalWrite(relayFanPin, LOW);
  fanOpen = true;
}

void closeFan() {
  digitalWrite(relayFanPin, HIGH);
  fanOpen = false;
}

void lcdDisplay(float tempValue, float humidityValue, int soilValuePercent, bool pumpSet, int hours, int minutes, int seconds) {
  int lastTempValue = -1;
  int lastHumidityValue = -1;
  if (!isnan(tempValue) && tempValue >= 0 && tempValue < 100 && !isnan(humidityValue) && humidityValue >= 0 && humidityValue < 100) {
    lastTempValue = int(tempValue);
    lastHumidityValue = int(humidityValue);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.setCursor(2, 0);
  lcd.print(lastTempValue);
  lcd.setCursor(5, 0);
  lcd.print("H:");
  lcd.setCursor(7, 0);
  lcd.print(lastHumidityValue);
  lcd.setCursor(11, 0);
  lcd.print("S:");
  lcd.setCursor(13, 0);
  lcd.print(soilValuePercent);
  lcd.setCursor(0, 1);
  if (pumpSet) {
    lcd.print("P:ON");
  } else {
    lcd.print("P:OFF");
  }
  lcd.setCursor(5, 1);
  lcd.print("-  " + String(hours) + ":" + String(minutes) + ":" + String(seconds));
}
