/*****************************************************************************************
 * Proyek Smart Home ESP32 dengan Logika Fuzzy dan MQTT
 * ----------------------------------------------------
 * Kode ini menggabungkan beberapa fungsionalitas:
 * 1. Konektivitas WiFi dan MQTT untuk komunikasi dengan Node-RED.
 * 2. Kontrol kipas cerdas menggunakan Logika Fuzzy berdasarkan suhu dan jumlah orang.
 * 3. Kontrol lampu otomatis berdasarkan deteksi orang.
 *
 * Mode:
 * - Otomatis: Kecepatan kipas, status jendela, dan lampu diatur oleh logika sistem.
 * - Manual: Kipas dan jendela dikontrol langsung melalui perintah dari Node-RED.
 *
 * Topik MQTT yang digunakan:
 * -> Publish (ESP32 ke Node-RED):
 * - smarthomeIoT/suhu
 * - smarthomeIoT/kelembapan
 * - smarthomeIoT/jumlahOrang
 *
 * -> Subscribe (Node-RED ke ESP32):
 * - smarthomeIoT/mode ("auto" atau "manual")
 * - smarthomeIoT/kecepatan (0, 1, 2, 3) --> Hanya berfungsi di mode manual
 * - smarthomeIoT/jendela ("buka" atau "tutup") --> Hanya berfungsi di mode manual
 *
 * Dibuat dengan menggabungkan kode Wokwi (MQTT) dan kode lokal (Fuzzy Logic).
 * Versi ini menambahkan kontrol LED.
 *****************************************************************************************/

// --- PUSTAKA (LIBRARIES) ---
#include <WiFi.h>
#include <MQTT.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <algorithm> // Diperlukan untuk std::min dan std::max

// --- KONFIGURASI JARINGAN & MQTT ---
const char ssid[] = "Redmi 9t";    // Ganti dengan nama WiFi Anda
const char pass[] = "1desember20051desember2005";  // Ganti dengan password WiFi Anda
const char mqtt_broker[] = "broker.emqx.io";
const int mqtt_port = 1883;

// --- KONFIGURASI PIN PERANGKAT KERAS ---
// Sensor Suhu & Kelembapan
#define DHT_PIN 26       // Pin data DHT11/22
#define DHT_TYPE DHT11  // Ganti ke DHT22 jika Anda menggunakan sensor tersebut

// Sensor Gerak (PIR) untuk Menghitung Orang
const int PIR_PIN1 = 25; // PIR sensor untuk pintu masuk
const int PIR_PIN2 = 33; // PIR sensor untuk pintu keluar

// Driver Motor DC untuk Kipas
#define ENA 32 // PWM pin untuk kecepatan motor
#define IN1 34 // Pin arah motor 1
#define IN2 35 // Pin arah motor 2

// Servo untuk Jendela
#define SERVO_WINDOW_PIN 23 // Pin sinyal untuk servo jendela

// LED Indikator
#define LED_PIN 22 // Pin untuk LED

// --- INISIALISASI OBJEK ---
WiFiClient net;
MQTTClient client;
DHT dht(DHT_PIN, DHT_TYPE);
Servo servoWindow;

// --- VARIABEL GLOBAL ---
// Status Sistem
bool isAutoMode = true;     // Mode awal adalah otomatis
bool windowState = false;   // Status jendela (false = tertutup, true = terbuka)
int fanLevelManual = 0;     // Level kecepatan kipas manual (0-3)

// Data Sensor
float temperature = 25.0;
float humidity = 60.0;
int peopleCounter = 0;      // Variabel untuk jumlah orang dari logika fuzzy

// Variabel untuk Penghitung Orang (PIR)
bool sensor1_triggered = false;
bool sensor2_triggered = false;
bool counting_up_sequence = false;
bool counting_down_sequence = false;
unsigned long last_state_change = 0;
const unsigned long DEBOUNCE_DELAY = 100;
const unsigned long SEQUENCE_TIMEOUT = 1500;

