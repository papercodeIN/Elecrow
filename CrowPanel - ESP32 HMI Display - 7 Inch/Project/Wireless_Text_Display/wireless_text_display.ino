#include <PCA9557.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "ui.h"

class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;

  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      
      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_7;  // B1
      cfg.pin_d2  = GPIO_NUM_6;  // B2
      cfg.pin_d3  = GPIO_NUM_5;  // B3
      cfg.pin_d4  = GPIO_NUM_4;  // B4
      
      cfg.pin_d5  = GPIO_NUM_9;  // G0
      cfg.pin_d6  = GPIO_NUM_46; // G1
      cfg.pin_d7  = GPIO_NUM_3;  // G2
      cfg.pin_d8  = GPIO_NUM_8;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5
      
      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 15000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width  = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX lcd;
int brightness = 255; // Brightness level (0-255)

// WiFi and Web Server variables
const char* ap_ssid = "CrowPanel_Display";
const char* ap_password = "12345678";
WebServer server(80);
DNSServer dnsServer;
bool isAPMode = false;
String wifiStatus = "Connecting...";

// Text display variables for 5 lines
String displayText[5] = {"Line 1", "Line 2", "Line 3", "Line 4", "Line 5"};
String textColor[5] = {"#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF"};
int textSize[5] = {48, 48, 48, 48, 48};
String backgroundColor = "#FF0000"; // Background color - Red
unsigned long lastUpdate = 0;
//UI
#define TFT_BL 2
SPIClass& spi = SPI;
#include "touch.h"

/* Change to your screen resolution */
static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 15];
static lv_disp_drv_t disp_drv;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
 lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#endif

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  if (touch_has_signal())
  {
    if (touch_touched())
    {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
      Serial.print("Data x ");
      Serial.println(data->point.x);
      Serial.print("Data y ");
      Serial.println(data->point.y);
    }
    else if (touch_released())
    {
      data->state = LV_INDEV_STATE_REL;
    }
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
  delay(15);
}

// WiFi connection function
void setupWiFi() {
  Serial.println("Setting up WiFi...");
  
  // Try to connect to saved WiFi first (you can modify this to add your credentials)
  WiFi.mode(WIFI_STA);
  WiFi.begin("Capgemini_4G", "MN704116"); // Add your WiFi credentials here if desired
  
  // Wait for connection for 10 seconds
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiStatus = "Connected: " + WiFi.localIP().toString();
    isAPMode = false;
  } else {
    Serial.println("\nFailed to connect to WiFi. Starting AP mode...");
    startAPMode();
  }
}

// Start Access Point mode
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP Mode started. IP address: ");
  Serial.println(apIP);
  
  wifiStatus = "AP Mode: " + apIP.toString();
  isAPMode = true;
  
  // Start DNS server for captive portal
  dnsServer.start(53, "*", apIP);
}

