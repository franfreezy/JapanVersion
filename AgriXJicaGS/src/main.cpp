#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

#define LORA_CS 5
#define LORA_RST 4
#define LORA_DIO0 26

#define XOR_KEY 0xAB
String processedData, processedLOC, processedImageData;

#define GreenPin 12
#define BluePin 13
#define RedPin 14

// FreeRTOS Task Handles
TaskHandle_t fetchTaskHandle;
TaskHandle_t serialListenTaskHandle;

// Define LoRa SoftwareSerial pins
#define RX_PIN 21 // Receive pin for LoRa
#define TX_PIN 15
SoftwareSerial mySerial(RX_PIN, TX_PIN); // LoRa communication

// Wi-Fi credentials
const char *ssid = "Nyakanga";
const char *password = "everydayiambuffering";

// API endpoints
const char *telemetryAPI = "https://agroxsat.onrender.com/backendapi/telemetry/";
const char *payloadAPI = "https://agroxsat.onrender.com/backendapi/payload/";
const char *GSLOCAPI = "https://agroxsat.onrender.com/backendapi/setGS/";
const char *SATLOCAPI = "https://agroxsat.onrender.com/backendapi/sat/";
const char *commandAPI = "https://agroxsat.onrender.com/backendapi/command/";

// Buffer for received data
String receivedData = "";
volatile bool listeningFlag = true; // Flag to control serial listening

// Function declarations
String decryptData(const String& data);
void processReceivedData(const String& data, String& processedData, String& processedLOC);
void processGroundData(const String& data, String& processedGroundData);
void processImageData(const String& data, String& processedImageData);

void fetchTask(void *parameter) {
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.begin(commandAPI);
            int httpResponseCode = http.GET();

            if (httpResponseCode > 0) {
                String response = http.getString();
                StaticJsonDocument<1024> doc;
                DeserializationError error = deserializeJson(doc, response);

                if (!error) {
                    if (!doc["command"].isNull()) {
                        digitalWrite(RedPin, HIGH);
                        String command = doc["command"];
                        command.toLowerCase(); // Convert to lowercase for case-insensitive matching

                        // Map full-text commands to shorthand notations
                        if (command == "capture image") {
                            command = "CI~";
                        } else if (command == "adjust orbit") {
                            command = "CO~";
                        } else if (command == "send data") {
                            command = "CD~";
                        } else if (command == "hello") {
                            command = "CH~";
                        } else if (command == "data") {
                            command = "GD~";
                        } else if (command == "buzzer") {
                            command = "GB~";
                        } else if (command == "pump") {
                            command = "GP~";
                        } else {
                            Serial.println("Unknown command received: " + command);
                            digitalWrite(RedPin, LOW);
                            vTaskDelay(pdMS_TO_TICKS(10000)); 
                            continue;
                        }

                        Serial.println("Command to send: " + command);
                        mySerial.println(command);
                        digitalWrite(RedPin, LOW);
                        listeningFlag = true;
                    }
                } else {
                    Serial.print("JSON Deserialization Error: ");
                    Serial.println(error.f_str());
                }
            } else {
                Serial.print("Error on HTTP request: ");
                Serial.println(httpResponseCode);
            }
            http.end();
        } else {
            Serial.println("WiFi not connected");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay for 10 seconds
    }
}