// Variabel untuk timing
unsigned long lastPublishTime = 0;
const long publishInterval = 5000; // Publikasi data setiap 5 detik

// Buffer untuk konversi data ke string
char msgBuffer[20];


// ——— FUNGSI KEANGGOTAAN FUZZY (FUZZY MEMBERSHIP FUNCTIONS) ——————————————————
// Fungsi keanggotaan segitiga (Triangular)
template<typename T>
float trimf(T x, T a, T b, T c) {
  if (x == b) return 1.0f;
  if (x <= a || x >= c) return 0.0f;
  if (x < b) return (float)(x - a) / (float)(b - a);
  return (float)(c - x) / (float)(c - b);
}

// Fungsi Keanggotaan Jumlah Orang (Input 1)
float mfPeopleFew(float x)    { return trimf(x, 0.0f, 0.0f, 3.0f); } // Sedikit
float mfPeopleMedium(float x) { return trimf(x, 1.0f, 3.0f, 5.0f); } // Sedang
float mfPeopleMany(float x)   { return trimf(x, 3.0f, 8.0f, 8.0f); } // Banyak

// Fungsi Keanggotaan Suhu (Input 2)
float mfTempCold(float x)     { return trimf(x, 15.0f, 15.0f, 25.0f); } // Dingin
float mfTempWarm(float x)     { return trimf(x, 20.0f, 30.0f, 35.0f); } // Hangat
float mfTempHot(float x)      { return trimf(x, 30.0f, 40.0f, 40.0f); } // Panas

// Fungsi Keanggotaan Output PWM Kipas (0-255)
float mfPWMLow(float x)       { return trimf(x, 0.0f, 0.0f, 100.0f); }   // Pelan
float mfPWMMedium(float x)    { return trimf(x, 50.0f, 127.0f, 200.0f); } // Sedang
float mfPWMHigh(float x)      { return trimf(x, 150.0f, 255.0f, 255.0f); } // Cepat

// Matriks Aturan Fuzzy [JumlahOrang][Suhu]
// 0 = PWM Rendah, 1 = PWM Sedang, 2 = PWM Tinggi
const int ruleMatrix[3][3] = {
  // Dingin, Hangat, Panas
  {0, 0, 1},      // Sedikit orang
  {0, 1, 2},      // Sedang orang
  {1, 2, 2}       // Banyak orang
};


// ——— KOMPUTASI LOGIKA FUZZY ——————————————————————————————————————————————————
float computePWMCentroid(float people, float temp) {
  float uPeople[3] = { mfPeopleFew(people), mfPeopleMedium(people), mfPeopleMany(people) };
  float uTemp[3] = { mfTempCold(temp), mfTempWarm(temp), mfTempHot(temp) };

  const int N = 255; // Rentang PWM
  float sumNum = 0.0f, sumDen = 0.0f;

  for (int k = 0; k <= N; k++) {
    float pwm = (float)k;
    float muAgg = 0.0f;

    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        float ruleStrength = std::min(uPeople[i], uTemp[j]);
        if (ruleStrength <= 0.0f) continue;

        int outputClass = ruleMatrix[i][j];
        float muOut;

        switch(outputClass) {
          case 0: muOut = mfPWMLow(pwm); break;
          case 1: muOut = mfPWMMedium(pwm); break;
          case 2: muOut = mfPWMHigh(pwm); break;
          default: muOut = 0.0f;
        }
        muAgg = std::max(muAgg, std::min(ruleStrength, muOut));
      }
    }
    sumNum += pwm * muAgg;
    sumDen += muAgg;
  }
  return (sumDen > 0.0f) ? (sumNum / sumDen) : 0.0f;
}

// ——— FUNGSI PENGHITUNG ORANG (PIR) ———————————————————————————————————————————
void resetSequence() {
  sensor1_triggered = false;
  sensor2_triggered = false;
  counting_up_sequence = false;
  counting_down_sequence = false;
  last_state_change = millis();
}

