// MASTER NODE 1
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#define NODE_ID    1
#define MAX_NODES  4

const char* WIFI_SSID = "ABCDEFGHOJKLMNOPQRSTUVWXYZ";     
const char* WIFI_PASS = "fhfhfhfh";            

struct NodeState {
  int id;
  int rssi;
  float packet_loss;
  unsigned long last_seen;
  bool active;
};

NodeState nodes[MAX_NODES + 1];
std::vector<int> rssi_history;

float interference_level = 0;
float stability_index    = 100;
float human_probability  = 0;
float sensitivity = 50;
float adaptation_speed = 50;

uint8_t broadcastAddr[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
WebServer server(80);
String cli_buffer = "";
unsigned long lastSsePush = 0;
const unsigned long SSE_INTERVAL = 500;

float secondsSince(unsigned long t) {
  return (millis() - t) / 1000.0;
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, data) != DeserializationError::Ok) return;

  int sender = doc["sender_id"] | 0;
  String type = doc["type"] | "";

  if (sender < 1 || sender > MAX_NODES) return;

  NodeState &n = nodes[sender];
  n.id = sender;
  n.active = true;
  n.last_seen = millis();

  int rssi_value = 0;
  if (info && info->rx_ctrl) {
    rssi_value = info->rx_ctrl->rssi;
  } else {
    rssi_value = doc["rssi"] | 0;
  }
  n.rssi = rssi_value;
  n.packet_loss = doc["packet_loss"] | 0.0f;

  if (type == "interference") {
    interference_level = doc["level"] | interference_level;
  }

  rssi_history.push_back(n.rssi);
  if (rssi_history.size() > 60) rssi_history.erase(rssi_history.begin());
}

void detectHuman() {
  if (rssi_history.size() < 10) return;

  float mean = 0;
  for (int r : rssi_history) mean += r;
  mean /= rssi_history.size();

  float variance = 0;
  for (int r : rssi_history) {
    float d = r - mean;
    variance += d * d;
  }
  variance /= rssi_history.size();

  human_probability = fmin(100.0, variance * 6.0 * (sensitivity / 50.0));
  stability_index = fmax(20.0, 100.0 - variance * 2.5 - interference_level * (1.0 + (100 - adaptation_speed)/100.0));
}

void sendInterferenceControl(int level) {
  DynamicJsonDocument doc(128);
  doc["sender_id"] = NODE_ID;
  doc["type"] = "control_interference";
  doc["level"] = level;
  uint8_t buffer[128];
  size_t len = serializeJson(doc, buffer);
  esp_now_send(broadcastAddr, buffer, len);
}

