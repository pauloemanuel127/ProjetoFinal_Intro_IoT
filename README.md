# Monitor de Sono - Projeto Final IoT (IMD0902)

Firmware desenvolvido para o projeto final da disciplina **IMD0902 (Introdução à Internet das Coisas)**. O sistame realiza o monitoramento contínuo de sinais vitais e movimentação durante o sono, enviando os dados consolidados para a plataforma **Adafruit IO** via protocolo **MQTT**.

## Hardware e Sensores

* **Microcontrolado:** XIAO ESP32C3
* **MAX30102:** É um sensor que usa o led vermelho e um infravermelho para por meio das pulsos sanguineos nas veias poder calcular o batimento cardiaco por minuto e pode ser utilizado para calcular a oxigenação do sangue.
* **MPU6050:** É um sensor que consegue medir tanto a aceleração como a direção de movimento que foi realizada por meio de 3 acelerometros e 3 giroscopios.

### Esquema de Ligação (Barramento I2C)

Os dois sensores compartilham o mesmo barramento I2C configurado no firmware (`Wire.begin(6, 7)`):

| Pino do Sensor | Pino no XIAO ESP32-C3 |
| :--- | :--- |
| **VCC / VIN** | `3V3` |
| **GND** | `GND` |
| **SDA** | `D4` (`GPIO 6`) |
| **SCL** | `D5` (`GPIO 7`) |

## Arquitetura do Firmware

Para economizar energia, tolerar oscilações de Wi-Fi e respeitar a cota gratuita do Adafruit IO (**30 pontos/minuto**), o código opera em três etapas:

1. **Amostragem contínua:** Lê o pulso óptico do MAX30102 em tempo real (média móvel de 4 batimentos) e amostra a aceleração resultante do MPU-6050 a cada **50 ms**.
2. **Agregação em RAM (Épocas):** A cada **30 segundos**, salva na memória RAM a média de BPM, o pico de aceleração do intervalo e o horário exato sincronizado via **NTP** (UTC).
3. **Despacho em Lote (Batch MQTT):** A cada **5 minutos**, envia os registros acumulados em formato JSON para o broker do Adafruit IO, limitando o envio a 10 registros por ciclo (20 pontos) para evitar bloqueios por excesso de taxa.

## Bibliotecas Utilizadas

**Externas (instalar via Gerenciador de Bibliotecas da Arduino IDE):**
* `PubSubClient` (Nick O'Leary) — comunicação MQTT com o broker.
* `SparkFun MAX3010x Pulse and Proximity Sensor Library` (`MAX30105.h` e `heartRate.h`) — leitura e detecção de batimentos.

**Nativas (já incluídas no pacote ESP32 / C++):**
* `Wire.h`, `WiFi.h`, `WiFiClient.h`, `time.h` e `math.h`.