void updatePeopleCounter() {
  unsigned long current_time = millis();
  int motion1 = digitalRead(PIR_PIN1);
  int motion2 = digitalRead(PIR_PIN2);

  if (current_time - last_state_change > SEQUENCE_TIMEOUT) {
    if (counting_up_sequence || counting_down_sequence) {
      resetSequence();
    }
  }

  if (current_time - last_state_change < DEBOUNCE_DELAY) return;

  // Urutan Masuk: sensor 1 lalu sensor 2
  if (!counting_down_sequence) {
    if (motion1 == HIGH && !sensor1_triggered && !counting_up_sequence) {
      sensor1_triggered = true;
      counting_up_sequence = true;
      last_state_change = current_time;
      Serial.println("s1");
    } else if (counting_up_sequence && sensor1_triggered && motion2 == HIGH && !sensor2_triggered) {
      sensor2_triggered = true;
      Serial.println("s2");
      peopleCounter++;
      Serial.println(">>> Seseorang Masuk");
      resetSequence();
    }
  }

  // Urutan Keluar: sensor 2 lalu sensor 1
  if (!counting_up_sequence) {
    if (motion2 == HIGH && !sensor2_triggered && !counting_down_sequence) {
      sensor2_triggered = true;
      counting_down_sequence = true;
      last_state_change = current_time;
      Serial.println("s2");
    } else if (counting_down_sequence && sensor2_triggered && motion1 == HIGH && !sensor1_triggered) {
      sensor1_triggered = true;
      Serial.println("s1");
      peopleCounter = std::max(0, peopleCounter - 1); // Cegah nilai negatif
      Serial.println(">>> Seseorang Keluar");
      resetSequence();
    }
  }
}

// ——— FUNGSI KONTROL PERANGKAT ————————————————————————————————————————————————
void setFanSpeed(int pwmValue) {
  // Pastikan arah motor sudah benar (misal: IN1 HIGH, IN2 LOW)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  // Terapkan kecepatan PWM
  analogWrite(ENA, constrain(pwmValue, 0, 255));
}

void controlWindow(bool open) {
  windowState = open;
  servoWindow.write(open ? 180 : 0); // 180 derajat untuk buka, 0 untuk tutup
  //Serial.print("Jendela diatur ke: ");
  //Serial.println(open ? "Terbuka" : "Tertutup");
}

void controlLed() {
  if (peopleCounter > 0) {
    digitalWrite(LED_PIN, HIGH); // Nyalakan LED jika ada orang
  } else {
    digitalWrite(LED_PIN, LOW); // Matikan LED jika tidak ada orang
  }
}


// ——— FUNGSI MQTT —————————————————————————————————————————————————————————————
void connectMQTT() {
  Serial.print("\nMenghubungkan ke MQTT broker...");
  while (!client.connect("esp32-fuzzy-client", "try", "try")) { // Ganti client ID jika perlu
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nTerhubung ke MQTT broker!");

  // Subscribe ke topik kontrol
  client.subscribe("smarthomeIoT/mode");
  client.subscribe("smarthomeIoT/kecepatan");
  client.subscribe("smarthomeIoT/jendela");
}

void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung!");
}

void messageReceived(String &topic, String &payload) {
  Serial.println("Pesan diterima: " + topic + " = " + payload);

  if (topic == "smarthomeIoT/mode") {
    if (payload == "auto") {
      isAutoMode = true;
      Serial.println("Mode diubah ke OTOMATIS");
    } else if (payload == "manual") {
      isAutoMode = false;
      Serial.println("Mode diubah ke MANUAL");
    }
  }

  // Kontrol manual hanya berfungsi jika mode bukan otomatis
  if (!isAutoMode) {
    if (topic == "smarthomeIoT/kecepatan") {
      int level = payload.toInt();
      fanLevelManual = level;
      int pwm = 0;
      switch (level) {
        case 1: pwm = 85; break;  // ~33%
        case 2: pwm = 170; break; // ~67%
        case 3: pwm = 255; break; // 100%
        default: pwm = 0; break;
      }
      setFanSpeed(pwm);
      Serial.print("Kecepatan kipas manual diatur ke level ");
      Serial.println(level);
    }
    if (topic == "smarthomeIoT/jendela") {
      controlWindow(payload == "true");
    }
  }
}