String buildNetworkStateJson() {
  DynamicJsonDocument doc(1024);
  doc["time"] = millis() / 1000.0;
  doc["interference_level"] = interference_level;
  doc["stability_index"] = stability_index;
  doc["human_probability"] = human_probability;
  doc["sensitivity"] = sensitivity;
  doc["adaptation_speed"] = adaptation_speed;

  JsonArray arr = doc.createNestedArray("nodes");
  for (int i = 1; i <= MAX_NODES; i++) {
    JsonObject o = arr.createNestedObject();
    o["id"] = i;
    o["role"] = (i == 1) ? "master" : (i == 4 ? "interference" : "sensor");
    o["active"] = nodes[i].active;
    o["rssi"] = nodes[i].rssi;
    o["loss"] = nodes[i].packet_loss;
    o["age"] = nodes[i].active ? secondsSince(nodes[i].last_seen) : -1.0;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void handleRoot();
void handleEvents();
void handleCli();
void handleSetInterference();
void handleSetSensitivity();
void handleSetAdaptation();

const char MAIN_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Wi‑Fi Educational System</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:monospace;}
body{background:#050814;color:#f0f0f0;padding:10px;}
h1{font-size:24px;margin-bottom:6px;color:#00b4d8;}
h2{font-size:16px;margin:8px 0 4px 0;color:#90e0ef;}
.grid{display:grid;grid-template-columns:1fr 1fr;grid-gap:10px;}
.card{background:#121826;border-radius:8px;padding:12px;border:1px solid #1f2a3a;}
table{width:100%;border-collapse:collapse;font-size:11px;}
th,td{border-bottom:1px solid #1f2a3a;padding:3px;text-align:center;}
th{background:#1b2433;}
.bar{height:10px;background:#1f2a3a;border-radius:4px;overflow:hidden;margin:4px 0;}
.bar-inner{height:100%;transition:width 0.3s;background:linear-gradient(90deg,#00b894,#00b894);}
.bar-red{background:#e74c3c;}
.bar-yellow{background:#f1c40f;}
.badge{padding:3px 8px;border-radius:4px;font-size:10px;}
.badge-ok{background:#145a32;}
.badge-warn{background:#7d6608;}
.badge-bad{background:#641e16;}
#map{width:100%;height:280px;border:1px solid #1f2a3a;border-radius:6px;background:#050814;}
#map svg{width:100%;height:100%;}
.slider-container{margin:8px 0;}
.slider{width:100%;height:6px;border-radius:3px;background:#1f2a3a;outline:none;}
.ghost{opacity:0.3;fill:#ff6b6b;}
.cli-link{color:#00b4d8;text-decoration:none;}
.module-tab{padding:4px 8px;background:#1f2a3a;border-radius:4px;cursor:pointer;margin-right:5px;}
.active-tab{background:#3498db;}
</style>
</head>
<body>
<h1>Wi‑Fi Educational System</h1>
<div style="font-size:12px;margin-bottom:10px;">Live Wi‑Fi physics: humans → RF distortion → topology → interference → adaptation</div>

<div class="grid">
  <div>
    <div class="card">
      <h2>Network Health Dashboard</h2>
      <div id="healthText" style="font-size:12px;margin-bottom:8px;">Initializing...</div>
      <div>
        <div>Stability <span id="stabilityVal"></span><span id="stabilityBadge" class="badge badge-ok"></span>
          <div class="bar"><div id="stabilityBar" class="bar-inner"></div></div>
        </div>
        <div style="margin-top:8px;">Interference <span id="interferenceVal"></span>
          <div class="bar bar-yellow"><div id="interferenceBar" class="bar-inner"></div></div>
        </div>
        <div style="margin-top:8px;">Human Presence <span id="presenceVal"></span>
          <div class="bar bar-red"><div id="presenceBar" class="bar-inner"></div></div>
        </div>
      </div>
    </div>

    <div class="card" style="margin-top:10px;">
      <h2>Controls</h2>
      <div class="slider-container">
        <label>Interference Level</label>
        <input type="range" class="slider" id="interfSlider" min="0" max="100" value="0" onchange="setInterference(this.value)">
        <span id="interfVal">0%</span>
      </div>
      <div class="slider-container">
        <label>Sensitivity</label>
        <input type="range" class="slider" id="sensSlider" min="10" max="200" value="50" onchange="setSensitivity(this.value)">
        <span id="sensVal">50%</span>
      </div>
      <div class="slider-container">
        <label>Adaptation Speed</label>
        <input type="range" class="slider" id="adaptSlider" min="10" max="200" value="50" onchange="setAdaptation(this.value)">
        <span id="adaptVal">50%</span>
      </div>
      <label><input type="checkbox" id="ghostToggle" checked> Show Wi‑Fi Ghosts</label>
    </div>

    <div class="card" style="margin-top:10px;">
      <h2>Room Map (Top View)</h2>
      <div id="map">
        <svg viewBox="0 0 400 250">
          <rect x="20" y="20" width="360" height="210" fill="none" stroke="#2c3e50" stroke-width="2"/>
          
          <!-- Ghost overlays -->
          <ellipse id="ghost2" class="ghost" cx="320" cy="130" rx="40" ry="30"/>
          <ellipse id="ghost3" class="ghost" cx="200" cy="60" rx="35" ry="25"/>
          <ellipse id="ghost4" class="ghost" cx="200" cy="190" rx="45" ry="35"/>
          
          <!-- Nodes -->
          <circle id="node1" cx="200" cy="130" r="14" fill="#3498db" stroke="#ecf0f1" stroke-width="2"/>
          <text x="200" y="112" text-anchor="middle" font-size="11" fill="#ecf0f1">1 MASTER</text>
          
          <circle id="node2" cx="320" cy="130" r="12" fill="#3498db" stroke="#ecf0f1" stroke-width="2"/>
          <text x="320" y="115" text-anchor="middle" font-size="10" fill="#ecf0f1">2 SENSOR</text>
          
          <circle id="node3" cx="200" cy="60" r="12" fill="#3498db" stroke="#ecf0f1" stroke-width="2"/>
          <text x="200" y="48" text-anchor="middle" font-size="10" fill="#ecf0f1">3 SENSOR</text>
          
          <circle id="node4" cx="200" cy="190" r="12" fill="#e74c3c" stroke="#ecf0f1" stroke-width="2"/>
          <text x="200" y="208" text-anchor="middle" font-size="10" fill="#ecf0f1">4 INTERF</text>

          <!-- Links -->
          <line id="link12" x1="200" y1="130" x2="320" y2="130" stroke="#555" stroke-width="2"/>
          <line id="link13" x1="200" y1="130" x2="200" y2="60" stroke="#555" stroke-width="2"/>
          <line id="link14" x1="200" y1="130" x2="200" y2="190" stroke="#555" stroke-width="2"/>
        </svg>
      </div>
      <div style="font-size:10px;margin-top:4px;">
        Thick green links = strong/stable. Red ghosts = human RF disturbance. Move to see physics!
      </div>
    </div>
  </div>

  <div>
    <div class="card">
      <h2>Live Node Status</h2>
      <table>
        <thead>
          <tr><th>ID</th><th>Role</th><th>RSSI</th><th>Loss</th><th>Age</th><th>●</th></tr>
        </thead>
        <tbody id="nodeTableBody">
          <tr><td colspan="6" style="font-size:10px;">Connecting nodes...</td></tr>
        </tbody>
      </table>
    </div>

    <div class="card" style="margin-top:10px;">
      <h2>CLI Terminal <a href="/cli" class="cli-link" target="_blank">[Open CLI]</a></h2>
      <div style="font-size:10px;color:#90e0ef;">Commands: status, interf 80, sens 70, help</div>
    </div>

    <div class="card" style="margin-top:10px;">
      <h2>Modules</h2>
      <div style="font-size:11px;">
        <span class="module-tab active-tab" onclick="showModule(1)">Module 1</span>
        <span class="module-tab" onclick="showModule(2)">Module 2</span>
        <span class="module-tab" onclick="showModule(3)">Module 3</span>
        <span class="module-tab" onclick="showModule(4)">Module 4</span>
      </div>
      <div id="moduleContent">
        <div id="module1" style="display:block;">
          <strong>WI‑FI SENSING:</strong> Your body distorts radio waves. RSSI jitter = human presence. No cameras!
        </div>
        <div id="module2" style="display:none;">
          <strong>TOPOLOGY MAPPING:</strong> Live network graph shows dead zones, hotspots as you move.
        </div>
        <div id="module3" style="display:none;">
          <strong>ADAPTIVE INTERFERENCE:</strong> Safe packet bursts simulate crowded Wi‑Fi. Watch adaptation!
        </div>
        <div id="module4" style="display:none;">
          <strong>WEB LAYER:</strong> Educational controls + visualization. Experience cause → effect.
        </div>
      </div>
    </div>

    <div class="card" style="margin-top:10px;">
      <h2>Physical Feedback</h2>
      <div style="font-size:10px;">
        • LED strip: Green=stable, Red=stress<br>
        • Vibration motors: High interference<br>
        • Servos: Near-collapse (optional)
      </div>
    </div>
  </div>
</div>

<script>
let evtSource = null;
let showGhosts = true;

function connectSSE() {
  evtSource = new EventSource('/events');
  evtSource.onmessage = function(e) {
    const data = JSON.parse(e.data);
    updateDashboard(data);
  };
  evtSource.onerror = function() {
    setTimeout(connectSSE, 2000);
  };
}

function setInterference(val) {
  fetch('/set_interference?level=' + val);
  document.getElementById('interfVal').textContent = val + '%';
}

function setSensitivity(val) {
  fetch('/setsens?val=' + val);
  document.getElementById('sensVal').textContent = val + '%';
}

function setAdaptation(val) {
  fetch('/setadapt?val=' + val);
  document.getElementById('adaptVal').textContent = val + '%';
}

function rssiToBars(rssi) {
  if (rssi === 0) return 0;
  if (rssi > -55) return 5;
  if (rssi > -65) return 4;
  if (rssi > -75) return 3;
  if (rssi > -85) return 2;
  return 1;
}

function rssiToColor(rssi) {
  if (rssi === 0) return "#555";
  if (rssi > -55) return "#2ecc71";
  if (rssi > -65) return "#f1c40f";
  if (rssi > -75) return "#e67e22";
  return "#e74c3c";
}

function updateDashboard(data) {
  const stab = data.stability_index || 0;
  const inf = data.interference_level || 0;
  const hp = data.human_probability || 0;

  document.getElementById('stabilityVal').textContent = stab.toFixed(0) + '%';
  document.getElementById('interferenceVal').textContent = inf.toFixed(0) + '%';
  document.getElementById('presenceVal').textContent = hp.toFixed(0) + '%';

  document.getElementById('stabilityBar').style.width = stab + '%';
  document.getElementById('interferenceBar').style.width = inf + '%';
  document.getElementById('presenceBar').style.width = hp + '%';

  const stabBadge = document.getElementById('stabilityBadge');
  let badgeClass = 'badge badge-ok', state = 'NORMAL';
  if (stab > 70) { badgeClass = 'badge badge-ok'; state = 'NORMAL'; }
  else if (stab > 40) { badgeClass = 'badge badge-warn'; state = 'ADAPTING'; }
  else { badgeClass = 'badge badge-bad'; state = 'STRESSED'; }
  stabBadge.textContent = state;
  stabBadge.className = badgeClass;

  document.getElementById('healthText').textContent = 
    `Loop: Human(${hp.toFixed(0)}%) → Sensing → Interference(${inf.toFixed(0)}%) → Stability(${stab.toFixed(0)}%)`;

  // Node table
  const tbody = document.getElementById('nodeTableBody');
  tbody.innerHTML = '';
  data.nodes.forEach(n => {
    const tr = document.createElement('tr');
    const bars = rssiToBars(n.rssi);
    let speed = '';
    for (let i = 0; i < 5; i++) speed += i < bars ? '█' : '·';
    tr.innerHTML = `
      <td>${n.id}</td><td>${n.role}</td><td>${n.active?n.rssi:'-'}</td>
      <td>${(n.loss*100).toFixed(0)}%</td><td>${n.active?n.age.toFixed(1):'-'}</td>
      <td style="color:${rssiToColor(n.rssi)};">${speed}</td>`;
    tbody.appendChild(tr);
  });

  // Map updates
  const nodeMap = {2: document.getElementById('node2'), 3: document.getElementById('node3'), 4: document.getElementById('node4')};
  const linkMap = {2: 'link12', 3: 'link13', 4: 'link14'};
  const ghostMap = {2: 'ghost2', 3: 'ghost3', 4: 'ghost4'};
  
  data.nodes.forEach(n => {
    if (n.id > 1) {
      const linkId = linkMap[n.id];
      const link = document.getElementById(linkId);
      if (link) {
        if (!n.active) {
          link.setAttribute('stroke', '#333');
          link.setAttribute('stroke-width', '1');
        } else {
          const col = rssiToColor(n.rssi);
          const bw = 1 + rssiToBars(n.rssi) * 0.6;
          link.setAttribute('stroke', col);
          link.setAttribute('stroke-width', bw);
        }
      }
      
      // Ghosts
      const ghost = document.getElementById(ghostMap[n.id]);
      if (ghost && showGhosts) {
        ghost.style.opacity = (hp * 0.005).toString();
        ghost.setAttribute('rx', (30 + hp * 0.3).toString());
        ghost.setAttribute('ry', (25 + hp * 0.25).toString());
      }
    }
  });
}

function showModule(num) {
  document.querySelectorAll('[id^="module"]').forEach((el, i) => {
    el.style.display = i+1 === num ? 'block' : 'none';
  });
  document.querySelectorAll('.module-tab').forEach((tab, i) => {
    tab.classList.toggle('active-tab', i+1 === num);
  });
}

document.getElementById('ghostToggle').addEventListener('change', function() {
  showGhosts = this.checked;
  const ghosts = document.querySelectorAll('.ghost');
  ghosts.forEach(g => g.style.display = showGhosts ? 'block' : 'none');
});

window.addEventListener('load', connectSSE);
</script>
</body></html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", MAIN_PAGE);
}

void handleEvents() {
  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
  while (client.connected()) {
    detectHuman();
    String json = buildNetworkStateJson();
    client.print("data: " + json + "\n\n");
    delay(SSE_INTERVAL);
  }
}

void handleCli() {
  String cmd = server.arg("cmd").length() > 0 ? server.arg("cmd") : "";
  if (cmd.length() > 0) {
    Serial.println("CLI> " + cmd);
    cli_buffer += "CLI> " + cmd + "\n";
    if (cmd == "status") {
      cli_buffer += "Nodes: ";
      int active = 0;
      for (int i=1; i<=MAX_NODES; i++) if (nodes[i].active) active++;
      for (int i=1; i<=MAX_NODES; i++) cli_buffer += nodes[i].active ? "Y" : "N";
      cli_buffer += " (" + String(active) + "/4)\n";
      cli_buffer += "Stability: " + String(stability_index).substring(0,4) + "%\n";
    } else if (cmd.startsWith("interf ")) {
      int lvl = cmd.substring(7).toInt();
      interference_level = constrain(lvl, 0, 100);
      sendInterferenceControl(lvl);
      cli_buffer += "Interference: " + String(lvl) + "%\n";
    } else if (cmd.startsWith("sens ")) {
      sensitivity = constrain(cmd.substring(5).toInt(), 10, 200);
      cli_buffer += "Sensitivity: " + String(sensitivity) + "%\n";
    } else if (cmd.startsWith("adapt ")) {
      adaptation_speed = constrain(cmd.substring(6).toInt(), 10, 200);
      cli_buffer += "Adaptation: " + String(adaptation_speed) + "%\n";
    } else if (cmd == "help") {
      cli_buffer += "status | interf 0-100 | sens 10-200 | adapt 10-200 | clear\n";
    } else if (cmd == "clear") {
      cli_buffer = "";
    } else {
      cli_buffer += "Unknown command\n";
    }
    if (cli_buffer.length() > 3000) cli_buffer = cli_buffer.substring(1000);
  }
  String html = "<!DOCTYPE html><html><head><title>CLI</title>"
    "<style>body{font-family:monospace;background:#000;color:#0f0;padding:20px;font-size:13px;}</style></head>"
    "<body><h2>ESP32 CLI (F5 refresh)</h2><pre style='height:400px;overflow:auto;background:#111;padding:10px;border:1px solid #0f0;'>" 
    + cli_buffer + "</pre>"
    "<form onsubmit='sendCmd();return false;'><input id='cmd' style='width:70%;padding:8px;font-family:monospace;background:#111;color:#0f0;border:1px solid #0f0;font-size:13px;' autofocus> "
    "<button style='width:25%;padding:8px;background:#0f0;color:#000;border:none;font-size:13px;'>Send</button></form>"
    "<script>function sendCmd(){let c=document.getElementById('cmd').value;fetch('/cli?cmd='+encodeURIComponent(c));"
    "document.getElementById('cmd').value='';setTimeout(()=>location.reload(),500);}</script></body></html>";
  server.send(200, "text/html", html);
}

void handleSetInterference() {
  int level = server.arg("level").toInt();
  interference_level = constrain(level, 0, 100);
  sendInterferenceControl(level);
  server.send(200, "text/plain", "OK " + String(level));
}

void handleSetSensitivity() {
  sensitivity = constrain(server.arg("val").toInt(), 10, 200);
  server.send(200, "text/plain", "OK " + String(sensitivity));
}

void handleSetAdaptation() {
  adaptation_speed = constrain(server.arg("val").toInt(), 10, 200);
  server.send(200, "text/plain", "OK " + String(adaptation_speed));
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts++ < 40) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("Channel: " + String(WiFi.channel()));
    
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  server.on("/", handleRoot);
  server.on("/events", HTTP_GET, handleEvents);
  server.on("/cli", HTTP_GET, handleCli);
  server.on("/set_interference", HTTP_GET, handleSetInterference);
  server.on("/setsens", HTTP_GET, handleSetSensitivity);
  server.on("/setadapt", HTTP_GET, handleSetAdaptation);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("=== WiFi Educational System READY ===");
  Serial.println("http://" + WiFi.localIP().toString());
  Serial.println("CLI: http://" + WiFi.localIP().toString() + "/cli");
}

void loop() {
  server.handleClient();
}