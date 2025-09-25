#include <Arduino.h>
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <time.h>

// ====== Konfiguration ======
#define WIFI_SSID     "Waoo4920_8847"
#define WIFI_PASSWORD "ckdd4889"

// MQTT-broker
#define MQTT_HOST     "192.168.1.56"
#define MQTT_PORT     1883
#define CLIENT_ID     "optilogics-esp32-01"

// Topic
static const char* TOPIC = "spBv1.0/officeb/DDATA/ventilationchamber2/olimextemp";

// Publish-interval (sek)
static const float PUBLISH_PERIOD_SEC = 5.0f;

// ====== Globale objekter ======
AsyncMqttClient mqttClient;
Ticker wifiReconnectTimer, mqttReconnectTimer, publishTimer;

// ====== Simpel Modbus-simulering ======
struct ModbusRegs {
  // “holding registers”
  uint16_t temp10;    // temperatur * 10 (fx 245 => 24.5 C)
  uint16_t hum10;     // fugt * 10 (fx 453 => 45.3 %)
  uint16_t press10;   // tryk * 10 (fx 10125 => 1012.5 hPa)
  uint16_t rpm;       // omdrejninger
  uint16_t status;    // status-bitmaske
};

class SimModbusDataSource {
public:
  bool begin() {
    // initiale registre
    regs.temp10  = 245;    // 24.5 C
    regs.hum10   = 450;    // 45.0 %
    regs.press10 = 10125;  // 1012.5 hPa
    regs.rpm     = 1200;
    regs.status  = 0x0001; // “OK”
    lastUpdateMs = millis();
    return true;
  }

  // Kaldes fra loop/timer for at simulere “ny modbus-data”
  void tick() {
    // opdater ~hver 1000 ms
    uint32_t now = millis();
    if (now - lastUpdateMs < 1000) return;
    lastUpdateMs = now;

    auto jitter = [](int range){ return (int32_t)(esp_random() % (2*range+1)) - range; };

    int t = (int)regs.temp10  + jitter(3);    // ±0.3 C
    int h = (int)regs.hum10   + jitter(5);    // ±0.5 %
    int p = (int)regs.press10 + jitter(8);    // ±0.8 hPa
    int r = (int)regs.rpm     + jitter(30);   // ±30 rpm

    // clamp rimelige områder
    regs.temp10  = (uint16_t) constrain(t, 200, 300);     // 20.0..30.0 C
    regs.hum10   = (uint16_t) constrain(h, 300, 700);     // 30..70 %
    regs.press10 = (uint16_t) constrain(p, 9900, 10300);  // 990..1030 hPa
    regs.rpm     = (uint16_t) constrain(r, 800, 1800);    // 800..1800 rpm

    // flip en status-bit en gang imellem
    if ((esp_random() % 20) == 0) regs.status ^= 0x0002;
  }

  // Læs “registre” (som om vi spurgte en Modbus-slave)
  bool read(float& temp, float& hum, float& pres, uint16_t& rpm, uint16_t& status) {
    temp   = regs.temp10  / 10.0f;
    hum    = regs.hum10   / 10.0f;
    pres   = regs.press10 / 10.0f;
    rpm    = regs.rpm;
    status = regs.status;
    return true;
  }

private:
  ModbusRegs regs{};
  uint32_t lastUpdateMs{0};
};

SimModbusDataSource mbSim;

// ====== Fremadrettede deklarationer ======
void connectToWifi();
void connectToMqtt();
void publishPeriodic();

// ====== Hjælpefunktioner ======
uint64_t epochMs() {
  time_t nowSec = time(nullptr);
  if (nowSec > 1700000000) return (uint64_t)nowSec * 1000ULL;
  return (uint64_t)millis();
}

// ====== WiFi events ======
void WiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println(F("[WiFi] STA connected"));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print(F("[WiFi] GOT IP: "));
      Serial.println(WiFi.localIP());
      mqttReconnectTimer.detach();
      connectToMqtt();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WiFi] Disconnected, reason=%d\n", info.wifi_sta_disconnected.reason);
      mqttReconnectTimer.detach();
      publishTimer.detach();
      wifiReconnectTimer.detach();
      wifiReconnectTimer.once(2, connectToWifi);
      break;
    default: break;
  }
}

// ====== MQTT callbacks ======
void onMqttConnect(bool) {
  Serial.println(F("[MQTT] Connected to broker"));
  // Start kontinuerlig publish når vi er online
  publishTimer.detach();
  publishTimer.attach(PUBLISH_PERIOD_SEC, publishPeriodic);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.printf("[MQTT] Disconnected (reason %d)\n", (int)reason);
  publishTimer.detach();
  if (WiFi.isConnected()) mqttReconnectTimer.once(2, connectToMqtt);
}

void onMqttPublish(uint16_t packetId) {
  Serial.printf("[MQTT] Message published (packetId: %u)\n", packetId);
}

// ====== Opsætning ======
void setup() {
  Serial.begin(115200);
  delay(200);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // MQTT
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setClientId(CLIENT_ID);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  // WiFi events
  WiFi.onEvent(WiFiEventHandler);

  // Start WiFi
  connectToWifi();

  // Start “Modbus” simulering
  mbSim.begin();
}

void loop() {
  // opdater simulerede registre ca. hvert sekund
  mbSim.tick();
}

// ====== Implementering ======
void connectToWifi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.printf("[MQTT] Trying %s:%d\n", MQTT_HOST, MQTT_PORT);
  mqttClient.connect();
}

// ====== Periodisk publish (læser “Modbus” og sender JSON) ======
void publishPeriodic() {
  if (!mqttClient.connected()) return;

  float temp, hum, pres;
  uint16_t rpm, status;
  if (!mbSim.read(temp, hum, pres, rpm, status)) {
    Serial.println("[DATA] read failed");
    return;
  }

  JsonDocument doc;
  uint64_t ts = epochMs();
  doc["timestamp"] = ts;

  JsonArray metrics = doc["metrics"].to<JsonArray>();

  auto addMetric = [&](const char* name, const char* dtype, float value) {
    JsonObject m = metrics.add<JsonObject>();
    m["name"] = name;
    m["timestamp"] = ts;
    m["dataType"] = dtype;
    m["value"] = value;
  };
  auto addMetricU16 = [&](const char* name, const char* dtype, uint16_t value) {
    JsonObject m = metrics.add<JsonObject>();
    m["name"] = name;
    m["timestamp"] = ts;
    m["dataType"] = dtype;
    m["value"] = value;
  };

  addMetric("temperature", "float", temp);
  addMetric("humidity",    "float", hum);
  addMetric("pressure",    "float", pres);
  addMetricU16("rpm",      "int",   rpm);
  addMetricU16("status",   "int",   status);

  static uint32_t seq = 1;
  doc["seq"] = seq++;

  String payload;
  serializeJson(doc, payload);

  uint16_t pktId = mqttClient.publish(TOPIC, 0, false, payload.c_str(), payload.length());
  if (pktId == 0) {
    Serial.println(F("[MQTT] Periodic publish failed to queue!"));
  } else {
    Serial.printf("[MQTT] Periodic publish queued, packetId=%u T=%.1f H=%.1f P=%.1f rpm=%u st=0x%04X\n",
                  pktId, temp, hum, pres, rpm, status);
  }
}
