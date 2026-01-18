// NODE 4 INTERFERENCE - COMPLETE
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

#define NODE_ID 4
#define LED_PIN 2

const char* WIFI_SSID = "ABCDEFGHOJKLMNOPQRSTUVWXYZ";
const char* WIFI_PASS = "fhfhfhfh";          

uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
int interference_level = 30;

void addBroadcastPeer() {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcast, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void sendInterference() {
  DynamicJsonDocument doc(128);
  doc["sender_id"] = NODE_ID;
  doc["type"] = "interference";
  doc["level"] = interference_level;
  doc["rssi"] = WiFi.RSSI();
  
  uint8_t buffer[128];
  size_t len = serializeJson(doc, buffer);
  esp_now_send(broadcast, buffer, len);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Node 4 connecting");
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
  Serial.printf("NODE 4 INTERFERENCE READY ch=%d lvl=%d\n", WiFi.channel(), interference_level);
}

void loop() {
  // LED blinks faster with interference
  digitalWrite(LED_PIN, (millis() / (100 + interference_level * 3)) % 2);
  sendInterference();
  delay(400 + random(-100, 100));
}
