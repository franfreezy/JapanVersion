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
#define LORA_RX 15
#define LORA_TX 21
#define LED1_PIN 25
#define LED2_PIN 27
#define LED3_PIN 14
#define I2C_SDA 12
#define I2C_SCL 13
#define SIGNAL_PIN 16

const int LORA_PACKET_SIZE = 240;
const int I2C_ADDRESS = 8;
SoftwareSerial LoRaSerial(LORA_RX, LORA_TX);
QueueHandle_t sensorQueue;
volatile bool imageTransmissionInProgress = false;

String encryptData(const String &data)
{
  String encrypted = "";
  int shift = 3;

  for (int i = 0; i < data.length(); i++)
  {
    char c = data[i];
    if (isalpha(c))
    {
      char base = islower(c) ? 'a' : 'A';
      c = (c - base + shift) % 26 + base;
    }
    encrypted += c;
  }

  return encrypted;
}

void imageTransmissionTask(void *pvParameters);

void commandTask(void *pvParameters)
{
  char command[10];
  int index = 0;

  

  while (true)
  {
    if (imageTransmissionInProgress)
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    while (LoRaSerial.available())
    {
      digitalWrite(LED1_PIN, HIGH);
      String command = LoRaSerial.readStringUntil('~');
      Serial.print(command);
      

        digitalWrite(LED1_PIN, LOW);

        digitalWrite(SIGNAL_PIN, HIGH);
        delay(100);
        digitalWrite(SIGNAL_PIN, LOW);

        
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void processAndSendData(const String &messageBuffer)
{
  String message = "agrix" + encryptData(messageBuffer);
  LoRa.beginPacket();
  LoRa.print(message);
  LoRa.endPacket();
  Serial.print("Forwarded telemetry data: ");
  Serial.println(message);
}

void handleI2CReceive(int bytesReceived)
{
  static String messageBuffer = "";
  char c;

  while (Wire.available())
  {
    c = Wire.read();

    if (c != '-' && c != ' ' && c != '\n' && c != '\r' && c != '\t')
    {
      messageBuffer += c;
    }

    if (c == '}')
    {
      processAndSendData(messageBuffer);
      messageBuffer = "";
    }
  }
}

void telemetryTask(void *pvParameters)
{
  while (true)
  {
    if (imageTransmissionInProgress)
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void groundSourceTask(void *pvParameters)
{
  char groundData[100];
  int index = 0;

  while (true)
  {
    if (imageTransmissionInProgress)
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    if (LoRaSerial.available())
    {
      char c = LoRaSerial.read();
      if (c == '#')
      {
        groundData[index] = '\0';
        index = 0;

        if (xQueueSend(sensorQueue, &groundData, portMAX_DELAY) != pdPASS)
        {
          Serial.println("Failed to send data to sensor queue");
        }
      }
      else
      {
        groundData[index++] = c;
      }
    }

    char sensorData[100];
    if (xQueueReceive(sensorQueue, &sensorData, portMAX_DELAY) == pdPASS)
    {
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

void imageTransmissionTask(void *pvParameters)
{
  if (!SD.exists("/DCIM/Camera/IMG_20230103_214227_442.jpg"))
  {
    Serial.println("File /DCIM/Camera/IMG_20230103_214227_442.jpg does not exist");
    imageTransmissionInProgress = false;
    vTaskDelete(NULL);
  }

  File file = SD.open("/DCIM/Camera/IMG_20230103_214227_442.jpg");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    imageTransmissionInProgress = false;
    vTaskDelete(NULL);
  }
  Serial.println("File opened successfully.");

  size_t totalSize = file.size();
  size_t bytesSent = 0;

  while (file.available())
  {
    uint8_t buffer[LORA_PACKET_SIZE];
    int bytesRead = file.read(buffer, LORA_PACKET_SIZE);

    String hexString = "image";
    for (int i = 0; i < bytesRead; i++)
    {
      if (buffer[i] < 16)
        hexString += "0";
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


void setup()
{
  // initialize the serial port
  Serial.begin(115200);

  LoRaSerial.begin(9600);

  // initialize the pins
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(SIGNAL_PIN, OUTPUT);

  // initialize the I2C bus
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(handleI2CReceive);
  while (!Serial)
    ;

  if (!SD.begin(SD_CS))
  {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    delay(1000);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    return;
  }
  digitalWrite(LED1_PIN, HIGH);
  delay(1000);

  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6))
  {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED3_PIN, HIGH);
    delay(1000);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);
    return;
  }
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, HIGH);
  delay(1000);

  digitalWrite(LED2_PIN, LOW);

  //blink thrice to indicate unsuccessful queue initialization
  sensorQueue = xQueueCreate(10, sizeof(char[100]));
  if (sensorQueue == NULL)
  {
    digitalWrite(LED1_PIN, HIGH);
    delay(300);
    digitalWrite(LED1_PIN, LOW);  
    delay(300);
    digitalWrite(LED1_PIN, HIGH);
    delay(300);
    digitalWrite(LED1_PIN, LOW);  
    delay(300);
    digitalWrite(LED1_PIN, HIGH);
    delay(300);
    digitalWrite(LED1_PIN, LOW);  
    return;
  }

  xTaskCreate(commandTask, "Command Task", 4096, NULL, 1, NULL);
  xTaskCreate(telemetryTask, "Telemetry Task", 4096, NULL, 1, NULL);
  xTaskCreate(groundSourceTask, "Ground Source Task", 4096, NULL, 1, NULL);
}

void loop()
{
}