// Web server handlers
void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>CrowPanel Text Display</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            max-width: 600px; 
            margin: 0 auto; 
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        h1 { color: #333; text-align: center; }
        .form-group { margin: 20px 0; }
        label { 
            display: block; 
            margin-bottom: 5px; 
            font-weight: bold;
            color: #555;
        }
        input[type="text"], select {
            width: 100%;
            padding: 12px;
            border: 2px solid #ddd;
            border-radius: 5px;
            font-size: 16px;
            box-sizing: border-box;
        }
        input[type="color"] {
            width: 100%;
            height: 50px;
            border: 2px solid #ddd;
            border-radius: 5px;
            cursor: pointer;
        }
        button {
            background-color: #4CAF50;
            color: white;
            padding: 15px 30px;
            font-size: 18px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            width: 100%;
            margin-top: 20px;
        }
        button:hover { background-color: #45a049; }
        .preview {
            border: 2px solid #ddd;
            padding: 20px;
            margin: 20px 0;
            border-radius: 5px;
            text-align: center;
            background-color: #f9f9f9;
            min-height: 60px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .status {
            background-color: #e7f3ff;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
            text-align: center;
            color: #0066cc;
        }
        .inline-controls {
            display: flex;
            gap: 15px;
            align-items: end;
            flex-wrap: wrap;
        }
        .text-control {
            flex: 2;
            min-width: 200px;
        }
        .color-control, .size-control {
            flex: 1;
            min-width: 120px;
        }
        .color-control input[type="color"] {
            width: 100%;
            height: 40px;
            border: 2px solid #ddd;
            border-radius: 5px;
            cursor: pointer;
        }
        .size-control select {
            width: 100%;
            height: 40px;
            padding: 8px;
            border: 2px solid #ddd;
            border-radius: 5px;
            background-color: white;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>CrowPanel 7" HMI Display - WiFi Update</h1>
        <div class="status">Status: )" + wifiStatus + R"(</div>
        
        <form id="textForm">
            <!-- Background Color Section -->
            <div class="form-group" style="background-color: #e8f4fd; padding: 15px; border-radius: 8px; margin: 20px 0; border: 2px solid #4CAF50;">
                <label for="bgcolor">Background Color:</label>
                <input type="color" id="bgcolor" name="bgcolor" value=")" + backgroundColor + R"(">
            </div>
            
            <!-- Line 1 -->
            <div style="border: 2px solid #ddd; margin: 15px 0; padding: 15px; border-radius: 8px; background-color: #f9f9f9;">
                <h3 style="margin-top: 0; color: #333;">Line 1</h3>
                <div class="inline-controls">
                    <div class="text-control">
                        <label for="text0">Text:</label>
                        <input type="text" id="text0" name="text0" value=")" + displayText[0] + R"(" maxlength="50">
                    </div>
                    <div class="color-control">
                        <label for="color0">Color:</label>
                        <input type="color" id="color0" name="color0" value=")" + textColor[0] + R"(">
                    </div>
                    <div class="size-control">
                        <label for="size0">Size:</label>
                        <select id="size0" name="size0">
                            <option value="18")" + (textSize[0] == 18 ? " selected" : "") + R"(>Tiny</option>
                            <option value="24")" + (textSize[0] == 24 ? " selected" : "") + R"(>Small</option>
                            <option value="36")" + (textSize[0] == 36 ? " selected" : "") + R"(>Medium</option>
                            <option value="48")" + (textSize[0] == 48 ? " selected" : "") + R"(>Large</option>
                        </select>
                    </div>
                </div>
            </div>
            
            <!-- Line 2 -->
            <div style="border: 2px solid #ddd; margin: 15px 0; padding: 15px; border-radius: 8px; background-color: #f9f9f9;">
                <h3 style="margin-top: 0; color: #333;"> Line 2</h3>
                <div class="inline-controls">
                    <div class="text-control">
                        <label for="text1">Text:</label>
                        <input type="text" id="text1" name="text1" value=")" + displayText[1] + R"(" maxlength="50">
                    </div>
                    <div class="color-control">
                        <label for="color1">Color:</label>
                        <input type="color" id="color1" name="color1" value=")" + textColor[1] + R"(">
                    </div>
                    <div class="size-control">
                        <label for="size1">Size:</label>
                        <select id="size1" name="size1">
                            <option value="18")" + (textSize[1] == 18 ? " selected" : "") + R"(>Tiny</option>
                            <option value="24")" + (textSize[1] == 24 ? " selected" : "") + R"(>Small</option>
                            <option value="36")" + (textSize[1] == 36 ? " selected" : "") + R"(>Medium</option>
                            <option value="48")" + (textSize[1] == 48 ? " selected" : "") + R"(>Large</option>
                        </select>
                    </div>
                </div>
            </div>
            
            <!-- Line 3 -->
            <div style="border: 2px solid #ddd; margin: 15px 0; padding: 15px; border-radius: 8px; background-color: #f9f9f9;">
                <h3 style="margin-top: 0; color: #333;">Line 3</h3>
                <div class="inline-controls">
                    <div class="text-control">
                        <label for="text2">Text:</label>
                        <input type="text" id="text2" name="text2" value=")" + displayText[2] + R"(" maxlength="50">
                    </div>
                    <div class="color-control">
                        <label for="color2">Color:</label>
                        <input type="color" id="color2" name="color2" value=")" + textColor[2] + R"(">
                    </div>
                    <div class="size-control">
                        <label for="size2">Size:</label>
                        <select id="size2" name="size2">
                            <option value="18")" + (textSize[2] == 18 ? " selected" : "") + R"(>Tiny</option>
                            <option value="24")" + (textSize[2] == 24 ? " selected" : "") + R"(>Small</option>
                            <option value="36")" + (textSize[2] == 36 ? " selected" : "") + R"(>Medium</option>
                            <option value="48")" + (textSize[2] == 48 ? " selected" : "") + R"(>Large</option>
                        </select>
                    </div>
                </div>
            </div>
            
            <!-- Line 4 -->
            <div style="border: 2px solid #ddd; margin: 15px 0; padding: 15px; border-radius: 8px; background-color: #f9f9f9;">
                <h3 style="margin-top: 0; color: #333;">Line 4</h3>
                <div class="inline-controls">
                    <div class="text-control">
                        <label for="text3">Text:</label>
                        <input type="text" id="text3" name="text3" value=")" + displayText[3] + R"(" maxlength="50">
                    </div>
                    <div class="color-control">
                        <label for="color3">Color:</label>
                        <input type="color" id="color3" name="color3" value=")" + textColor[3] + R"(">
                    </div>
                    <div class="size-control">
                        <label for="size3">Size:</label>
                        <select id="size3" name="size3">
                            <option value="18")" + (textSize[3] == 18 ? " selected" : "") + R"(>Tiny</option>
                            <option value="24")" + (textSize[3] == 24 ? " selected" : "") + R"(>Small</option>
                            <option value="36")" + (textSize[3] == 36 ? " selected" : "") + R"(>Medium</option>
                            <option value="48")" + (textSize[3] == 48 ? " selected" : "") + R"(>Large</option>
                        </select>
                    </div>
                </div>
            </div>
            
            <!-- Line 5 -->
            <div style="border: 2px solid #ddd; margin: 15px 0; padding: 15px; border-radius: 8px; background-color: #f9f9f9;">
                <h3 style="margin-top: 0; color: #333;">Line 5</h3>
                <div class="inline-controls">
                    <div class="text-control">
                        <label for="text4">Text:</label>
                        <input type="text" id="text4" name="text4" value=")" + displayText[4] + R"(" maxlength="50">
                    </div>
                    <div class="color-control">
                        <label for="color4">Color:</label>
                        <input type="color" id="color4" name="color4" value=")" + textColor[4] + R"(">
                    </div>
                    <div class="size-control">
                        <label for="size4">Size:</label>
                        <select id="size4" name="size4">
                            <option value="18")" + (textSize[4] == 18 ? " selected" : "") + R"(>Tiny</option>
                            <option value="24")" + (textSize[4] == 24 ? " selected" : "") + R"(>Small</option>
                            <option value="36")" + (textSize[4] == 36 ? " selected" : "") + R"(>Medium</option>
                            <option value="48")" + (textSize[4] == 48 ? " selected" : "") + R"(>Large</option>
                        </select>
                    </div>
                </div>
            </div>
            
            <!-- Submit Button -->
            <div style="text-align: center; margin: 30px 0;">
                <button type="submit" style="background: linear-gradient(45deg, #4CAF50, #45a049); color: white; padding: 15px 40px; font-size: 18px; border: none; border-radius: 25px; cursor: pointer; box-shadow: 0 4px 8px rgba(0,0,0,0.2); transition: all 0.3s;">
                    Update All Lines
                </button>
            </div>
            
            <!-- Old single line controls (hidden but kept for compatibility) -->
            <div style="display: none;">
                <div class="form-group">
                    <label for="text">Display Text:</label>
                    <input type="text" id="text" name="text" value=")" + displayText[0] + R"(" maxlength="100">
                </div>
            
            <div class="form-group">
                <label for="color">Text Color:</label>
                <input type="color" id="color" name="color" value=")" + textColor[0] + R"(">
            </div>
            
            <div class="form-group">
                <label for="size">Text Size:</label>
                <select id="size" name="size">
                    <option value="24">Small (24px)</option>
                    <option value="36">Medium (36px)</option>
                    <option value="48">Large (48px)</option>
                    <option value="64" selected>Extra Large (64px)</option>
                    <option value="72">Huge (72px)</option>
                </select>
            </div>
            
            <div class="form-group">
                <label>Preview:</label>
                <div class="preview" id="preview" style="color: )" + textColor[0] + R"(; font-size: )" + String(textSize[0]) + R"(px;">)" + displayText[0] + R"(</div>
            </div>
            </div>
            
            <!-- Hidden old submit button -->
            <button type="submit" style="display: none;">Publish to Display</button>
        </form>
    </div>
    
    <script>
        function updatePreview() {
            const text = document.getElementById('text').value;
            const color = document.getElementById('color').value;
            const size = document.getElementById('size').value;
            const preview = document.getElementById('preview');
            
            preview.style.color = color;
            preview.style.fontSize = size + 'px';
            preview.textContent = text || 'Preview text...';
        }
        
        document.getElementById('text').addEventListener('input', updatePreview);
        document.getElementById('color').addEventListener('change', updatePreview);
        document.getElementById('size').addEventListener('change', updatePreview);
        
        document.getElementById('textForm').addEventListener('submit', function(e) {
            e.preventDefault();
            
            const formData = new FormData();
            
            // Add background color
            if(document.getElementById('bgcolor')) {
                formData.append('bgcolor', document.getElementById('bgcolor').value);
            }
            
            // Add all 5 lines
            for(let i = 0; i < 5; i++) {
                const textEl = document.getElementById('text' + i);
                const colorEl = document.getElementById('color' + i);
                const sizeEl = document.getElementById('size' + i);
                
                if(textEl) formData.append('text' + i, textEl.value);
                if(colorEl) formData.append('color' + i, colorEl.value);
                if(sizeEl) formData.append('size' + i, sizeEl.value);
            }
            
            fetch('/update', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                alert('Display updated successfully!');
            })
            .catch(error => {
                alert('Error updating display: ' + error);
            });
        });
        
        // Set initial size selection
        document.getElementById('size').value = ')" + String(textSize[0]) + R"(';
        updatePreview();
    </script>
</body>
</html>
)";
  
  server.send(200, "text/html", html);
}

void handleUpdate() {
  // Update background color
  if (server.hasArg("bgcolor")) {
    backgroundColor = server.arg("bgcolor");
    Serial.println("Background color updated: " + backgroundColor);
  }
  
  // Update all 5 lines
  for(int i = 0; i < 5; i++) {
    String textArg = "text" + String(i);
    String colorArg = "color" + String(i);
    String sizeArg = "size" + String(i);
    
    if (server.hasArg(textArg)) {
      displayText[i] = server.arg(textArg);
    }
    if (server.hasArg(colorArg)) {
      textColor[i] = server.arg(colorArg);
    }
    if (server.hasArg(sizeArg)) {
      textSize[i] = server.arg(sizeArg).toInt();
    }
    
    Serial.println("Line " + String(i + 1) + " - Text: " + displayText[i] + 
                   ", Color: " + textColor[i] + ", Size: " + String(textSize[i]));
  }
  
  // Force UI update
  lastUpdate = 0;
  
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  // Redirect to main page for captive portal functionality
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void setup()
{
  Serial.begin(9600);
  delay(1000); // Wait for serial to initialize
  Serial.println("=== CrowPanel 7\" HMI Display ===");
  Serial.println("Initializing system...");

  // Initialize LED pin
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);

  Wire.begin(19, 20);
  Serial.println("Initializing LCD...");
  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  delay(200);
  Serial.println("LCD initialized");
  
  Serial.println("Initializing LVGL...");
  lv_init();

  touch_init();

  screenWidth = lcd.width();
  screenHeight = lcd.height();

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 15);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Initialize backlight with PWM for brightness control
#ifdef TFT_BL
  ledcSetup(1, 300, 8);
  ledcAttachPin(TFT_BL, 1);
  ledcWrite(1, brightness); // Use brightness variable
#endif

  Serial.println("Initializing UI...");
  ui_init();
  lv_timer_handler();
  Serial.println("UI initialized");
  
  // Initialize WiFi
  setupWiFi();
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.onNotFound(handleNotFound);
  
  // Start web server
  server.begin();
  Serial.println("Web server started");
  
  Serial.println("System initialization complete!");
  Serial.println("Connect to web interface to update display text");
}

