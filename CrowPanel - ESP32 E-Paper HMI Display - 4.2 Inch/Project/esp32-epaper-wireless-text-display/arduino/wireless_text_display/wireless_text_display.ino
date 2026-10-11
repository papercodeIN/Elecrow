#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "GxEPD2_BW.h"
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "config.h"

WebServer server(WEB_SERVER_PORT);

GxEPD2_BW<GxEPD2_420_GYE042A87, GxEPD2_420_GYE042A87::HEIGHT> epd(GxEPD2_420_GYE042A87(PIN_CS, PIN_DC, PIN_RES, PIN_BUSY));

struct Row {
  String text;
  int fontSize;
  Row() : text(""), fontSize(FONT_SIZE_MEDIUM) {}
};

struct DisplaySettings {
  static const int MAX_ROWS = 8;
  Row rows[MAX_ROWS];
  bool border;
  bool invertColors;
  DisplaySettings() : border(true), invertColors(false) {}
};

DisplaySettings currentSettings;
unsigned long lastWifiReconnectAttempt = 0;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
<title>Info Display Control</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background-color:#f0f0f0}
.content{max-width:800px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 0 10px rgba(0,0,0,0.1)}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-weight:bold}
input[type=text]{width:calc(100% - 180px);padding:8px;margin-bottom:0;border:1px solid #ddd;border-radius:4px}
select{width:100px;padding:8px;border:1px solid #ddd;border-radius:4px;margin:0 5px}
button{background-color:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer}
button:hover{background-color:#45a049}
.preview-container{width:400px;height:300px;margin:20px auto;position:relative}
.preview{width:400px;height:300px;position:relative;border:1px solid #000;background:white;overflow:hidden;transition:all 0.3s ease}
.preview.inverted{background:black;color:white}
.preview-row{position:absolute;left:10px;right:10px;white-space:nowrap;overflow:hidden;font-family:monospace}
.row-group{display:grid;grid-template-columns:auto 110px 60px;gap:5px;margin-bottom:5px;align-items:center}
.del-btn{background-color:#ff4444;padding:8px;font-size:12px;height:35px;margin:0;width:100%;border:none;border-radius:4px;cursor:pointer;color:white}
.color-options{display:flex;gap:10px;margin-bottom:15px}
.color-btn{padding:10px 20px;border:2px solid #ddd;border-radius:4px;cursor:pointer;transition:all 0.3s ease}
.color-btn.normal{background:white;color:black}
.color-btn.inverted{background:black;color:white}
.color-btn.selected{border-color:#4CAF50}
</style>
</head>
<body>
<div class="content">
<h1>Info Display Control</h1>
<form onsubmit="return submitForm()">
<div class="form-group">
<label>Display Colors:</label>
<div class="color-options">
<div class="color-btn normal selected" onclick="toggleColors(false)">Black on White</div>
<div class="color-btn inverted" onclick="toggleColors(true)">White on Black</div>
</div>
</div>
<div class="form-group">
<label>Display Rows:</label>
<div id="rowsContainer"></div>
</div>
<div class="form-group">
<label><input type="checkbox" id="border" checked> Show Border</label>
</div>
<div class="preview-container">
<div class="preview" id="preview"></div>
</div>
<button type="submit">Update Display</button>
</form>
</div>
<script>
var rowHeights={12:32,18:40,24:48};
var settings={rows:[],border:true,invertColors:false};
function loadStatus(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){settings=d;buildRows();updatePreview()})}
function buildRows(){var c=document.getElementById('rowsContainer');c.innerHTML='';for(var i=0;i<8;i++){var rd=settings.rows[i]||{text:'',fontSize:18};var div=document.createElement('div');div.className='row-group';div.innerHTML="<input type='text' id='row"+i+"' maxlength='30' placeholder='Row "+(i+1)+"' value='"+(rd.text||'')+"' onkeyup='updatePreview()'><select id='fontSize"+i+"' onchange='updatePreview()'><option value='12'"+(rd.fontSize==12?' selected':'')+">Small (12pt)</option><option value='18'"+(rd.fontSize==18?' selected':'')+">Medium (18pt)</option><option value='24'"+(rd.fontSize==24?' selected':'')+">Large (24pt)</option></select><button type='button' class='del-btn' onclick='clearRow("+i+")'>DEL</button>";c.appendChild(div)}}
function updatePreview(){var p=document.getElementById('preview');p.innerHTML='';var y=10;for(var i=0;i<8;i++){var t=document.getElementById('row'+i).value;var fs=parseInt(document.getElementById('fontSize'+i).value);if(t){var d=document.createElement('div');d.className='preview-row';d.style.top=y+'px';d.style.fontSize=fs+'px';d.style.letterSpacing=(fs*0.65)+'px';d.textContent=t;p.appendChild(d)}y+=rowHeights[fs]}}
function toggleColors(inv){var p=document.getElementById('preview');if(inv){p.classList.add('inverted')}else{p.classList.remove('inverted')}document.querySelector('.color-btn.normal').className='color-btn normal'+(inv?'':' selected');document.querySelector('.color-btn.inverted').className='color-btn inverted'+(inv?' selected':'')}
function clearRow(i){document.getElementById('row'+i).value='';updatePreview()}
function submitForm(){var rows=[];for(var i=0;i<8;i++){rows.push({text:document.getElementById('row'+i).value,fontSize:parseInt(document.getElementById('fontSize'+i).value)})}var data={rows:rows,border:document.getElementById('border').checked,invertColors:document.getElementById('preview').classList.contains('inverted')};fetch('/api/update',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(function(r){if(r.ok)alert('Display updated!')});return false}
loadStatus();
</script>
</body>
</html>
)rawliteral";

void epdPower(int state) {
  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, state);
}

void epdInit() {
  epd.init(EPD_INIT_BAUD_RATE, EPD_INIT_RESET, EPD_INIT_DELAY, EPD_INIT_PARTIAL);
  epd.setRotation(0);
  epd.setTextColor(currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  epd.setFullWindow();
}

void setFontSize(int size) {
  switch (size) {
    case FONT_SIZE_SMALL:
      epd.setFont(&FreeMonoBold12pt7b);
      break;
    case FONT_SIZE_MEDIUM:
      epd.setFont(&FreeMonoBold18pt7b);
      break;
    case FONT_SIZE_LARGE:
      epd.setFont(&FreeMonoBold24pt7b);
      break;
  }
}

int getRowHeight(int fontSize) {
  switch (fontSize) {
    case FONT_SIZE_SMALL: return ROW_HEIGHT_SMALL;
    case FONT_SIZE_MEDIUM: return ROW_HEIGHT_MEDIUM;
    case FONT_SIZE_LARGE: return ROW_HEIGHT_LARGE;
    default: return ROW_HEIGHT_MEDIUM;
  }
}

void updateDisplay() {
  epdPower(HIGH);
  epdInit();
  epd.fillScreen(currentSettings.invertColors ? GxEPD_BLACK : GxEPD_WHITE);
  if (currentSettings.border) {
    epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  }
  int yPos = getRowHeight(FONT_SIZE_LARGE);
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    if (currentSettings.rows[i].text.length() > 0) {
      setFontSize(currentSettings.rows[i].fontSize);
      epd.setCursor(DISPLAY_MARGIN, yPos);
      epd.print(currentSettings.rows[i].text);
      yPos += getRowHeight(currentSettings.rows[i].fontSize);
    }
  }
  epd.display();
  epd.hibernate();
  epdPower(LOW);
}

bool validateSettings(const DisplaySettings& s) {
  for (int i = 0; i < s.MAX_ROWS; i++) {
    if (s.rows[i].text.length() > MAX_TEXT_LENGTH) return false;
    if (s.rows[i].fontSize != FONT_SIZE_SMALL &&
        s.rows[i].fontSize != FONT_SIZE_MEDIUM &&
        s.rows[i].fontSize != FONT_SIZE_LARGE) return false;
  }
  return true;
}

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleApiStatus() {
  StaticJsonDocument<1024> doc;
  JsonArray rows = doc.createNestedArray("rows");
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    JsonObject row = rows.createNestedObject();
    row["text"] = currentSettings.rows[i].text;
    row["fontSize"] = currentSettings.rows[i].fontSize;
  }
  doc["border"] = currentSettings.border;
  doc["invertColors"] = currentSettings.invertColors;
  doc["ip"] = WiFi.localIP().toString();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleApiUpdate() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No data received\"}");
    return;
  }
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  DisplaySettings newSettings;
  JsonArray rows = doc["rows"];
  int i = 0;
  for (JsonVariant row : rows) {
    if (i >= newSettings.MAX_ROWS) break;
    newSettings.rows[i].text = row["text"].as<String>();
    newSettings.rows[i].fontSize = row["fontSize"].as<int>();
    i++;
  }
  newSettings.border = doc["border"].as<bool>();
  newSettings.invertColors = doc["invertColors"].as<bool>();
  if (!validateSettings(newSettings)) {
    server.send(400, "application/json", "{\"error\":\"Invalid settings\"}");
    return;
  }
  currentSettings = newSettings;
  updateDisplay();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void displayStartupScreen() {
  epdPower(HIGH);
  epdInit();
  epd.fillScreen(GxEPD_WHITE);
  epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
  epd.setFont(&FreeMonoBold24pt7b);
  epd.setCursor(90, 70);
  epd.print("Wireless");
  epd.setCursor(35, 150);
  epd.print("Info-Display");
  epd.setFont(&FreeMonoBold18pt7b);
  epd.setCursor(45, 230);
  epd.print("Fusion Automate");
  epd.display();
  delay(2000);
}

void displayWiFiStatus(bool connected, String ipAddress = "") {
  epd.fillScreen(GxEPD_WHITE);
  epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
  epd.setFont(&FreeMonoBold18pt7b);
  if (connected) {
    epd.setCursor(60, 70);
    epd.print("WiFi Connected");
    epd.setFont(&FreeMonoBold12pt7b);
    epd.setCursor(80, 150);
    epd.print("IP: " + ipAddress);
  } else {
    epd.setCursor(50, 70);
    epd.print("WiFi Failed");
    epd.setFont(&FreeMonoBold12pt7b);
    epd.setCursor(30, 150);
    epd.print("Check Settings");
  }
  epd.setFont(&FreeMonoBold18pt7b);
  epd.setCursor(45, 230);
  epd.print("Fusion Automate");
  epd.display();
  delay(IP_DISPLAY_DURATION);
}

void checkWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      lastWifiReconnectAttempt = now;
      WiFi.reconnect();
    }
  }
}

void setup() {
  Serial.begin(115200);

  displayStartupScreen();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_TIMEOUT) {
    delay(WIFI_RETRY_DELAY);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    displayWiFiStatus(true, WiFi.localIP().toString());
  } else {
    displayWiFiStatus(false);
  }

  epd.fillScreen(GxEPD_WHITE);
  epd.hibernate();
  epdPower(LOW);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/update", HTTP_POST, handleApiUpdate);
  server.begin();

  currentSettings.invertColors = false;
  updateDisplay();
}

void loop() {
  server.handleClient();
  checkWiFiReconnect();
  delay(LOOP_DELAY);
}
