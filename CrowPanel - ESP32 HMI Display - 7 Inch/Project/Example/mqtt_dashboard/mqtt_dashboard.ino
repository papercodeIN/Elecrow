#include <PCA9557.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ui.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// NTP settings
const char* timeServer = "pool.ntp.org"; // NTP server
const long utcOffsetInSeconds = 19800;  // Time zone offset for IST (India Standard Time, UTC+5:30)

// Create an instance of WiFiUDP and NTPClient
WiFiUDP udp;
NTPClient timeClient(udp, timeServer, utcOffsetInSeconds);

#define wifi_ssid "Capgemini_4G"
#define wifi_password "MN704116"

const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// MQTT topics
const char* topic_temperature = "fusion_automate/temperature";
const char* topic_pressure = "fusion_automate/pressure";
const char* topic_humidity = "fusion_automate/humidity";
const char* topic_wind = "fusion_automate/wind";

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Variables to store received values
int temperature = 0, pressure = 0, humidity = 0, wind = 0;

// These settings will affect the IP address allocation for devices connected to the Access Point  
// IPAddress local_ip(192, 168, 1, 67);  // The ESP32's IP address as an Access Point is typically 192.168.4.1  
// IPAddress gateway(192, 168, 1, 1);    // Gateway address  
// IPAddress subnet(255, 255, 255, 0);   // Subnet mask 

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

void setup()
{
  Serial.begin(9600);

  pinMode(38, OUTPUT);

  Wire.begin(19, 20);
  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  delay(200);
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

#ifdef TFT_BL
  ledcSetup(1, 300, 8);
  ledcAttachPin(TFT_BL, 1);
  ledcWrite(1, 255);
#endif
 
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW); 
  delay(500);
  digitalWrite(TFT_BL, HIGH);
#endif

  ui_init();

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  String ipAddress = WiFi.localIP().toString();

  // Set the label text with the IP address
  lv_label_set_text(ui_ipaddress, ipAddress.c_str());  

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // Start NTP client
  timeClient.begin();
  timeClient.update();
  
  // Create a timer to update the arc value every 5 seconds (5000 ms)
  // lv_timer_t *timer = lv_timer_create(update_arc_value, 5000, NULL);

  lv_timer_t *ui_timer  = lv_timer_create(update_ui, 2000, NULL);
  lv_timer_t *time_timer  = lv_timer_create(update_time, 60000, NULL);

  lv_timer_handler();
}

// Update the time every minute
void update_time(lv_timer_t *time_timer) {
    // Get the current time from NTP client
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();  // Get the epoch time
    
    // Convert epoch time to a time structure
    struct tm *timeinfo;
    timeinfo = localtime((time_t *)&epochTime);
    
    // Format the time into DD-MMM-YYYY HH:MM
    char timeStr[20];  // Make sure the buffer is large enough to hold the formatted string
    strftime(timeStr, sizeof(timeStr), "%d-%b-%Y %H:%M", timeinfo);
    
    // Update the label with the new time
    lv_label_set_text(ui_datetime, timeStr);
}



// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  strncpy(message, (char*)payload, length);
  message[length] = '\0';
  
  int value = atoi(message);  // Convert the message to an integer

  if (strcmp(topic, topic_temperature) == 0) {
    temperature = value;
  } else if (strcmp(topic, topic_pressure) == 0) {
    pressure = value;
  } else if (strcmp(topic, topic_humidity) == 0) {
    humidity = value;
  } else if (strcmp(topic, topic_wind) == 0) {
    wind = value;
  }
}

// Timer callback to update arcs and labels
void update_ui(lv_timer_t* ui_timer) {
  // Update arcs
  lv_arc_set_value(ui_Arc1, temperature);
  lv_arc_set_value(ui_Arc2, pressure);
  lv_arc_set_value(ui_Arc3, humidity);
  lv_arc_set_value(ui_Arc4, wind);

  // Update labels
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", temperature);
  lv_label_set_text(ui_Label1, buf);

  snprintf(buf, sizeof(buf), "%d", pressure);
  lv_label_set_text(ui_Label2, buf);

  snprintf(buf, sizeof(buf), "%d", humidity);
  lv_label_set_text(ui_Label3, buf);

  snprintf(buf, sizeof(buf), "%d", wind);
  lv_label_set_text(ui_Label4, buf);
}


void reconnect() {
  int attemptCount = 0;
  // Loop until reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Generate a unique Client ID based on the ESP32 chip ID
    String clientId = "FusionAutomate_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Connected to MQTT broker!");
      
      // Resubscribe to topics if needed
      mqttClient.subscribe("fusion_automate/temperature");
      mqttClient.subscribe("fusion_automate/pressure");
      mqttClient.subscribe("fusion_automate/humidity");
      mqttClient.subscribe("fusion_automate/wind");
      
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Retrying...");
      
      // Exponential backoff up to 30 seconds
      int delayTime = min(5000 * (1 << attemptCount), 30000);
      delay(delayTime);
      
      attemptCount = (attemptCount < 5) ? attemptCount + 1 : 5;
    }
  }
}


void loop()
{
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();  // Handle MQTT messages

  lv_timer_handler();
  delay(10);
}
