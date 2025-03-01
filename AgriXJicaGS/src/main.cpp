#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX2 17
#define TX2 16

SoftwareSerial loraSerial(RX2, TX2);

void setup()
{
  Serial.begin(115200);
  loraSerial.begin(9600);
  Serial.println("LoRa Receiver Ready...");
}

void loop()
{
  if (loraSerial.available())
  {
    Serial.println("receivedData");
    String receivedData = loraSerial.readStringUntil('\n');
    
    Serial.println(receivedData);
    loraSerial.flush();
  }
}
