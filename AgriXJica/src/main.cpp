#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <LoRa.h>
#include <SoftwareSerial.h>
#include "esp_system.h"
#include <JPEGDecoder.h>

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
#define PACKET_SIZE 256
#define CHUNK_SIZE 4096
#define TARGET_WIDTH 640
#define TARGET_HEIGHT 480
#define COMPRESSION_QUALITY 10  // Lower = smaller file

SPIClass sdSPI(VSPI);
SoftwareSerial loraSerial(LORA_RX, LORA_TX);

bool resizeAndCompressImage(const char* filename, uint8_t** outputBuffer, size_t* outputSize) {
    File imgFile = SD.open(filename);
    if (!imgFile) {
        Serial.println("Failed to open image file");
        return false;
    }

    // Get file size
    size_t fileSize = imgFile.size();
    if (fileSize == 0) {
        Serial.println("Empty file");
        imgFile.close();
        return false;
    }

    // Read file into buffer
    uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
    if (!fileBuffer) {
        Serial.println("Failed to allocate file buffer");
        imgFile.close();
        return false;
    }

    if (imgFile.read(fileBuffer, fileSize) != fileSize) {
        Serial.println("Failed to read file");
        free(fileBuffer);
        imgFile.close();
        return false;
    }
    imgFile.close();

    // Decode JPEG
    if (!JpegDec.decodeArray(fileBuffer, fileSize)) {
        Serial.println("Failed to decode JPEG");
        free(fileBuffer);
        return false;
    }
    free(fileBuffer); // Free original file buffer

    // Get original dimensions
    uint16_t origWidth = JpegDec.width;
    uint16_t origHeight = JpegDec.height;
    
    // Calculate scaling factors
    float scaleX = (float)TARGET_WIDTH / origWidth;
    float scaleY = (float)TARGET_HEIGHT / origHeight;
    float scale = min(scaleX, scaleY);
    
    uint16_t newWidth = origWidth * scale;
    uint16_t newHeight = origHeight * scale;
    
    // Allocate buffer for resized image (RGB888 format)
    size_t bufferSize = newWidth * newHeight * 3;
    uint8_t* buffer = (uint8_t*)malloc(bufferSize);
    if (!buffer) {
        Serial.println("Failed to allocate resize buffer");
        return false;
    }

    // Process the image MCU by MCU
    while (JpegDec.read()) {
        uint16_t mcu_x = JpegDec.MCUx * JpegDec.MCUWidth;
        uint16_t mcu_y = JpegDec.MCUy * JpegDec.MCUHeight;
        uint16_t mcu_w = JpegDec.MCUWidth;
        uint16_t mcu_h = JpegDec.MCUHeight;
        uint16_t mcu_pixels = mcu_w * mcu_h;
        
        // Scale MCU to new size
        uint16_t scaled_x = mcu_x * scale;
        uint16_t scaled_y = mcu_y * scale;
        uint16_t scaled_w = mcu_w * scale;
        uint16_t scaled_h = mcu_h * scale;
        
        // Copy pixels to output buffer with scaling
        for (uint16_t i = 0; i < mcu_pixels; i++) {
            uint16_t x = i % mcu_w;
            uint16_t y = i / mcu_w;
            
            uint16_t new_x = scaled_x + (x * scale);
            uint16_t new_y = scaled_y + (y * scale);
            
            if (new_x < newWidth && new_y < newHeight) {
                size_t new_pos = (new_y * newWidth + new_x) * 3;
                buffer[new_pos] = JpegDec.pImage[i * 3];
                buffer[new_pos + 1] = JpegDec.pImage[i * 3 + 1];
                buffer[new_pos + 2] = JpegDec.pImage[i * 3 + 2];
            }
        }
    }

    // Compress the resized image
    *outputSize = bufferSize; // Initial size estimate
    *outputBuffer = (uint8_t*)malloc(*outputSize);
    if (!*outputBuffer) {
        Serial.println("Failed to allocate output buffer");
        free(buffer);
        return false;
    }

    // Simple RLE compression
    size_t outPos = 0;
    uint8_t count = 1;
    for (size_t i = 0; i < bufferSize - 3; i += 3) {
        if (outPos + 4 >= *outputSize) {
            // Buffer full, stop compression
            Serial.println("Output buffer full");
            free(buffer);
            free(*outputBuffer);
            *outputBuffer = NULL;
            return false;
        }
        
        if (i + 3 < bufferSize &&
            buffer[i] == buffer[i + 3] &&
            buffer[i + 1] == buffer[i + 4] &&
            buffer[i + 2] == buffer[i + 5]) {
            count++;
        } else {
            (*outputBuffer)[outPos++] = count;
            (*outputBuffer)[outPos++] = buffer[i];
            (*outputBuffer)[outPos++] = buffer[i + 1];
            (*outputBuffer)[outPos++] = buffer[i + 2];
            count = 1;
        }
    }
    *outputSize = outPos;

    free(buffer);
    Serial.printf("Image resized from %dx%d to %dx%d and compressed\n", 
                 origWidth, origHeight, newWidth, newHeight);
    return true;
}

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
        delay(5);
        LoRa.idle();
        return false;
    }
    
    if (!success && retry < MAX_RETRIES) {
        Serial.printf("Retry %d/%d\n", retry + 1, MAX_RETRIES);
        delay(5 * (retry + 1));
        return sendLoRaPacket(data, len, retry + 1);
    }
    
    return success;
}

