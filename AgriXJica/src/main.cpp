#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <LoRa.h>
#include <SoftwareSerial.h>
#include "esp_system.h"

#define SD_CS 22 
#define LORA_CS 5  
#define LORA_RST 4  
#define LORA_DIO0 26
#define LORA_RX 21  
#define LORA_TX 15  
#define LED1_PIN 25
#define LED2_PIN 27
#define LED3_PIN 14
#define PACKET_TIMEOUT 3000
#define MAX_RETRIES 3

SPIClass sdSPI(VSPI);
SoftwareSerial loraSerial(LORA_RX, LORA_TX); 

bool sendLoRaPacket(const uint8_t* data, size_t len, int retry = 0) {
    if (retry >= MAX_RETRIES) {
        return false;
    }
    
    unsigned long startTime = millis();
    LoRa.beginPacket();
    LoRa.write(data, len);
    bool success = LoRa.endPacket();
    
    if (millis() - startTime > PACKET_TIMEOUT) {
        Serial.println("Packet send timeout");
        LoRa.sleep();
        delay(100);
        LoRa.idle();
        return false;
    }
    
    if (!success && retry < MAX_RETRIES) {
        Serial.printf("Retry %d/%d\n", retry + 1, MAX_RETRIES);
        delay(50 * (retry + 1));
        return sendLoRaPacket(data, len, retry + 1);
    }
    
    return success;
}

void sendImageOverLoRa(fs::FS &fs, const char *dirname, const char *filename) {
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", dirname, filename);
    
    File file = fs.open(fullPath);
    if(!file) {
        Serial.printf("Failed to open file: %s\n", fullPath);
        return;
    }

    const int PACKET_SIZE = 256;
    uint8_t buffer[PACKET_SIZE + 8];
    size_t fileSize = file.size();
    uint32_t packets = (fileSize + PACKET_SIZE - 1) / PACKET_SIZE;
    uint32_t failedPackets = 0;
    
    Serial.printf("Starting transmission of %s (%lu bytes)\n", filename, fileSize);
    
    memcpy(buffer, &fileSize, 4);
    size_t fnLen = strlen(filename);
    memcpy(buffer + 4, filename, fnLen);
    
    if (!sendLoRaPacket(buffer, fnLen + 4)) {
        Serial.println("Failed to send file metadata");
        file.close();
        return;
    }
    delay(20);

    uint32_t packetIndex = 0;
    digitalWrite(LED2_PIN, HIGH);  // Turn on LED2 during transmission
    
    while(file.available() && failedPackets < 3) {
        int bytesRead = file.read(buffer + 4, PACKET_SIZE);
        if(bytesRead > 0) {
            memcpy(buffer, &packetIndex, 4);
            
            if (!sendLoRaPacket(buffer, bytesRead + 4)) {
                Serial.printf("Failed to send packet %lu\n", packetIndex);
                failedPackets++;
                if (failedPackets >= 3) {
                    Serial.println("Too many failed packets, stopping transmission");
                    break;
                }
                delay(100);
                continue;
            }
            
            failedPackets = 0;
            packetIndex++;
            Serial.printf("Sent packet %lu of %lu\n", packetIndex, packets);
            digitalWrite(LED3_PIN, !digitalRead(LED3_PIN));  // Toggle LED3 for each successful packet
            delay(10);
        }
    }
    
    file.close();
    digitalWrite(LED2_PIN, LOW);  // Turn off LED2 when transmission ends
    digitalWrite(LED3_PIN, LOW);  // Turn off LED3
    
    if (failedPackets >= 3) {
        Serial.println("File transmission aborted due to errors");
    } else {
        Serial.println("File transmission complete");
    }
    
    LoRa.sleep();
    delay(100);
    LoRa.idle();
}

void sendImagesInDir(fs::FS &fs, const char *dirname) {
    File root = fs.open(dirname);
    if(!root || !root.isDirectory()) {
        Serial.printf("Failed to open directory: %s\n", dirname);
        return;
    }

    File file = root.openNextFile();
    while(file) {
        if(!file.isDirectory()) {
            const char* filename = file.name();
            if(strstr(filename, ".jpg") || strstr(filename, ".jpeg") || strstr(filename, ".png")) {
                Serial.printf("Sending image: %s\n", filename);
                digitalWrite(LED1_PIN, LOW);  // Turn off LED1 when starting transmission
                sendImageOverLoRa(fs, dirname, filename);
                digitalWrite(LED1_PIN, HIGH);  // Turn LED1 back on after transmission
                delay(500);
            }
        }
        file = root.openNextFile();
    }
}

void setup() {
    Serial.begin(115200);
    loraSerial.begin(9600); 

    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    pinMode(LED3_PIN, OUTPUT);
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);

    pinMode(SD_CS, OUTPUT);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    digitalWrite(LORA_CS, HIGH);

    sdSPI.begin(18, 19, 23, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) {
        Serial.println("Card Mount Failed");
        loraSerial.println("Card Mount Failed");
        return;
    }
    Serial.println("SD Card Initialized");
    loraSerial.println("SD Card Initialized");

    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC) Serial.println("MMC");
    else if(cardType == CARD_SD) Serial.println("SDSC");
    else if(cardType == CARD_SDHC) Serial.println("SDHC");
    else Serial.println("UNKNOWN");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(433E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    
    LoRa.setTxPower(20);
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    
    Serial.println("LoRa Initialized");
}

void loop() {
    digitalWrite(LED1_PIN, HIGH);  // LED1 on while searching for images
    sendImagesInDir(SD, "/DCIM/camera");
    delay(10000);
}
