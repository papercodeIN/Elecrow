#ifndef DISPLAY_7INCH_H
#define DISPLAY_7INCH_H

// CrowPanel 7-inch RGB panel backend. Keep this definition in its own header so
// image_decoder.h can include it before declaring the global display instance.
#define LGFX_USE_V1
#include <Arduino.h>
#include "config.h"
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;
    lgfx::Light_PWM light;

    LGFX() {
        auto pcfg = panel.config();
        pcfg.memory_width = pcfg.panel_width = SCREEN_WIDTH;
        pcfg.memory_height = pcfg.panel_height = SCREEN_HEIGHT;
        pcfg.offset_x = pcfg.offset_y = 0;
        panel.config(pcfg);
        auto dcfg = panel.config_detail();
        dcfg.use_psram = 1;
        panel.config_detail(dcfg);

        auto bcfg = bus.config();
        bcfg.panel = &panel;
        bcfg.pin_d0 = GPIO_NUM_15; bcfg.pin_d1 = GPIO_NUM_7;
        bcfg.pin_d2 = GPIO_NUM_6; bcfg.pin_d3 = GPIO_NUM_5;
        bcfg.pin_d4 = GPIO_NUM_4; bcfg.pin_d5 = GPIO_NUM_9;
        bcfg.pin_d6 = GPIO_NUM_46; bcfg.pin_d7 = GPIO_NUM_3;
        bcfg.pin_d8 = GPIO_NUM_8; bcfg.pin_d9 = GPIO_NUM_16;
        bcfg.pin_d10 = GPIO_NUM_1; bcfg.pin_d11 = GPIO_NUM_14;
        bcfg.pin_d12 = GPIO_NUM_21; bcfg.pin_d13 = GPIO_NUM_47;
        bcfg.pin_d14 = GPIO_NUM_48; bcfg.pin_d15 = GPIO_NUM_45;
        bcfg.pin_henable = GPIO_NUM_41; bcfg.pin_vsync = GPIO_NUM_40;
        bcfg.pin_hsync = GPIO_NUM_39; bcfg.pin_pclk = GPIO_NUM_0;
        bcfg.freq_write = 14000000;
        bcfg.hsync_polarity = 0; bcfg.hsync_front_porch = 40;
        bcfg.hsync_pulse_width = 48; bcfg.hsync_back_porch = 40;
        bcfg.vsync_polarity = 0; bcfg.vsync_front_porch = 1;
        bcfg.vsync_pulse_width = 31; bcfg.vsync_back_porch = 13;
        bcfg.pclk_active_neg = 1; bcfg.de_idle_high = 0; bcfg.pclk_idle_high = 0;
        bus.config(bcfg);

        auto lcfg = light.config();
        lcfg.pin_bl = GPIO_NUM_2;
        light.config(lcfg);
        panel.light(&light);
        panel.setBus(&bus);
        setPanel(&panel);
    }
};

#endif
