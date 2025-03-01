#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <LoRa.h>
#include <SoftwareSerial.h>

#define SD_CS 5  
#define LORA_CS 4  
#define LORA_RST 14  
#define LORA_DIO0 2  

#define LORA_RX 16   
#define LORA_TX 17   

#define RX2 16   
#define TX2 17   

#define LED_PIN 13  

SoftwareSerial loraSerial(LORA_RX, LORA_TX); 

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);
    Serial2.printf("Listing directory: %s\n", dirname);
    loraSerial.print("Listing directory: ");  
    loraSerial.println(dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        Serial2.println("Failed to open directory");
        loraSerial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        Serial2.println("Not a directory");
        loraSerial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("DIR : ");
            Serial.println(file.name());
            Serial2.print("DIR : ");
            Serial2.println(file.name());
            loraSerial.print("DIR : ");
            loraSerial.println(file.name());

            if (levels > 0) {
                listDir(fs, file.name(), levels - 1);
            }
        } else {
            Serial.print("FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());

            Serial2.print("FILE: ");
            Serial2.print(file.name());
            Serial2.print("  SIZE: ");
            Serial2.println(file.size());

            loraSerial.print("FILE: ");
            loraSerial.print(file.name());
            loraSerial.print("  SIZE: ");
            loraSerial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, TX2, RX2);
    loraSerial.begin(9600); 

    pinMode(SD_CS, OUTPUT);
    pinMode(LORA_CS, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    digitalWrite(LORA_CS, HIGH);
    digitalWrite(LED_PIN, LOW);

    if (!SD.begin(SD_CS)) {
        Serial.println("Card Mount Failed");
        Serial2.println("Card Mount Failed");
        loraSerial.println("Card Mount Failed");
        return;
    }
    Serial.println("SD Card Initialized");
    Serial2.println("SD Card Initialized");
    loraSerial.println("SD Card Initialized");

    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(915E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    Serial.println("LoRa Initialized");
    loraSerial.println("LoRa Initialized");
}

void loop() {
    Serial.println("\n--- Listing DCIM Directory ---");
    
    loraSerial.println("\n--- Listing DCIM Directory ---");
    
    delay(50); /
    Serial.println("\n--- Listing DCIM/Camera Directory ---");
   
    loraSerial.println("\n--- Listing DCIM/Camera Directory ---");
    

    
    digitalWrite(LED_PIN, HIGH);
    delay(2000);
    digitalWrite(LED_PIN, LOW);

    delay(5000); 
}
