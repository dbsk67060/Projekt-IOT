#include <ModbusMaster.h> 
// Opret ModbusMaster instans 
ModbusMaster node; 

// RS485 pin-konfiguration for Olimex ESP32-PoE 
#define RXD2 16 
#define TXD2 17 
#define RS485_DIR 22  // DE/RE styring 
 
void preTransmission() { 
  digitalWrite(RS485_DIR, HIGH);  // Aktiver sendetilstand 
} 
 
void postTransmission() { 
  digitalWrite(RS485_DIR, LOW);   // Tilbage til modtagelse 
} 
 
void setup() { 
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); // UART2 til RS485 
 
  // RS485 styring 
  pinMode(RS485_DIR, OUTPUT); 
  digitalWrite(RS485_DIR, LOW); 
 
  // Start Modbus som master, slave-ID = 1 
  node.begin(1, Serial2); 
 
  // Knyt callback-funktioner 
  node.preTransmission(preTransmission); 
  node.postTransmission(postTransmission); 
 
  Serial.println("Modbus RTU startes på Olimex ESP32-PoE..."); 
} 
 
void loop() { 
  uint8_t result; 
  uint16_t data; 
  int attempts = 0; 
  const int maxAttempts = 3; 
 
  while (attempts < maxAttempts) { 
    result = node.readHoldingRegisters(0, 1);  // læs 1 register fra adresse 0 
 
    if (result == node.ku8MBSuccess) { 
      data = node.getResponseBuffer(0); 
      Serial.print("Registerværdi: "); 
      Serial.println(data); 
      break;  // Succes, forlad løkken 
    } else { 
      Serial.print("Fejl ved læsning (kode "); 
      Serial.print(result); 
      Serial.println("). Forsøger igen..."); 
      attempts++; 
      delay(1000); 
    } 
  } 
 
  if (attempts == maxAttempts) { 
    Serial.println("� � Maksimale antal forsøg nået. Ingen forbindelse til slave.");
    } 
delay(2000); // Vent før næste læsning 
}