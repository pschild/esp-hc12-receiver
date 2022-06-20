#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <WifiHandler.h>
#include <MqttHandler.h>
#include <OTAUpdateHandler.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#ifndef WIFI_SSID
  #error "Missing WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
  #error "Missing WIFI_PASSWORD"
#endif

#ifndef VERSION
  #error "Missing VERSION"
#endif

const String CHIP_ID = String(ESP.getChipId());
const String CLIENT_NAME = String("ESP_") + CHIP_ID;

SoftwareSerial hc12(D6, D5);

void onOtaUpdate(char* payload);
void onMqttConnected();
void onMqttMessage(char* topic, char* message);
void sendPingForSendingDevice(String chipId);
void sendMqtt(String chipId, String topic, JsonObject doc);

WifiHandler wifiHandler(WIFI_SSID, WIFI_PASSWORD);
MqttHandler mqttHandler("192.168.178.28", CLIENT_NAME);
OTAUpdateHandler updateHandler("192.168.178.28:9042", VERSION);

void setup() {
  Serial.begin(9600);
  hc12.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  wifiHandler.connect();
  mqttHandler.setup();
  mqttHandler.setOnConnectedCallback(onMqttConnected);
  mqttHandler.setOnMessageCallback(onMqttMessage);

  // start OTA update immediately
  updateHandler.startUpdate();
}

void loop() {
  mqttHandler.loop();
  updateHandler.loop();

  if (hc12.available()) {
    String message = hc12.readString();
    Serial.println(message);

    // check if message has correct format
    if (message.startsWith("<") && message.endsWith(">")) {
      message = message.substring(1, message.length() - 1);
      Serial.println(message);
      String kvPairs[20];
      int StringCount = 0;
      while (message.length() > 0) {
        int index = message.indexOf(',');
        if (index == -1) {
          kvPairs[StringCount++] = message;
          break;
        } else {
          kvPairs[StringCount++] = message.substring(0, index);
          message = message.substring(index + 1);
        }
      }

      StaticJsonDocument<400> doc;
      for (int i = 0; i < StringCount; i++) {
        int delimeterIdx = kvPairs[i].indexOf('=');
        String key = kvPairs[i].substring(0, delimeterIdx);
        String value = kvPairs[i].substring(delimeterIdx + 1);
        doc[key] = value;
      }

      String code = doc["code"].as<String>();
      String chipId = doc["chipId"].as<String>();
      String topic = doc["topic"].as<String>();
      // remove meta information from document
      doc.remove("code");
      doc.remove("chipId");
      doc.remove("topic");

      hc12.print("ACK-" + code);
      sendPingForSendingDevice(chipId);
      sendMqtt(chipId, topic, doc.as<JsonObject>());
    }
  }
}

void onOtaUpdate(char* payload) {
  updateHandler.startUpdate();
}

void onMqttConnected() {
  const String otaDeviceChannel = String("ota/") + CHIP_ID;
  mqttHandler.subscribe(otaDeviceChannel.c_str());
  const String otaAllDevicesChannel = "ota/all";
  mqttHandler.subscribe(otaAllDevicesChannel.c_str());
}

void onMqttMessage(char* topic, char* message) {
  if (String(topic).startsWith("ota/")) {
    onOtaUpdate(message);
  }
}

void sendPingForSendingDevice(String chipId) {
  const String channel = "devices/" + chipId + "/ping";
  mqttHandler.publish(channel.c_str(), NULL);
}

void sendMqtt(String chipId, String topic, JsonObject doc) {
  String output;
  serializeJson(doc, output);

  const String channel = "devices/" + chipId + "/" + topic;
  mqttHandler.publish(channel.c_str(), output.c_str());
}