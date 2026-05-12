#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- AYARLAR ---
const char* ssid = "WIFINAME";           
const char* password = "PASSWORD";     
const char* mqtt_server = "IP"; // ipconfig ile bulduğun IP

// LCD Ayarı (Kontrastı ayarladığın adres ve pinler)
LiquidCrystal_I2C lcd(0x27, 16, 2); 

WiFiClient espClient;
PubSubClient client(espClient);

// Sunucudan mesaj geldiğinde çalışan fonksiyon
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);

  // Gelen JSON verisini senin formatına (otobüs kodundaki DTO yapısına) göre ayıklıyoruz
  // String değerleri const char* olarak alıyoruz
  const char* route = doc["route"];
  
  // Sıcaklık ve nem değerleri (otobüs kodundaki "temperture" isimlendirmesine sadık kalarak)
  float sicaklik = doc["temperture"];
  int nem = doc["humidity"];
  int yolcuSayisi = doc["numberOfPassengers"];

  // LCD Ekranı Güncelleme (Yeni verilere göre uyarlandı)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hat:"); lcd.print(route);
  lcd.print(" Yolcu:");                        lcd.print(yolcuSayisi);

  lcd.setCursor(0, 1);
  lcd.print(sicaklik, 1); lcd.print("C ");
  lcd.print(" Nem:%"); lcd.print(nem);
  
}

void setup() {
  Serial.begin(115200);
  
  // I2C pinlerini 13 ve 14 yaparak çakışmayı önledik
  // Wire.begin(13, 14); 
  
  lcd.init();
  lcd.backlight();
  lcd.print("Sistem Beklemede");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("baglandi");
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void reconnect() {
  while (!client.connected()) {
    // MQTT Client ID'sinde Türkçe karakter (ı) sorun yaratabileceği için düzeltildi
    if (client.connect("Durak_Alici_ESP32")) {
      client.subscribe("otobussistemi/304/+/veri");
    } else {
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}