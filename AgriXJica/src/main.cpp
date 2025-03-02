#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <LoRa.h>
#include <SoftwareSerial.h>
#include "esp_system.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define SD_CS 22
#define LORA_CS 5
#define LORA_RST 4
#define LORA_DIO0 26
#define LORA_RX 21
#define LORA_TX 15
#define LED1_PIN 25
#define LED2_PIN 27
#define I2C_SDA 12
#define I2C_SCL 13
#define SIGNAL_PIN 14

const int LORA_PACKET_SIZE = 240;
SoftwareSerial LoRaSerial(LORA_RX, LORA_TX);
QueueHandle_t sensorQueue;
volatile bool imageTransmissionInProgress = false;

String encryptData(const String& data) {
    String encrypted = "";
    int shift = 3;

    for (int i = 0; i < data.length(); i++) {
        char c = data[i];
        if (isalpha(c)) {
            char base = islower(c) ? 'a' : 'A';
            c = (c - base + shift) % 26 + base;
        }
        encrypted += c;
    }

    return encrypted;
}

void imageTransmissionTask(void *pvParameters);

void commandTask(void *pvParameters) {
  char command[10];
  int index = 0;

  LoRaSerial.begin(9600);
  Wire.begin(I2C_SDA, I2C_SCL);

  while (true) {
    if (imageTransmissionInProgress) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    if (LoRaSerial.available()) {
      digitalWrite(LED1_PIN, HIGH);
      char c = LoRaSerial.read();
      if (c == '\n') {
        command[index] = '\0';
        index = 0;

        Wire.beginTransmission(0x08);
        Wire.write(command);
        Wire.endTransmission();
        digitalWrite(LED1_PIN, LOW);

        digitalWrite(SIGNAL_PIN, HIGH);
        delay(100);
        digitalWrite(SIGNAL_PIN, LOW);

        if (strcmp(command, "SI") == 0) {
          imageTransmissionInProgress = true;
          xTaskCreate(imageTransmissionTask, "Image Transmission Task", 8192, NULL, 1, NULL);
        }
      } else {
        command[index++] = c;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void receiveEvent(int howMany) {
  if (imageTransmissionInProgress) {
    return;
  }

  char telemetryData[100];
  int index = 0;

  while (Wire.available()) {
    char c = Wire.read();
    telemetryData[index++] = c;
    if (c == '\n') {
      telemetryData[index] = '\0';
      index = 0;

      String message = "agrix" + encryptData(String(telemetryData));
      LoRa.beginPacket();
      LoRa.print(message);
      LoRa.endPacket();
      Serial.print("Forwarded telemetry data: ");
      Serial.println(message);
    }
  }
}

void telemetryTask(void *pvParameters) {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.onReceive(receiveEvent);

  while (true) {
    if (imageTransmissionInProgress) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void groundSourceTask(void *pvParameters) {
  char groundData[100];
  int index = 0;

  while (true) {
    if (imageTransmissionInProgress) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    if (LoRaSerial.available()) {
      char c = LoRaSerial.read();
      if (c == '#') {
        groundData[index] = '\0';
        index = 0;

        if (xQueueSend(sensorQueue, &groundData, portMAX_DELAY) != pdPASS) {
          Serial.println("Failed to send data to sensor queue");
        }
      } else {
        groundData[index++] = c;
      }
    }

    char sensorData[100];
    if (xQueueReceive(sensorQueue, &sensorData, portMAX_DELAY) == pdPASS) {
      String message = "ground" + encryptData(String(sensorData));
      LoRa.beginPacket();
      LoRa.print(message);
      LoRa.endPacket();
      Serial.print("Forwarded ground data: ");
      Serial.println(message);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void imageTransmissionTask(void *pvParameters) {
  if (!SD.exists("/DCIM/Camera/IMG_20230103_214227_442.jpg")) {
    Serial.println("File /DCIM/Camera/IMG_20230103_214227_442.jpg does not exist");
    imageTransmissionInProgress = false;
    vTaskDelete(NULL);
  }

  File file = SD.open("/DCIM/Camera/IMG_20230103_214227_442.jpg");
  if (!file) {
    Serial.println("Failed to open file for reading");
    imageTransmissionInProgress = false;
    vTaskDelete(NULL);
  }
  Serial.println("File opened successfully.");

  size_t totalSize = file.size();
  size_t bytesSent = 0;

  while (file.available()) {
    uint8_t buffer[LORA_PACKET_SIZE];
    int bytesRead = file.read(buffer, LORA_PACKET_SIZE);

    String hexString = "image";
    for (int i = 0; i < bytesRead; i++) {
      if (buffer[i] < 16) hexString += "0";
      hexString += String(buffer[i], HEX);
    }

    LoRa.beginPacket();
    LoRa.print(hexString);
    LoRa.endPacket();

    bytesSent += bytesRead;
    Serial.print("Sent ");
    Serial.print(bytesSent);
    Serial.print("/");
    Serial.println(totalSize);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  file.close();
  Serial.println("Image sent successfully");
  imageTransmissionInProgress = false;
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
    return;
  }
  Serial.println("SD Card initialized.");

  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    return;
  }
  Serial.println("LoRa initialized.");

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(SIGNAL_PIN, OUTPUT);

  sensorQueue = xQueueCreate(10, sizeof(char[100]));
  if (sensorQueue == NULL) {
    Serial.println("Failed to create sensor queue");
    return;
  }

  xTaskCreate(commandTask, "Command Task", 4096, NULL, 1, NULL);
  xTaskCreate(telemetryTask, "Telemetry Task", 4096, NULL, 1, NULL);
  xTaskCreate(groundSourceTask, "Ground Source Task", 4096, NULL, 1, NULL);
}

void loop() {
}