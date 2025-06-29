#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "GxEPD2_BW.h"

// Network credentials
const char* ssid = "Capgemini_4G";
const char* password = "MN704116";

// Pin definitions
#define PWR 7
#define BUSY 48
#define RES 47
#define DC 46
#define CS 45

// Web server on port 80
WebServer server(80);

// E-paper display initialization
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
GxEPD2_BW<GxEPD2_420_GYE042A87, GxEPD2_420_GYE042A87::HEIGHT> epd(GxEPD2_420_GYE042A87(CS, DC, RES, BUSY));

// Display settings structure
struct Row {
  String text;
  int fontSize;
  Row() : text(""), fontSize(18) {}
};

struct DisplaySettings {
  static const int MAX_ROWS = 8;
  Row rows[MAX_ROWS];
  bool border;
  bool invertColors;  // New field for color inversion
  DisplaySettings() : border(true), invertColors(false) {}
};

DisplaySettings currentSettings;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Info Display Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; margin: 20px; background-color: #f0f0f0; }
    .content { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type=text] { 
      width: calc(100% - 180px);
      padding: 8px;
      margin-bottom: 0;
      border: 1px solid #ddd;
      border-radius: 4px;
    }
    select { 
      width: 100px;
      padding: 8px;
      border: 1px solid #ddd;
      border-radius: 4px;
      margin: 0 5px;
    }
    button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }
    button:hover { background-color: #45a049; }
    
    .preview-container {
      width: 400px;
      height: 300px;
      margin: 20px auto;
      position: relative;
    }
    .preview {
      width: 400px;
      height: 300px;
      position: relative;
      border: 1px solid #000;
      background: white;
      overflow: hidden;
      transition: all 0.3s ease;
    }
    .preview.inverted {
      background: black;
      color: white;
    }
    .preview-row {
      position: absolute;
      left: 10px;
      right: 10px;
      white-space: nowrap;
      overflow: hidden;
      font-family: monospace;
    }
    .error { color: red; display: none; margin-top: 5px; }
    .row-container { margin-bottom: 10px; }
    .row-group {
      display: grid;
      grid-template-columns: auto 110px 60px;
      gap: 5px;
      margin-bottom: 5px;
      align-items: center;
    }
    .del-btn {
      background-color: #ff4444;
      padding: 8px;
      font-size: 12px;
      height: 35px;
      margin: 0;
      width: 100%;
    }
    .color-options {
      display: flex;
      gap: 10px;
      margin-bottom: 15px;
    }
    .color-btn {
      padding: 10px 20px;
      border: 2px solid #ddd;
      border-radius: 4px;
      cursor: pointer;
      transition: all 0.3s ease;
    }
    .color-btn.normal {
      background: white;
      color: black;
    }
    .color-btn.inverted {
      background: black;
      color: white;
    }
    .color-btn.selected {
      border-color: #4CAF50;
    }
  </style>
  <script>
    const rowHeights = {
      12: 32,
      18: 40,
      24: 48
    };

    function calculateRowPositions() {
      let positions = [];
      let currentY = 10;
      
      for (let i = 0; i < 8; i++) {
        const fontSize = parseInt(document.getElementById(`fontSize${i}`).value);
        const height = rowHeights[fontSize];
        positions.push({
          y: currentY,
          height: height
        });
        currentY += height;
      }
      return positions;
    }

 function updatePreview() {
  const preview = document.querySelector('.preview');
  preview.innerHTML = '';
  
  const positions = calculateRowPositions();
  
  for (let i = 0; i < 8; i++) {
    const text = document.getElementById(`row${i}`).value;
    const fontSize = document.getElementById(`fontSize${i}`).value;
    if (text) {
      const rowDiv = document.createElement('div');
      rowDiv.className = 'preview-row';
      
      // Create a span with double character width
      const textSpan = document.createElement('span');
      
      // Use a non-breaking space for visual representation
      textSpan.innerHTML = text.replace(/ /g, '&nbsp;');
      textSpan.style.letterSpacing = `${fontSize * 0.65}px`; // Adjust letter spacing
      
      rowDiv.appendChild(textSpan);
      rowDiv.style.top = `${positions[i].y}px`;
      rowDiv.style.fontSize = `${fontSize}px`;
      preview.appendChild(rowDiv);
    }
  }
}

    
    function toggleColors(inverted) {
      const preview = document.querySelector('.preview');
      preview.className = inverted ? 'preview inverted' : 'preview';
      
      // Update button styles
      document.querySelector('.color-btn.normal').className = 'color-btn normal' + (!inverted ? ' selected' : '');
      document.querySelector('.color-btn.inverted').className = 'color-btn inverted' + (inverted ? ' selected' : '');
    }
    
    function clearRow(index) {
      document.getElementById(`row${index}`).value = '';
      updatePreview();
    }
    
    function submitForm() {
      const rows = [];
      for (let i = 0; i < 8; i++) {
        rows.push({
          text: document.getElementById(`row${i}`).value,
          fontSize: parseInt(document.getElementById(`fontSize${i}`).value)
        });
      }
      
      const formData = {
        rows: rows,
        border: document.getElementById('border').checked,
        invertColors: document.querySelector('.preview').classList.contains('inverted')
      };
      
      fetch('/update', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(formData)
      }).then(response => {
        if (response.ok) {
          alert('Display updated successfully!');
        }
      });
      return false;
    }
  </script>
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
        <div class="row-container">
          %row_inputs%
        </div>
      </div>
      
      <div class="form-group">
        <label>
          <input type="checkbox" id="border" name="border" checked>
          Show Border
        </label>
      </div>
      
      <div class="preview-container">
        <div class="preview"></div>
      </div>
      <button type="submit">Update Display</button>
    </form>
  </div>
  <script>
    updatePreview();
  </script>
