#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <time.h>
#include <math.h>
#include "MAX30105.h"
#include "heartRate.h"

// Confguração Wifi
const char* wifi_ssid = ""; // Ainda falta botar o wifi 
const char* wifi_password = ""; // Ainda falta botar o password
const int wifi_timeout = 10000;

const char* mqtt_broker = "io.adafruit.com";
const int mqtt_port = 1883;
const char* mqtt_client_id = "Xiao_esp32c3_sono";
const char* username = "";
const char* user_key = ""; 
const char* mqtt_topic = "";

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

// Funções WiFi e MQTT além das funções para sincronizar o tempo e formar a string do tempo 
void connectWiFi() {
  if(WiFi.status() == WL_CONNECTED) return;

  Serial.print("Conectando à rede WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  unsigned long startMillis = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMillis < wifi_timeout)) {
    Serial.print(".");
    delay(500);
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado à rede WiFi com IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFalha na conexão WiFI!");
  }
}

bool connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (mqtt_client.connected()) return true;

  Serial.print("Conectando ao broker MQTT...");
  if (mqtt_client.connect(mqtt_client_id, username, user_key)) {
    Serial.println("Conectado!");
    return true;
  } else {
    Serial.println("Falha na conexão ao broker: Estado=" + String(mqtt_client.state()));
    return false;
  }
}

void syncNTP() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Sincronizando relógio NTP");

  unsigned long startMillis = millis();
  while (time(nullptr) < 1700000000 && (millis() - startMillis < 10000)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nRelógio sincronizado!");
}

String formatISO8601(time_t epochTime) {
  struct tm *ptm = gmtime(&epochTime);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", ptm);
  return String(buffer);
}

// Configuração Sensores
const unsigned long INTERVALO_COLETA = 30000; 
const unsigned long INTERVALO_ENTREGA = 300000; 
unsigned long ULTIMA_COLETA = 0;
unsigned long ULTIMA_ENTREGA = 0;

const int MPU_ADDR = 0x68;
const unsigned long INTERVALO_LEITURA_MPU = 50;
unsigned long ultimaLeituraMPU = 0;
float picoMovimento = 0;

MAX30105 particleSensor;
byte RATE_COUNT = 0;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

// Configuração do batch
typedef struct {
  int BPMAVG;
  float AC;
  time_t timestamp;
} dados;

const int MAX_BATCH = 30;
dados batch[MAX_BATCH];
int batchIndex = 0;


// Começo do codigo real
void setup() {
  Serial.begin(115200);
  delay(2000);

  Wire.begin(6, 7);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  if(Wire.endTransmission(true) == 0) {
    Serial.println("MPU-6050 encontrado e acordado com sucesso!");
  } else {
    Serial.println("Erro ao comunicar com MPU-6050!");
  }

  if(!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 nao encontrado. Verifique conexoes/alimentacao.");
    while (1);
  }

  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  connectWiFi();
  syncNTP();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setBufferSize(512);

}

void loop() {
  long leituraIR = particleSensor.getIR();

  // Leitura do MAX
  if (leituraIR < 40000) {
    RATE_COUNT = 0;
    beatsPerMinute = 0;
    beatAvg = 0;
    rateSpot = 0;
    memset(rates, 0, sizeof(rates));
  } 
  else if (checkForBeat(leituraIR) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60.0 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      if(RATE_COUNT < RATE_SIZE){
      RATE_COUNT++;
      }

      beatAvg = 0;
      for (byte x = 0; x < RATE_COUNT; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_COUNT;
    }
  }

  // Leitura do MPU
  if (millis() - ultimaLeituraMPU >= INTERVALO_LEITURA_MPU) {
    ultimaLeituraMPU = millis();

    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);

    int16_t AcX = Wire.read() << 8 | Wire.read();
    int16_t AcY = Wire.read() << 8 | Wire.read();
    int16_t AcZ = Wire.read() << 8 | Wire.read();

    float ax = AcX / 16384.0;
    float ay = AcY / 16384.0;
    float az = AcZ / 16384.0;

    float aceleracaoTotal = fabs(sqrt(ax * ax + ay * ay + az * az) - 1.02);

    if(aceleracaoTotal > picoMovimento) {
      picoMovimento = aceleracaoTotal;
    }
  }

  // Sistema de coleta
  if(millis() - ULTIMA_COLETA >= INTERVALO_COLETA)  {
    ULTIMA_COLETA = millis();
    
    if(batchIndex < MAX_BATCH) {
      batch[batchIndex].BPMAVG = beatAvg;
      batch[batchIndex].AC = picoMovimento;
      batch[batchIndex].timestamp = time(nullptr);
      batchIndex++;

      Serial.print("[RAM] Salvo no batch. Total na fila: ");
      Serial.println(batchIndex);
    }
    else {
      Serial.println("[AVISO] Buffer cheio! Aguardando envio.");
    }

    picoMovimento = 0;
  }

  // Sistema de entrega  
  if(millis() - ULTIMA_ENTREGA >= INTERVALO_ENTREGA) {
    ULTIMA_ENTREGA = millis();

    if(batchIndex > 0) {
      connectWiFi();

      if(connectMQTT()) {
        mqtt_client.loop();

        int limiteEnvio = (batchIndex > 10) ? 10 : batchIndex; // quantidade de envio
        int enviados = 0;

        Serial.println("Enviando os dados para o mqtt do adafruit");
        for (int i = 0; i < limiteEnvio; i++) {
          String isoTime = formatISO8601(batch[i].timestamp);

          char payload[160];
          snprintf(payload, sizeof(payload),
                   "{\"feeds\":{\"bpm\":%d,\"movimento\":%.2f},\"created_at\":\"%s\"}",
                   batch[i].BPMAVG, batch[i].AC, isoTime.c_str());

          if (mqtt_client.publish(mqtt_topic, payload)) {
            Serial.print("Item ");
            Serial.print(i);
            Serial.print(" enviado: ");
            Serial.println(payload);
            enviados++;
            delay(150);
          } else {
            Serial.println("Falha ao publicar item. Mantendo restante na RAM.");
            break;
          }
        }

        for (int i = enviados; i < batchIndex; i++) {
          batch[i - enviados] = batch[i];
        }
        batchIndex -= enviados;

        Serial.print("Restantes na fila: ");
        Serial.println(batchIndex);
        Serial.println("\n");
      } else {
        Serial.println("[OFFLINE] Sem conexão. Lote mantido na RAM.");
      }
    }
  }
}