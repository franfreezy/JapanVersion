#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// Pin definitions from AgriXJica
#define LORA_CS 5
#define LORA_RST 4
#define LORA_DIO0 26
#define LED1_PIN 25

unsigned long lastSendTime = 0;
const long interval = 4;
int counter = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial);
    
    pinMode(LED1_PIN, OUTPUT);
    
    // Initialize LoRa
    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    
    if (!LoRa.begin(433E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    
 
    Serial.println("LoRa Initialized - Ready to send test packets");
}

void loop() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastSendTime > interval) {
        // Prepare test message
        String message = "Test Packet #" + String(counter++);
        
        // Send packet
        LoRa.beginPacket();
        LoRa.print(message);
        LoRa.endPacket();
        
        // Visual feedback
        digitalWrite(LED1_PIN, HIGH);
        Serial.print("Sent packet: ");
        Serial.println(message);
        
        lastSendTime = currentTime;
        digitalWrite(LED1_PIN, LOW);
    }
}