</body>
</html>
)rawliteral";

void epdPower(int state) {
  pinMode(PWR, OUTPUT);
  digitalWrite(PWR, state);
}

void epdInit() {
  epd.init(115200, true, 50, false);
  epd.setRotation(0);
  epd.setTextColor(currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  epd.setFullWindow();
}

void setFontSize(int size) {
  switch (size) {
    case 12:
      epd.setFont(&FreeMonoBold12pt7b);
      break;
    case 18:
      epd.setFont(&FreeMonoBold18pt7b);
      break;
    case 24:
      epd.setFont(&FreeMonoBold24pt7b);
      break;
  }
}

int getRowHeight(int fontSize) {
  switch (fontSize) {
    case 12: return 32;
    case 18: return 40;
    case 24: return 48;
    default: return 40;
  }
}

void updateDisplay() {
  epdPower(HIGH);
  epdInit();
  
  // Set background color based on inversion setting
  epd.fillScreen(currentSettings.invertColors ? GxEPD_BLACK : GxEPD_WHITE);
  
  if (currentSettings.border) {
    epd.drawRect(0, 0, 400, 300, currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  }
  
  int yPos = getRowHeight(24);
  int margin = 10;
  
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    if (currentSettings.rows[i].text.length() > 0) {
      setFontSize(currentSettings.rows[i].fontSize);
      epd.setCursor(margin, yPos);
      epd.print(currentSettings.rows[i].text);
      yPos += getRowHeight(currentSettings.rows[i].fontSize);
    }
  }
  
  epd.display();
  epd.hibernate();
  epdPower(LOW);
}

// Rest of the code remains the same until handleUpdate()



String getRowInputsHTML() {
  String html = "";
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    html += "<div class='row-group'>";
    
    // Text input
    html += "<input type='text' id='row" + String(i) + "' maxlength='30' ";
    html += "placeholder='Row " + String(i + 1) + "' ";
    html += "value='" + currentSettings.rows[i].text + "' ";
    html += "onkeyup='updatePreview()'>";
    
    // Font size selector
    html += "<select id='fontSize" + String(i) + "' onchange='updatePreview()'>";
    html += "<option value='12'" + String(currentSettings.rows[i].fontSize == 12 ? " selected" : "") + ">Small (12pt)</option>";
    html += "<option value='18'" + String(currentSettings.rows[i].fontSize == 18 ? " selected" : "") + ">Medium (18pt)</option>";
    html += "<option value='24'" + String(currentSettings.rows[i].fontSize == 24 ? " selected" : "") + ">Large (24pt)</option>";
    html += "</select>";
    
    // DEL button
    html += "<button type='button' class='del-btn' onclick='clearRow(" + String(i) + ")'>DEL</button>";
    html += "</div>";
  }
  return html;
}

String getHTML() {
  String html = String(index_html);
  html.replace("%row_inputs%", getRowInputsHTML());
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleUpdate() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (!error) {
      JsonArray rows = doc["rows"];
      int i = 0;
      for (JsonVariant row : rows) {
        currentSettings.rows[i].text = row["text"].as<String>();
        currentSettings.rows[i].fontSize = row["fontSize"].as<int>();
        i++;
      }
      
      currentSettings.border = doc["border"].as<bool>();
      currentSettings.invertColors = doc["invertColors"].as<bool>();
      
      updateDisplay();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid JSON");
    }
  }
}

void setup() {
  Serial.begin(115200);


  epdPower(HIGH);
  epdInit();
  epd.fillScreen(GxEPD_WHITE);
  epd.drawRect(0, 0, 400, 300, GxEPD_BLACK);
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
  epd.fillScreen(GxEPD_WHITE);
  epd.hibernate(); 
  epdPower(LOW);
  
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.begin();
  
  // Initial display update with default settings
  currentSettings.invertColors = false;  // Ensure default color scheme
  updateDisplay();
}

void loop() {
  server.handleClient();
  delay(10);  // Small delay to prevent watchdog issues
}