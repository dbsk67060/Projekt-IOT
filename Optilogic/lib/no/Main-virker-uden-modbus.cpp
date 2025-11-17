#include <Arduino.h>
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <time.h>

// ====== Konfiguration ======
#define WIFI_SSID     "Waoo4920_8847"
#define WIFI_PASSWORD "ckdd4889"

// MQTT-broker (tilpas efter behov)
#define MQTT_HOST     "192.168.1.56"
#define MQTT_PORT     1883
#define CLIENT_ID     "optilogics-esp32-01" // unik ID for denne klient

// Sparkplug-lignende topic
static const char* TOPIC = "spBv1.0/officeb/DDATA/ventilationchamber2/olimextemp";

// ====== Publish interval ======
static const float PUBLISH_PERIOD_SEC = 5.0f;  // send hvert 5. sekund

// ====== Globale objekter ======
AsyncMqttClient mqttClient;
Ticker wifiReconnectTimer;
Ticker mqttReconnectTimer;
Ticker publishTimer;                 // <-- NY: periodisk publish

// ====== Fremadrettede deklarationer ======
void connectToWifi();
void connectToMqtt();
void publishInitialJson();
void publishAltJson();
void publishPeriodic();              // <-- NY

// Simulerede funktioner (tilpas til dine sensorer)
uint64_t sampleTime();
float measureTemp();

// ====== Simulerings-state ======
static float simTemp = 24.0f;        // startværdi for “random walk”
static uint32_t seq = 3;             // Sparkplug-lignende sekvensnummer

// ====== WiFi events ======
void WiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println(F("[WiFi] STA connected"));
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print(F("[WiFi] GOT IP: "));
      Serial.println(WiFi.localIP());
      // Når WiFi er forbundet -> opret MQTT-forbindelse
      mqttReconnectTimer.detach();
      connectToMqtt();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WiFi] Disconnected, reason=%d\n", info.wifi_sta_disconnected.reason);
      // stop evt. MQTT-reconnect og prøv WiFi igen om lidt
      mqttReconnectTimer.detach();
      wifiReconnectTimer.detach();
      publishTimer.detach();         // <-- stop publikations-timer når WiFi ryger
      wifiReconnectTimer.once(2, connectToWifi);
      break;

    default:
      break;
  }
}

// ====== MQTT callbacks ======
void onMqttConnect(bool /*sessionPresent*/) {
  Serial.println(F("[MQTT] Connected to broker"));

  // Engangs-publikationer som før
  publishInitialJson();
  publishAltJson();

  // Start kontinuerlig publikations-timer
  publishTimer.detach();
  publishTimer.attach(PUBLISH_PERIOD_SEC, publishPeriodic);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.printf("[MQTT] Disconnected (reason %d)\n", (int)reason);
  publishTimer.detach();             // <-- stop kontinuerlige publish ved disconnect
  // Prøv at forbinde igen, hvis WiFi er oppe
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.printf("[MQTT] Message published (packetId: %u)\n", packetId);
}

// ====== Opsætning ======
void setup() {
  Serial.begin(115200);
  delay(200);

  // NTP (for at få epoch tid i sekunder)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC

  // MQTT: konfigurer én gang
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setClientId(CLIENT_ID);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  // WiFi events: registrér én gang
  WiFi.onEvent(WiFiEventHandler);

  // Start WiFi
  connectToWifi();
}

void loop() {
  // Intet i loop: alt kører async
}

// ====== Implementering ======
void connectToWifi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);

  // 1) Init/Mode først
  WiFi.persistent(false);        // undgå flash writes
  WiFi.mode(WIFI_STA);           // init og sæt STA-mode
  WiFi.setSleep(false);          // mere stabilt under MQTT

  // 2) Ryd evt. gammel forbindelse (uden deinit)
  WiFi.disconnect(false, false); // IKKE (true, true)

  // 3) Start forbindelse
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.printf("[MQTT] Trying %s:%d\n", MQTT_HOST, MQTT_PORT);
  mqttClient.connect();
}