void publishSensorData() {
  // Publikasi Suhu
  dtostrf(temperature, 4, 2, msgBuffer);
  client.publish("smarthomeIoT/suhu", msgBuffer);

  // Publikasi Kelembapan
  dtostrf(humidity, 4, 2, msgBuffer);
  client.publish("smarthomeIoT/kelembapan", msgBuffer);

  // Publikasi Jumlah Orang
  itoa(peopleCounter, msgBuffer, 10);
  client.publish("smarthomeIoT/jumlahOrang", msgBuffer);
  
  Serial.println("--- Data Sensor Dipublikasikan ---");
}


// ——— FUNGSI UTAMA (SETUP & LOOP) —————————————————————————————————————————————
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("=== Inisialisasi Sistem Smart Home Fuzzy ===");

  // Inisialisasi sensor dan aktuator
  dht.begin();
  pinMode(PIR_PIN1, INPUT);
  pinMode(PIR_PIN2, INPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(LED_PIN, OUTPUT); // Inisialisasi pin LED
  servoWindow.attach(SERVO_WINDOW_PIN);

  // Atur kondisi awal perangkat
  setFanSpeed(0); // Matikan kipas
  controlWindow(false); // Tutup jendela
  digitalWrite(LED_PIN, LOW); // Matikan LED saat awal

  // Koneksi ke jaringan
  connectWiFi();
  
  // Konfigurasi dan koneksi MQTT
  client.begin(mqtt_broker, mqtt_port, net);
  client.onMessage(messageReceived);
  connectMQTT();

  Serial.println("==========================================");
  Serial.println("Sistem Siap.");
  Serial.println("==========================================");
}

void loop() {
  // Jaga koneksi MQTT tetap hidup
  client.loop();
  if (!client.connected()) {
    Serial.println("Koneksi MQTT terputus. Mencoba menghubungkan kembali...");
    connectMQTT();
  }

  // --- Baca Sensor ---
  updatePeopleCounter(); // Selalu update jumlah orang

  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();
  if (!isnan(newTemp)) { temperature = newTemp; }
  if (!isnan(newHum)) { humidity = newHum; }

  // --- Logika Kontrol Utama ---
  // Logika LED selalu aktif terlepas dari mode auto/manual
  controlLed();

  if (isAutoMode) {
    // Mode Otomatis: Gunakan Logika Fuzzy
    int currentPWM = 0;
    if (peopleCounter == 0) {
      currentPWM = 0; // Matikan kipas jika tidak ada orang
    } else {
      float fuzzyPWM = computePWMCentroid((float)peopleCounter, temperature);
      currentPWM = constrain((int)fuzzyPWM, 0, 255);
    }
    setFanSpeed(currentPWM);

    // Logika jendela otomatis berdasarkan suhu
    controlWindow(temperature > 30.0); // Buka jendela jika suhu > 30°C
  }
  // Jika mode manual, tidak ada yang perlu dilakukan di loop utama
  // karena semua kontrol ditangani oleh `messageReceived`.

  // --- Publikasi Data Berkala ---
  unsigned long currentTime = millis();
  if (currentTime - lastPublishTime >= publishInterval) {
    lastPublishTime = currentTime;
    publishSensorData();
    
    // Cetak status ke Serial Monitor untuk debugging
    Serial.println("--- Status Saat Ini ---");
    Serial.print("Mode: "); Serial.println(isAutoMode ? "Otomatis" : "Manual");
    Serial.print("Jumlah Orang: "); Serial.println(peopleCounter);
    Serial.print("Suhu: "); Serial.print(temperature, 1); Serial.println(" *C");
    Serial.print("Kelembapan: "); Serial.print(humidity, 1); Serial.println(" %");
    Serial.print("Status LED: "); Serial.println(digitalRead(LED_PIN) ? "NYALA" : "MATI");
    Serial.println("-----------------------");
  }

  delay(10); // Delay kecil untuk stabilitas
}