void serialListenTask(void *parameter) {
    while (true) {
        if (listeningFlag) {
            int packetSize = LoRa.parsePacket();
            if (packetSize) {
                Serial.print("Packet received, size: ");
                Serial.println(packetSize);
                while (LoRa.available()) {
                    char c = (char)LoRa.read();
                    receivedData += c;
                }
                Serial.print("Received: ");
                Serial.println(receivedData);

                if (receivedData.endsWith("#")) {
                    if (receivedData.startsWith("agrix")) {
                        // Process telemetry data
                        String telemetryData = receivedData.substring(5); // Remove "agrix" prefix
                        telemetryData = decryptData(telemetryData); // Decrypt the data
                        processReceivedData(telemetryData, processedData, processedLOC);
                    } else if (receivedData.startsWith("image")) {
                        // Process image data
                        String imageData = receivedData.substring(5); // Remove "image" prefix
                        processImageData(imageData, processedImageData);
                    } else if (receivedData.startsWith("ground")) {
                        // Process ground data
                        String groundData = receivedData.substring(6); // Remove "ground" prefix
                        groundData = decryptData(groundData); // Decrypt the data
                        processGroundData(groundData, processedData);
                    } else {
                        Serial.println("Unknown data received");
                    }

                    receivedData = "";
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Simple Caesar Cipher for decryption
String decryptData(const String& data) {
    String decrypted = "";
    int shift = 3; // Shift for Caesar cipher

    for (int i = 0; i < data.length(); i++) {
        char c = data[i];
        // Decrypt only alphabetic characters
        if (isalpha(c)) {
            char base = islower(c) ? 'a' : 'A';
            c = (c - base - shift + 26) % 26 + base; // Wrap around using +26
        }
        decrypted += c;
    }

    return decrypted;
}

void processReceivedData(const String& data, String& processedData, String& processedLOC) {
    String cleanedData = data;
    cleanedData.replace("{", "");
    cleanedData.replace("'", "\"");
    cleanedData.replace("#", "");
    cleanedData = "{" + cleanedData + "}";

    String decryptedData = cleanedData;

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, decryptedData);
    Serial.println(decryptedData);

    processedData = "";
    processedLOC = "";

    if (!error) {
        StaticJsonDocument<512> processedDataDoc;

        processedDataDoc["M"] = doc["M"].as<float>();
        processedDataDoc["BT"] = doc["BT"].as<float>();
        processedDataDoc["B1"] = doc["B1"].as<float>();
        processedDataDoc["C1"] = doc["C1"].as<float>();
        processedDataDoc["B2"] = doc["B2"].as<float>();
        processedDataDoc["C2"] = doc["C2"].as<float>();
        processedDataDoc["T"] = doc["T"].as<float>();
        processedDataDoc["H"] = doc["H"].as<float>();
        processedDataDoc["P"] = doc["P"].as<float>();
        processedDataDoc["X"] = doc["X"].as<float>();
        processedDataDoc["Y"] = doc["Y"].as<float>();
        processedDataDoc["Z"] = doc["Z"].as<float>();

        // Serialize `processedDataDoc` to the global `processedData`
        serializeJson(processedDataDoc, processedData);

        // Separate JSON document for location
        StaticJsonDocument<128> processedLOCDoc;
        processedLOCDoc["La"] = doc["La"].as<float>();
        processedLOCDoc["L"] = doc["L"].as<float>();

        // Serialize `processedLOCDoc` to the global `processedLOC`
        serializeJson(processedLOCDoc, processedLOC);

        // Print serialized data
        Serial.println("Processed Data:");
        Serial.println(processedData);

        Serial.println("Processed Location Data:");
        Serial.println(processedLOC);
    } else {
        Serial.print("Error during JSON deserialization: ");
        Serial.println(error.f_str());
    }
}

void processGroundData(const String& data, String& processedGroundData) {
    String cleanedData = data;
    cleanedData.replace("{", "");
    cleanedData.replace("'", "\"");
    cleanedData.replace("#", "");
    cleanedData.replace(" ", "");
    cleanedData = "{" + cleanedData + "}";

    String decryptedData = cleanedData;

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, decryptedData);
    Serial.println(decryptedData);

    if (!error) {
        StaticJsonDocument<1024> outputDoc;
        outputDoc["T"] = doc["T"];
        outputDoc["H"] = doc["H"];
        outputDoc["SM"] = doc["SM"];
        outputDoc["SP"] = doc["SP"];
        outputDoc["SL"] = doc["SL"];

        processedGroundData = "";
        serializeJson(outputDoc, processedGroundData);
    } else {
        Serial.print("Error during JSON deserialization: ");
        Serial.println(error.f_str());
    }
}

void processImageData(const String& data, String& processedImageData) {
    String cleanedData = data;
    cleanedData.replace("{", "");
    cleanedData.replace("'", "\"");
    cleanedData.replace("#", "");
    cleanedData = "{" + cleanedData + "}";

    String decryptedData = cleanedData;

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, decryptedData);
    Serial.println(decryptedData);

    if (!error) {
        StaticJsonDocument<1024> outputDoc;
        outputDoc["image"] = doc["image"];

        processedImageData = "";
        serializeJson(outputDoc, processedImageData);

        // Print serialized image data
        Serial.println("Processed Image Data:");
        Serial.println(processedImageData);
    } else {
        Serial.print("Error during JSON deserialization: ");
        Serial.println(error.f_str());
    }
}

void setup() {
    Serial.begin(115200);
    
    SPI.begin(18, 19, 23, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

    pinMode(GreenPin, OUTPUT);
    pinMode(BluePin, OUTPUT);
    pinMode(RedPin, OUTPUT);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    mySerial.begin(9600);

    if (!LoRa.begin(433E6)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
    Serial.println("LoRa initialized.");

    xTaskCreate(fetchTask, "Fetch Task", 8192, NULL, 1, &fetchTaskHandle);
    xTaskCreate(serialListenTask, "Serial Listen Task", 4096, NULL, 1, &serialListenTaskHandle);
}

void loop() {
    // Nothing to do here, tasks are running in the background
}