// ====== JSON builders (ArduinoJson v7-stil) ======
static String buildInitialJson() {
  // Matcher dit rå JSON fra Paho-eksemplet
  JsonDocument doc;
  doc["timestamp"] = 1486144502122ULL;

  JsonArray metrics = doc["metrics"].to<JsonArray>();
  JsonObject m = metrics.add<JsonObject>();
  m["name"] = "temperature";
  m["alias"] = 1;
  m["timestamp"] = 1479123452194ULL;
  m["dataType"] = "integer";
  m["value"] = "25.5";  // bevidst string for at spejle dit eksempel

  doc["seq"] = 2;

  String out;
  serializeJson(doc, out);  // kompakt (ingen pretty print)
  return out;
}

static String buildAltJson() {
  JsonDocument doc;

  // timestamp i millisekunder
  doc["timestamp"] = sampleTime();

  JsonArray metrics = doc["metrics"].to<JsonArray>();
  JsonObject m = metrics.add<JsonObject>();
  m["name"] = "temperature";
  m["timestamp"] = sampleTime();
  m["dataType"] = "float";
  m["value"] = measureTemp(); // numerisk værdi

  // Sekvensnummer (kan styres globalt)
  doc["seq"] = seq++;

  String out;
  serializeJson(doc, out);
  return out;
}

void publishInitialJson() {
  String payload = buildInitialJson();
  uint16_t pktId = mqttClient.publish(TOPIC, 0, false, payload.c_str(), payload.length());
  if (pktId == 0) {
    Serial.println(F("[MQTT] Publish (initial) failed to queue!"));
  } else {
    Serial.printf("[MQTT] Queued initial publish, packetId=%u\n", pktId);
  }
}

void publishAltJson() {
  String payload = buildAltJson();
  uint16_t pktId = mqttClient.publish(TOPIC, 0, false, payload.c_str(), payload.length());
  if (pktId == 0) {
    Serial.println(F("[MQTT] Publish (alt) failed to queue!"));
  } else {
    Serial.printf("[MQTT] Queued alt publish, packetId=%u\n", pktId);
  }
}

// ====== Kontinuerlig publish ======
void publishPeriodic() {
  // Beskyt mod race: kun forsøg hvis MQTT faktisk er forbundet
  if (!mqttClient.connected()) return;

  // Opdatér simuleret temperatur lidt (random walk)
  // delta i intervallet ~[-0.2, +0.2]
  float delta = ((int32_t)esp_random() % 41 - 20) / 100.0f;
  simTemp += delta;
  // clamp til et rimeligt område
  if (simTemp < 20.0f) simTemp = 20.0f;
  if (simTemp > 30.0f) simTemp = 30.0f;

  // midlertidigt “overstyr” measureTemp() for feed
  // (eller ændr measureTemp() hvis du foretrækker det)
  float saved = simTemp; // bare for tydelighed
  (void)saved;

  // Byg og send
  String payload = buildAltJson(); // buildAltJson kalder measureTemp(), som bruger simTemp
  uint16_t pktId = mqttClient.publish(TOPIC, 0, false, payload.c_str(), payload.length());
  if (pktId == 0) {
    Serial.println(F("[MQTT] Periodic publish failed to queue!"));
  } else {
    Serial.printf("[MQTT] Periodic publish queued, packetId=%u temp=%.2f\n", pktId, simTemp);
  }
}

// ====== Hjælpefunktioner ======
uint64_t epochMs() {
  // Returnér epoch i ms hvis vi har NTP-tid, ellers millis siden boot
  time_t nowSec = time(nullptr);
  if (nowSec > 1700000000) { // grov sanity-check (≈2023+)
    return (uint64_t)nowSec * 1000ULL;
  }
  return (uint64_t)millis();
}

uint64_t sampleTime() {
  return epochMs();
}

float measureTemp() {
  // Brug den simulerede temperatur
  return simTemp;
}
