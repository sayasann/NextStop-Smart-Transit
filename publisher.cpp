/*
 * ==============================================================================
 * SMART TRANSIT VEHICLE & MQTT PUBLISHER (MERGED)
 * ==============================================================================
 */

#include <SPI.h>               
#include <MFRC522.h>           
#include <DHT.h>               
#include <Wire.h>              
#include <LiquidCrystal_I2C.h> 
#include <ESP32Servo.h>        
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- AĞ VE MQTT AYARLARI ---
const char* ssid = "WifiName";
const char* password = "Password";
const char* mqtt_server = "IP"; 

WiFiClient espClient;
PubSubClient client(espClient);

// --- OTOBÜS BİLGİLERİ ---
String route = "304";
String busId = "35ABC123";
String publishTopic = "otobussistemi/304/35ABC123/veri";
unsigned long sonMqttGonderim = 0;
unsigned long sonBaglantiDenemesi = 0;

// --- SENSÖR, MOTOR VE EKRAN PINLERI ---
#define DHTPIN 15       // Data pin for the DHT11 sensor
#define MOTOR_PIN 13    // Output pin connected to the transistor to control the DC Motor (Fan)
#define LED_PIN 26      // Output pin for the indicator LED
#define BUTTON_PIN 27   // Input pin for the push button (LED icin)
#define BUZZER_PIN 25   // Output pin for the Buzzer auditory feedback
#define DORTLULED_PIN 2 // Output pin for the 4 LEDS. (Transistor)

// Servo ve Servo Butonu Pinleri
#define SERVO1_PIN 17
#define SERVO2_PIN 14
#define SERVO_BUTON_PIN 16

// Ultrasonic sensor pins
#define TRIG1 32        
#define ECHO1 34        
#define TRIG2 33        
#define ECHO2 35        

// RFID module specific pins
#define SS_PIN 5        
#define RST_PIN 4       

// SYSTEM CONSTANTS & THRESHOLDS 
#define MESAFE_ESIK 7
#define GECIS_TIMEOUT 1500  
#define ESIK_SICAKLIK 28 

DHT dht(DHTPIN, DHT11);                      
MFRC522 mfrc522(SS_PIN, RST_PIN);             
LiquidCrystal_I2C lcd(0x27, 16, 2);           

Servo servo1;
Servo servo2;

// Variables for the RFID payment system
String gecerliKart = "KART ID";  
float sanalBakiye = 70.0;                     
float biletFiyati = 17.50;                    

// General system state variables
int yolcuSayisi = 5;                         
bool motorCalisiyor = false;                 
float guncelSicaklik = 0.0;                
float guncelNem = 0.0;

bool ledAcik = false;                         
int sonButonDurumu = HIGH;                    

// Servo durumu degiskenleri
bool servolarAcik = false;
int sonServoButonDurumu = HIGH;

// Variables for timing and ultrasonic sensor state management
unsigned long sonEkranGuncelleme = 0;         
bool sensor1Gecildi = false;                  
unsigned long sensor1Zaman = 0;               

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Baglaniliyor: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // Baslangicta baglanmasini bekle
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi baglandi!");
  Serial.print("IP Adresi: ");
  Serial.println(WiFi.localIP());
}

boolean reconnect() {
  String clientId = "OtobusClient-";
  clientId += String(random(0, 1000));
  
  if (client.connect(clientId.c_str())) {
    Serial.println("MQTT Baglandi!");
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);       
  SPI.begin();                
  mfrc522.PCD_Init();         
  dht.begin();                
  lcd.init();                 
  lcd.backlight();            

  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW); 
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  pinMode(SERVO_BUTON_PIN, INPUT_PULLUP);

  // Servo ayarlari (Yumusak kalkis frekans sabitleme)
  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo1.attach(SERVO1_PIN, 500, 2400);
  servo2.attach(SERVO2_PIN, 500, 2400);
  servo1.write(0); 
  servo2.write(0); 
  
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(DORTLULED_PIN,OUTPUT);
  digitalWrite(DORTLULED_PIN, HIGH);

  lcd.setCursor(0, 0);
  lcd.print("Sistem Basliyor");

  // WiFi ve MQTT kurulumu
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  
  lcd.clear();                
}

void loop() {
  // MQTT Baglanti Kontrolu (Sistemi kilitlemeden arkada dener)
  if (!client.connected()) {
    if (millis() - sonBaglantiDenemesi > 5000) {
      sonBaglantiDenemesi = millis();
      if (reconnect()) {
        sonBaglantiDenemesi = 0;
      }
    }
  } else {
    client.loop(); // MQTT arkaplan isleri
  }



  // --- ANA SISTEM FONKSIYONLARI ---
  butonServisi();           
  servoKapiServisi();       
  sicaklikMotorServisi();   
  rfidServisi();            
  yolcuSayaciServisi();     
  ekranServisi();           
  
  // --- YENİ EKLENEN: MQTT YAYIN SERVISI ---
  mqttYayinServisi();
}

// 10 SANIYEDE BIR ARKA PLANDA CALISAN MQTT FONKSIYONU
void mqttYayinServisi() {
  if (millis() - sonMqttGonderim >= 10000) {
    sonMqttGonderim = millis();
    
    // Sicaklik degerimiz zaten var, sadece nemi okuyalim
    
    if (isnan(guncelNem)) {
      guncelNem = 0.0; // Hata alirsak 0 gondersin
    }

    StaticJsonDocument<200> doc;
    doc["route"] = route;
    doc["busId"] = busId;
    doc["temperture"] = guncelSicaklik; 
    doc["humidity"] = (int)guncelNem; 
    doc["numberOfPassengers"] = yolcuSayisi;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    if (client.connected()) {
      Serial.print("Veri Gonderiliyor: ");
      Serial.println(jsonPayload);
      client.publish(publishTopic.c_str(), jsonPayload.c_str());
    }
  }
}