void sendImageOverLoRa(fs::FS &fs, const char *dirname, const char *filename) {
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", dirname, filename);
    
    uint8_t* compressedImage = NULL;
    size_t compressedSize = 0;
    
    if (!resizeAndCompressImage(fullPath, &compressedImage, &compressedSize)) {
        Serial.println("Image processing failed");
        return;
    }

    Serial.printf("Original file: %s\n", fullPath);
    Serial.printf("Compressed size: %u bytes\n", compressedSize);
    
    uint32_t packets = (compressedSize + PACKET_SIZE - 1) / PACKET_SIZE;
    uint32_t failedPackets = 0;
    
    // Send file metadata
    uint8_t buffer[PACKET_SIZE + 8];
    memcpy(buffer, &compressedSize, 4);
    size_t fnLen = strlen(filename);
    memcpy(buffer + 4, filename, fnLen);
    
    if (!sendLoRaPacket(buffer, fnLen + 4)) {
        Serial.println("Failed to send file metadata");
        free(compressedImage);
        return;
    }
    delay(2);

    // Send compressed image data
    uint32_t packetIndex = 0;
    digitalWrite(LED2_PIN, HIGH);
    
    while(packetIndex * PACKET_SIZE < compressedSize && failedPackets < 3) {
        size_t bytesToSend = min((size_t)PACKET_SIZE, 
                               compressedSize - (packetIndex * PACKET_SIZE));
        
        memcpy(buffer, &packetIndex, 4);
        memcpy(buffer + 4, compressedImage + (packetIndex * PACKET_SIZE), bytesToSend);
        
        if (!sendLoRaPacket(buffer, bytesToSend + 4)) {
            Serial.printf("Failed to send packet %lu\n", packetIndex);
            failedPackets++;
            if (failedPackets >= 3) {
                Serial.println("Too many failed packets, stopping transmission");
                break;
            }
            delay(5);
            continue;
        }
        
        failedPackets = 0;
        packetIndex++;
        Serial.printf("Sent packet %lu of %lu\n", packetIndex, packets);
        digitalWrite(LED3_PIN, !digitalRead(LED3_PIN));
        delay(1);
    }
    
    free(compressedImage);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);
    
    if (failedPackets >= 3) {
        Serial.println("File transmission aborted due to errors");
    } else {
        Serial.println("File transmission complete");
    }
    
    LoRa.sleep();
    delay(5);
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
                digitalWrite(LED1_PIN, LOW);
                sendImageOverLoRa(fs, dirname, filename);
                digitalWrite(LED1_PIN, HIGH);
                delay(10);
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
        return;
    }
    Serial.println("SD Card Initialized");

    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }

    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(433E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    
    LoRa.setTxPower(20);
    LoRa.setSpreadingFactor(6);  // Fastest possible setting
    LoRa.setSignalBandwidth(500E3);  // Widest bandwidth
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    
    Serial.println("LoRa Initialized");
}

void loop() {
    digitalWrite(LED1_PIN, HIGH);
    sendImagesInDir(SD, "/DCIM/camera");
    delay(1000);
}