// Function to set display brightness
void setBrightness(int level) {
  brightness = constrain(level, 0, 255);
  ledcWrite(1, brightness);
  Serial.print("Brightness set to: ");
  Serial.println(brightness);
}

// Function to update status display
void updateStatusDisplay() {
  lv_label_set_text(ui_StatusLabel, wifiStatus.c_str());
}

void loop()
{
  // Handle web server requests
  server.handleClient();
  
  // Handle DNS server for captive portal in AP mode
  if (isAPMode) {
    dnsServer.processNextRequest();
  }
  
  // Update display every 1 second or when text changes
  if(millis() - lastUpdate > 1000) {
    updateDisplayText();
    updateStatusDisplay();
    lastUpdate = millis();
  }
  
  // Handle LVGL timer
  lv_timer_handler();
  delay(10);
}

// Function to update the display text for 5 lines
void updateDisplayText() {
  static String lastDisplayText[5];
  static String lastTextColor[5];
  static int lastTextSize[5];
  static String lastWifiStatus = "";
  static String lastBackgroundColor = "";
  
  // Update background color if changed
  if (backgroundColor != lastBackgroundColor) {
    uint32_t bgColorValue = strtol(backgroundColor.c_str() + 1, NULL, 16); // Skip the '#'
    lv_obj_set_style_bg_color(ui_Screen1, lv_color_hex(bgColorValue), LV_PART_MAIN | LV_STATE_DEFAULT);
    lastBackgroundColor = backgroundColor;
    Serial.println("Background color updated: " + backgroundColor);
  }
  
  // Update each of the 5 text lines
  for(int i = 0; i < 5; i++) {
    if (displayText[i] != lastDisplayText[i] || textColor[i] != lastTextColor[i] || textSize[i] != lastTextSize[i]) {
      // Update text content
      lv_label_set_text(ui_TextLabel[i], displayText[i].c_str());
      
      // Convert hex color to LVGL color
      uint32_t colorValue = strtol(textColor[i].c_str() + 1, NULL, 16); // Skip the '#'
      lv_obj_set_style_text_color(ui_TextLabel[i], lv_color_hex(colorValue), LV_PART_MAIN | LV_STATE_DEFAULT);
      
      // Update font size (limited by available fonts in LVGL)
      if (textSize[i] <= 18) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (textSize[i] <= 24) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (textSize[i] <= 36) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else {
        // For sizes 48px and above, use the largest available font (48px)
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
      }
      
      lastDisplayText[i] = displayText[i];
      lastTextColor[i] = textColor[i];
      lastTextSize[i] = textSize[i];
      
      Serial.println("Line " + String(i + 1) + " updated - Text: " + displayText[i] + 
                     ", Color: " + textColor[i] + ", Size: " + String(textSize[i]));
    }
  }
  
  // Update status if changed
  if (wifiStatus != lastWifiStatus) {
    lv_label_set_text(ui_StatusLabel, wifiStatus.c_str());
    lastWifiStatus = wifiStatus;
  }
}