// SERVO AND BUTTON MANAGEMENT (Yumusak Kalkis)
void servoKapiServisi() {
  int okunanButon = digitalRead(SERVO_BUTON_PIN); 
  
  if (okunanButon == LOW && sonServoButonDurumu == HIGH) {
    servolarAcik = !servolarAcik; 
    
    if (servolarAcik) {
      digitalWrite(LED_PIN, LOW);
      ledAcik = LOW;
      for (int pos = 90; pos <= 180; pos += 5) { 
        servo1.write(180-pos);
        servo2.write(pos);
        
        delay(20); 
      }
    } else {
      
      for (int pos = 180; pos >= 90; pos -= 5) { 
        servo1.write(180-pos);
        servo2.write(pos);
        delay(20);
      }
    }
    delay(50); 
  }
  sonServoButonDurumu = okunanButon; 
}

// LED BUTTON MANAGEMENT 
void butonServisi() {
  int okunanButon = digitalRead(BUTTON_PIN); 
  
  if (okunanButon == LOW && sonButonDurumu == HIGH && ledAcik == LOW) {
    ledAcik = !ledAcik; 
    digitalWrite(LED_PIN, ledAcik ? HIGH : LOW); 
    delay(50); 
  }
  sonButonDurumu = okunanButon; 
}

// TEMPERATURE AND MOTOR MANAGEMENT 
void sicaklikMotorServisi() {
  static unsigned long sonOkuma = 0; 
  
  if (millis() - sonOkuma >= 2000) {
    sonOkuma = millis(); 
    float t = dht.readTemperature(); 
    float hum = dht.readHumidity();
    guncelNem = hum;
    
    if (isnan(t)) return; 

    guncelSicaklik = t; 

    if (guncelSicaklik >= ESIK_SICAKLIK) {
      digitalWrite(MOTOR_PIN, HIGH); 
      motorCalisiyor = true;         
    } else {
      digitalWrite(MOTOR_PIN, LOW);  
      motorCalisiyor = false;        
    }
  }
}

// RFID AND PAYMENT MANAGEMENT 
void rfidServisi() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String okunanKartID = ""; 
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    okunanKartID += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    okunanKartID += String(mfrc522.uid.uidByte[i], HEX);
  }
  okunanKartID.trim(); 
  okunanKartID.toUpperCase(); 

  lcd.clear(); 
  
  if (okunanKartID == gecerliKart) {
    if (sanalBakiye >= biletFiyati) {
      bakiyeDusur(); 
      yolcuSayisi++; 
    } else {
      bakiyeYetersiz(); 
    }
  } else {
    gecersizKartMesaji(okunanKartID); 
  }

  sonEkranGuncelleme = millis() + 3000;
  mfrc522.PICC_HaltA();
}

// PASSENGER COUNTING (ULTRASONIC SENSORS) 
void yolcuSayaciServisi() {
  float mesafe1 = mesafeOlc(TRIG1, ECHO1);
  Serial.println(mesafe1);
  
  delay(30); 
  float mesafe2 = mesafeOlc(TRIG2, ECHO2);
   Serial.println(mesafe2);
  
  if (!sensor1Gecildi && mesafe2 < MESAFE_ESIK) {
    delay(500);  
  }
  else if (!sensor1Gecildi && mesafe1 < MESAFE_ESIK) {
    sensor1Gecildi = true;       
    sensor1Zaman = millis();     
  }

  if (sensor1Gecildi && servolarAcik) {
    if (mesafe2 < MESAFE_ESIK) {
      if (yolcuSayisi > 0) yolcuSayisi--;
      
      sensor1Gecildi = false; 
      
      lcd.setCursor(0, 1);
      lcd.print("Yolcu: ");
      lcd.print(yolcuSayisi);
      lcd.print("          "); 
      delay(500);              
    }
    
    if (millis() - sensor1Zaman > GECIS_TIMEOUT) {
      sensor1Gecildi = false;
    }
  }
}

// DEFAULT LCD DISPLAY 
void ekranServisi() {
  if (millis() < sonEkranGuncelleme) return;

  lcd.setCursor(0, 0);
  lcd.print("Sic:");
  lcd.print(guncelSicaklik, 1); 
  lcd.print("C ");
  lcd.print(motorCalisiyor ? "FAN:AC " : "FAN:KP ");

  lcd.setCursor(0, 1);
  lcd.print("Nem:%"); lcd.print((int)guncelNem);
  lcd.print(" Yolcu: ");
  lcd.print(yolcuSayisi); 
}

// Calculates the distance using the HC-SR04
float mesafeOlc(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  
  long sure = pulseIn(echo, HIGH, 15000);
  
  if (sure == 0) return 999;
  
  return sure * 0.034 / 2.0;
}

// Handles successful transactions
void bakiyeDusur() {
  digitalWrite(BUZZER_PIN, HIGH); delay(150); digitalWrite(BUZZER_PIN, LOW);
  sanalBakiye -= biletFiyati; 
  lcd.print("Gecis Onaylandi");
  lcd.setCursor(0, 1);
  lcd.print("Bakiye:"); lcd.print(sanalBakiye); lcd.print("TL");
}

// Handles insufficient funds scenario
void bakiyeYetersiz() {
  digitalWrite(BUZZER_PIN, HIGH); delay(800); digitalWrite(BUZZER_PIN, LOW);
  lcd.print("Yetersiz Bakiye");
}

// Handles unregistered/invalid cards
void gecersizKartMesaji(String id) {
  digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(100);
  digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);
  lcd.print("Gecersiz Kart!");
}