#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#define LORA_CS 5
#define LORA_RST 4
#define LORA_DIO0 26

void setup() {
    Serial.begin(115200);
    
    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    
    if (!LoRa.begin(433E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    
    
    
    Serial.println("LoRa Initialized");
}

void loop() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        while (LoRa.available()) {
            Serial.print((char)LoRa.read());
        }
        Serial.println();
    }
}
