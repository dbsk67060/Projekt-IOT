#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>

// NOTE: Ensure PubSubClient library is installed (Arduino Library Manager or PlatformIO lib_deps).
// ================= MODBUS / RS485 CONFIG =================
#define RX_PIN 36     //kommunikations pin for modtagelse af data ESP32
#define TX_PIN 4   //kommunikations pin afsendelse for ESP32
#define MAX485_DE 5 //tænder reciever
#define MAX485_RE_NEG 14 //slukker reciever 
#define BAUD_RATE 9600 // Modbus standard baud rate
#define MODBUS_SLAVE_ID 1 // Slave ID ventilatoren 

ModbusMaster modbus;

// ================= WiFi + MQTT =================
const char* WIFI_SSID = "FMS"; // wifi navn 
const char* WIFI_PASS = "FMS12345"; // wife kode

const char* MQTT_HOST = "192.168.3.100";   // Mosquitto broker
const int   MQTT_PORT = 1883; //port på broker 

WiFiClient espClient; // WiFi client til MQTT
PubSubClient mqtt(espClient); // MQTT client

// Sparkplug topics (samme format som emulator)
const char* GROUP  = "plantA"; //gruppe område topic
const char* DEVICE = "olimex-device"; //device id topic 

String TOP_DBIRTH = String("spBv1.0/") + GROUP + "/DBIRTH/" + DEVICE;  //dbirth topic
String TOP_DDATA  = String("spBv1.0/") + GROUP + "/DDATA/"  + DEVICE; //ddata topic
String TOP_DDEATH = String("spBv1.0/") + GROUP + "/DDEATH/" + DEVICE; //ddeath topic


// ================= RS485 CONTROL =================
void preTransmission() { 
  digitalWrite(MAX485_RE_NEG, HIGH);  //forbered afsendelse
  digitalWrite(MAX485_DE, HIGH);       //forbered afsendelse
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);  //forbered modtagelse
  digitalWrite(MAX485_DE, LOW);     //forbered modtagelse
}

// ================= GENERIC MODBUS =================
bool readRegistersBlock(uint16_t regs[11]) {   // Læs 11 input registre fra starter ved adressen 10
  uint8_t result = modbus.readInputRegisters(10, 11);
  if (result != modbus.ku8MBSuccess) return false;   // hvis Læsning fejlede return false
  for (int i = 0; i < 11; i++) {  // gem værdier i array
    regs[i] = modbus.getResponseBuffer(i); // hent register værdi og gem i array
  }
  return true; // læsning succesfuld
}

// Extract values
float getTemperature(uint16_t regs[11]) { return regs[9] / 10.0f; } //få temperatur fra array
float getPressure(uint16_t regs[11])    { return regs[3] / 10.0f; } // få tryk fra array
float getAirFlow(uint16_t regs[11])     { return regs[5]; }      // få airflow fra array

// ================= SPECIALIZED FUNCTIONS =================
void fanStart() {
  Serial.println("Starter ventilation (fanStart)");
  uint8_t result = modbus.writeSingleRegister(367, 0); // Skriv til holding register 367 med værdien 0 for sluk og 3 for start.
  if (result == modbus.ku8MBSuccess) {
    Serial.println("Ventilation startet: register skriv ok");  //hvis skrivning succesfuld
  } else {
    Serial.print("Ventilation start fejlede, modbus fejlkode: "); //hvis skrivning fejlede½
    Serial.println(result); //vis resultat i terminal 
  }
}

// ================= BUILD JSON PAYLOAD =================
String makeJsonPayload(float t, float p, float rpm) { // lav json payload
    String json = "{";
    json += "\"temp\":" + String(t, 1) + ",";      // tilføj temperatur til json
    json += "\"tryk\":" + String(p, 1) + ",";    // tilføj tryk til json (string(p, 1) konverterer float til string med 1 decimal)
    json += "\"rpm\":"  + String((int)rpm);       // tilføj rpm til json
    json += "}"; //afslut json
    return json;   // return json string til kaldende funktion
}

// ================= MQTT CONNECT =================
void mqttReconnect() {   // forsøg at forbinde til MQTT broker
  Serial.println("MQTT: Forsøger at forbinde til broker..."); // besked til terminal
  while (!mqtt.connected()) { // mens ikke forbundet
    Serial.println("MQTT: Ikke forbundet, prøver igen..."); // besked til terminal 
    if (mqtt.connect("olimex-client", NULL, NULL, 
                     TOP_DDEATH.c_str(), 1, false, "DDEATH")) { //hvis forbundet send ddeath besked

      mqtt.publish(TOP_DBIRTH.c_str(), "DBIRTH", false); //efer ddeath send dbirth besked
      Serial.println("MQTT: DBIRTH sendt"); // besked til terminal
      Serial.println("MQTT: Forbundet til broker!"); // besked til terminal
    }
    delay(1000); // vent 1 sekund før næste forsøg
  }
}

// ================= SETUP =================
void setup() {  // opsætning
  pinMode(MAX485_RE_NEG, OUTPUT); //opsæt RE pin
  pinMode(MAX485_DE, OUTPUT); //opsæt DE pin
  digitalWrite(MAX485_RE_NEG, LOW); // sæt RE til modtagelse
  digitalWrite(MAX485_DE, LOW); // sæt DE til modtagelse

  Serial.begin(115200); // start serial monitor
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN); // start serial2 til modbus kommunikation

  modbus.begin(MODBUS_SLAVE_ID, Serial2); // start modbus med slave id og serial2
  modbus.preTransmission(preTransmission); // sæt preTransmission callback
  modbus.postTransmission(postTransmission); // sæt postTransmission callback

  // Start ventilation kort efter Modbus init
  fanStart(); //tidligere defineret start fan
  delay(300); // vent lidt for stabilitet

  WiFi.begin(WIFI_SSID, WIFI_PASS); //forbind til wifi 
  Serial.println("Forbinder til WiFi...");  //printes i terminal på pc
  while (WiFi.status() != WL_CONNECTED) { // mens ikke forbundet til wifi
    Serial.print("."); // print punktum i terminal
    delay(200); // vent 200 ms
  }
  Serial.println(); // ny linje i terminal
  Serial.print("WiFi forbundet. IP: "); // print besked i terminal
  Serial.println(WiFi.localIP()); // print lokal ip adresse i terminal

  mqtt.setServer(MQTT_HOST, MQTT_PORT); // sæt mqtt broker server og port 
}

// ================= LOOP =================
void loop() {
  if (!mqtt.connected()) mqttReconnect(); // hvis ikke forbundet til mqtt broker, forsøg at forbinde
  mqtt.loop();

  uint16_t regs[11] = {0}; // array til modbus registre
  if (!readRegistersBlock(regs)) { // læs registre, hvis fejler:
    delay(2000); // vent 2 sekunder
    return; // afslut loop og prøv igen
  }

  float t  = getTemperature(regs); // få temperatur fra arrays, funktioner defineret på  linje 58 til 60
  float p  = getPressure(regs); // få tryk fra arrays
  float af = getAirFlow(regs); // få airflow fra arrays

  String payload = makeJsonPayload(t, p, af); // lav json payload
  Serial.print("Sender payload: "); // besked til terminal
  Serial.println(payload); // vis payload i terminal
  mqtt.publish(TOP_DDATA.c_str(), payload.c_str(), false); // send data-payload til mqtt broker 
  Serial.println("Payload sendt til MQTT broker."); // besked til terminal


  delay(2000);
}

