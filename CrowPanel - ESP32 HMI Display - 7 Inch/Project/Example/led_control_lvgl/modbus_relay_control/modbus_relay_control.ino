#if defined(ARDUINO_ARCH_ESP32)
#include <HardwareSerial.h>
extern HardwareSerial Serial2;
#endif


#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/gpio.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <PCA9557.h>
#include <ModbusMaster.h>
#include "ui.h"

// RS485/Modbus defines
#define MAX485_DE_RE  17  // Example GPIO for DE/RE pin, change as needed
#if defined(ARDUINO_ARCH_ESP32)
#define MODBUS_SERIAL Serial2
#else
#define MODBUS_SERIAL Serial1 // fallback for non-ESP32
#endif
#define MODBUS_BAUD   9600

ModbusMaster node;
// Modbus relay control functions
// coilNum: 0-3 for first 4 relays, state: true=ON, false=OFF
bool setRelayCoil(uint8_t coilNum, bool state) {
  if (coilNum > 3) return false;
  uint8_t result = node.writeSingleCoil(coilNum, state ? 0xFF00 : 0x0000);
  return (result == node.ku8MBSuccess);
}

// Read coil state (returns true=ON, false=OFF, or -1 for error)
int readRelayCoil(uint8_t coilNum) {
  if (coilNum > 3) return -1;
  uint8_t result = node.readCoils(coilNum, 1);
  if (result == node.ku8MBSuccess) {
    return node.getResponseBuffer(0) ? 1 : 0;
  }
  return -1;
}

// RS485 direction control
void preTransmission() {
  digitalWrite(MAX485_DE_RE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_DE_RE, LOW);
}

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
  cfg.pin_d13 = 47; // GPIO_NUM_47; // R2
  cfg.pin_d14 = 48; // GPIO_NUM_48; // R3
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
int led;
int brightness = 255; // Brightness level (0-255)
// unsigned long lastUpdate = 0; // Not needed when status display is disabled
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
  // RS485/Modbus setup
  pinMode(MAX485_DE_RE, OUTPUT);
  digitalWrite(MAX485_DE_RE, LOW); // Receive mode by default
  MODBUS_SERIAL.begin(MODBUS_BAUD, SERIAL_8N1, 18, 21); // RX=18, TX=21 (change as needed)
  node.begin(1, MODBUS_SERIAL); // 1 = Modbus slave address, change if needed
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
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
  
  Serial.println("System initialization complete!");
  Serial.println("Touch the ON/OFF buttons to control the LED");
}

// Function to set display brightness
void setBrightness(int level) {
  brightness = constrain(level, 0, 255);
  ledcWrite(1, brightness);
  Serial.print("Brightness set to: ");
  Serial.println(brightness);
}

// Function to update status display (commented out since ui_StatusLabel is disabled)
/*void updateStatusDisplay() {
  unsigned long uptime = millis() / 1000; // Convert to seconds
  char statusText[100];
  
  sprintf(statusText, "System Ready | LED: %s | Uptime: %lus", 
          led ? "ON" : "OFF", uptime);
  
  lv_label_set_text(ui_StatusLabel, statusText);
}*/

void loop()
{
  // Handle LED control (local)
  if(led == 1) {
    digitalWrite(38, HIGH);
  } else {
    digitalWrite(38, LOW);
  }
  // ...existing code...
  
  // Update status display every 1 second (commented out for debugging)
  /*if(millis() - lastUpdate > 1000) {
    updateStatusDisplay();
    lastUpdate = millis();
  }*/
  
  // Handle LVGL timer
  lv_timer_handler();
  delay(10);
}
