// NODE 2/3 SENSOR - Change #define NODE_ID 2 or 3
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

#define NODE_ID 2  // CHANGE TO 3 FOR NODE 3

const char* WIFI_SSID = "ABCDEFGHOJKLMNOPQRSTUVWXYZ";
const char* WIFI_PASS = "fhfhfhfh";          

uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
float packet_loss_sim = 0;

void addBroadcastPeer() {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcast, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void sendData() {
  DynamicJsonDocument doc(128);
  doc["sender_id"] = NODE_ID;
  doc["type"] = "sensor";
  doc["rssi"] = WiFi.RSSI();
  doc["packet_loss"] = packet_loss_sim + (random(0, 5) / 100.0);
  
  uint8_t buffer[128];
  size_t len = serializeJson(doc, buffer);
  esp_now_send(broadcast, buffer, len);
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Node %d connecting", NODE_ID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed");
    return;
  }
  
  addBroadcastPeer();
  Serial.printf("NODE %d SENSOR READY ch=%d\n", NODE_ID, WiFi.channel());
}

void loop() {
  sendData();
  delay(1800 + random(-200, 200));  // Natural jitter
}
