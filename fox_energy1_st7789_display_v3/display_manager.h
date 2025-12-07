/*
 * Display Manager with Tile-Based Double Buffering
 * Handles all display rendering with flicker-free updates
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "types.h"

class DisplayManager {
public:
    DisplayManager() : 
        tft(nullptr),
        status_canvas(nullptr),
        main_canvas(nullptr),
        buffer_mode(DIRECT),
        screen_width(0),
        wifi_icon_x(0),
        wifi_icon_y(0),
        temp_text_right_x(0),
        time_text_left_x(0),
        prev_rssi_level(-1) {
        
        // Initialize previous values
        prev_power.voltage = -1.0f;
        prev_power.current = -1.0f;
        prev_power.power_active = -1.0f;
        prev_power_color = BG_COLOR;
        
        prev_status.internal_temp = -100.0f;
        prev_status.rssi = -100;
        prev_status.hours = "";
        prev_status.minutes = "";
        prev_status.seconds = "";
    }
    
    ~DisplayManager() {
        if (status_canvas) delete status_canvas;
        if (main_canvas) delete main_canvas;
    }
    
    /**
     * Initialize display and allocate buffers
     */
    void begin() {
        // Initialize TFT display
        tft = new Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
        // Initialize with physical dimensions (240, 320) before rotation
        tft->init(240, 320);
        tft->setRotation(3); // Landscape: 320x240
        screen_width = tft->width();
        
        // Calculate runtime UI positions
        wifi_icon_y = (STATUS_BAR_HEIGHT - WIFI_ICON_HEIGHT) / 2;
        wifi_icon_x = screen_width - WIFI_ICON_WIDTH - WIFI_RIGHT_PADDING;
        temp_text_right_x = wifi_icon_x - TEMP_WIFI_GAP;
        time_text_left_x = 5;
        
        Serial.printf("Display initialized: %dx%d\n", screen_width, tft->height());
        
        // Attempt to allocate canvas for double buffering
        if (ENABLE_DOUBLE_BUFFER) {
            delay(20); // Allow memory to stabilize
            
            // Allocate status bar canvas (320x40 = 25.6KB)
            Serial.print("Allocating status canvas (320x40)... ");
            status_canvas = new GFXcanvas16(320, STATUS_BAR_HEIGHT);
            
            if (!status_canvas || !status_canvas->getBuffer()) {
                Serial.println("FAILED!");
                if (status_canvas) { delete status_canvas; status_canvas = nullptr; }
            } else {
                Serial.printf("OK (%d KB free)\n", ESP.getFreeHeap() / 1024);
            }
            
            delay(20); // Allow memory to stabilize
            
            // Allocate main display canvas (320x180 = 115.2KB)
            int main_height = tft->height() - STATUS_BAR_HEIGHT - 1 - STATUS_BAR_V_PADDING;
            Serial.printf("Allocating main canvas (320x%d)... ", main_height);
            main_canvas = new GFXcanvas16(320, main_height);
            
            if (!main_canvas || !main_canvas->getBuffer()) {
                Serial.println("FAILED!");
                if (main_canvas) { delete main_canvas; main_canvas = nullptr; }
            } else {
                Serial.printf("OK (%d KB free)\n", ESP.getFreeHeap() / 1024);
            }
            
            // Check if we have at least one canvas
            if (status_canvas || main_canvas) {
                buffer_mode = TILE;  // Reusing TILE mode for canvas buffering
                Serial.println("Canvas buffering enabled - flicker-free mode!");
            } else {
                buffer_mode = DIRECT;
                Serial.println("WARNING: Canvas allocation failed, using direct rendering");
            }
        } else {
            buffer_mode = DIRECT;
            Serial.println("Double buffering disabled, using direct rendering");
        }
    }
    
    /**
     * Draw fullscreen message (for startup/errors)
     */
    void drawFullScreenMessage(const String& text, int textSize, uint16_t color) {
        tft->fillScreen(BG_COLOR);
        tft->setTextWrap(true);
        tft->setTextSize(textSize);
        tft->setTextColor(color);
        
        int16_t x1, y1;
        uint16_t w, h;
        tft->setCursor(0, 0);
        tft->getTextBounds(text, tft->getCursorX(), tft->getCursorY(), &x1, &y1, &w, &h);
        
        int16_t x = (tft->width() - w) / 2;
        int16_t y = (tft->height() - h) / 2;
        tft->setCursor(x, y);
        tft->print(text);
        tft->setTextWrap(false);
    }
    
    /**
     * Convert grayscale value (0-255) to RGB565 format
     */
    uint16_t grayToRGB565(uint8_t gray) {
        uint16_t r = (gray >> 3) & 0x1F;
        uint16_t g = (gray >> 2) & 0x3F;
        uint16_t b = (gray >> 3) & 0x1F;
        return (r << 11) | (g << 5) | b;
    }
    
    /**
     * Draw a single frame of the startup animation to the display using strip buffering
     * This renders the entire screen flicker-free by building each strip in a canvas
     */
    void drawStartupFrame(GFXcanvas16* strip, int strip_h, uint16_t bgColor, 
                          uint16_t textColor, bool showText,
                          const char* title, const char* subtitle,
                          int title_x, int title_y, int sub_x, int sub_y) {
        
        int screen_h = tft->height();
        int screen_w = tft->width();
        
        // Render screen in horizontal strips
        for (int strip_y = 0; strip_y < screen_h; strip_y += strip_h) {
            int current_strip_h = min(strip_h, screen_h - strip_y);
            
            // Clear strip with background color
            strip->fillScreen(bgColor);
            
            if (showText) {
                // Check if title overlaps this strip
                int title_h = 21;  // Approximate height for text size 3
                if (title_y < strip_y + current_strip_h && title_y + title_h > strip_y) {
                    strip->setTextColor(textColor);
                    strip->setTextSize(3);
                    strip->setCursor(title_x, title_y - strip_y);
                    strip->print(title);
                }
                
                // Check if subtitle overlaps this strip
                int sub_h = 14;  // Approximate height for text size 2
                if (sub_y < strip_y + current_strip_h && sub_y + sub_h > strip_y) {
                    strip->setTextColor(textColor);
                    strip->setTextSize(2);
                    strip->setCursor(sub_x, sub_y - strip_y);
                    strip->print(subtitle);
                }
            }
            
            // Flush strip to display
            tft->drawRGBBitmap(0, strip_y, strip->getBuffer(), screen_w, current_strip_h);
        }
    }
    
    /**
     * Draw startup animation with smooth fade effects using buffered rendering
     * Sequence: black -> fade to white -> show text -> fade out text -> fade to black
     */
    void drawStartupAnimation() {
        const char* title = "ESP32-C3";
        const char* subtitle = "Energy Monitor";
        
        int16_t x1, y1;
        uint16_t tw, th, sw, sh;
        
        // Calculate text positions
        tft->setTextSize(3);
        tft->getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
        int title_x = (tft->width() - tw) / 2;
        int title_y = (tft->height() / 2) - 20;
        
        tft->setTextSize(2);
        tft->getTextBounds(subtitle, 0, 0, &x1, &y1, &sw, &sh);
        int sub_x = (tft->width() - sw) / 2;
        int sub_y = title_y + th + 15;
        
        // Allocate strip buffer (320x60 = 38.4KB - fits in ESP32-C3 RAM)
        const int STRIP_HEIGHT = 60;
        GFXcanvas16* strip = new GFXcanvas16(320, STRIP_HEIGHT);
        
        if (!strip || !strip->getBuffer()) {
            // Fallback to non-buffered if allocation fails
            Serial.println("Strip buffer allocation failed, using direct rendering");
            tft->fillScreen(ST77XX_BLACK);
            delay(500);
            tft->fillScreen(ST77XX_WHITE);
            tft->setTextColor(ST77XX_BLACK);
            tft->setTextSize(3);
            tft->setCursor(title_x, title_y);
            tft->print(title);
            tft->setTextSize(2);
            tft->setCursor(sub_x, sub_y);
            tft->print(subtitle);
            delay(1500);
            tft->fillScreen(ST77XX_BLACK);
            if (strip) delete strip;
            return;
        }
        
        Serial.println("Using buffered startup animation");
        
        // Step 1: Start with black screen
        drawStartupFrame(strip, STRIP_HEIGHT, ST77XX_BLACK, ST77XX_BLACK, false,
                         title, subtitle, title_x, title_y, sub_x, sub_y);
        delay(300);
        
        // Step 2: Fade in white background (black -> white)
        for (int i = 0; i <= 255; i += 20) {
            uint16_t color = grayToRGB565(i);
            drawStartupFrame(strip, STRIP_HEIGHT, color, color, false,
                             title, subtitle, title_x, title_y, sub_x, sub_y);
            delay(25);
        }
        
        // Step 3: Show text (instant appear on white background)
        drawStartupFrame(strip, STRIP_HEIGHT, ST77XX_WHITE, ST77XX_BLACK, true,
                         title, subtitle, title_x, title_y, sub_x, sub_y);
        delay(1500);
        
        // Step 4: Fade out everything together (white with text -> black)
        for (int i = 255; i >= 0; i -= 20) {
            uint16_t bgColor = grayToRGB565(i);
            bool showText = (i > 30);
            int textGray = max(0, i - 80);
            uint16_t textColor = grayToRGB565(textGray);
            
            drawStartupFrame(strip, STRIP_HEIGHT, bgColor, textColor, showText,
                             title, subtitle, title_x, title_y, sub_x, sub_y);
            delay(30);
        }
        
        // Final black screen
        drawStartupFrame(strip, STRIP_HEIGHT, ST77XX_BLACK, ST77XX_BLACK, false,
                         title, subtitle, title_x, title_y, sub_x, sub_y);
        delay(100);
        
        // Free the strip buffer
        delete strip;
        Serial.println("Startup animation complete");
    }
    
    /**
     * Draw initial UI structure
     */
    void drawInitialUI() {
        tft->fillScreen(BG_COLOR);
        tft->drawFastHLine(0, STATUS_BAR_HEIGHT, screen_width, STATUS_BAR_LINE_COLOR);
        
        // Force draw all elements
        StatusData init_status;
        init_status.internal_temp = temperatureRead();
        init_status.rssi = -100;
        init_status.hours = "--";
        init_status.minutes = "--";
        init_status.seconds = "--";
        init_status.time_str = "--:--:--";
        
        PowerData init_power(0.0f, 0.0f, 0.0f);
        
        drawStatusBar(init_status, true);
        drawMainDisplay(init_power, true);
        
        // Store initial values as previous
        prev_status = init_status;
        prev_power = init_power;
        prev_power_color = POWER_COLOR_NORMAL;
        prev_rssi_level = getRSSILevel(init_status.rssi);
    }
    
    /**
     * Update status bar
     */
    void drawStatusBar(const StatusData& status, bool force_redraw) {
        int current_level = getRSSILevel(status.rssi);
        long rounded_temp = round(status.internal_temp);
        
        bool time_changed = (status.hours != prev_status.hours || 
                           status.minutes != prev_status.minutes || 
                           status.seconds != prev_status.seconds);
        bool rssi_changed = (current_level != prev_rssi_level);
        bool temp_changed = (rounded_temp != round(prev_status.internal_temp));
        
        // Get drawing target(canvas if available, else TFT directly)
        Adafruit_GFX* target = getDrawTarget(status_canvas);
        bool using_canvas = (target == (Adafruit_GFX*)status_canvas);
        
        // If using canvas, we MUST redraw everything because we clear the canvas
        bool effective_force_redraw = force_redraw || using_canvas;
        
        // Skip if nothing changed AND we're not forcing a redraw
        if (!rssi_changed && !temp_changed && !time_changed && !effective_force_redraw) {
            return;
        }
        
        // If using canvas, clear it first
        if (using_canvas) {
            status_canvas->fillScreen(STATUS_BAR_BG_COLOR);
        }
        
        // Update WiFi icon
        if (rssi_changed || effective_force_redraw) {
            drawWiFiIcon(target, current_level);
            prev_rssi_level = current_level;
        }
        
        // Update temperature
        if (temp_changed || effective_force_redraw) {
            drawTemperature(target, rounded_temp);
            prev_status.internal_temp = status.internal_temp;
        }
        
        // Update time (segment by segment)
        if (time_changed || effective_force_redraw) {
            drawTime(target, status, effective_force_redraw);
            prev_status.hours = status.hours;
            prev_status.minutes = status.minutes;
            prev_status.seconds = status.seconds;
        }
        
        // Flush canvas to screen atomically - no flicker!
        if (using_canvas) {
            flushCanvas(status_canvas, 0, 0);
        }
    }
    
    /**
     * Update main display area (power, voltage, current)
     */
    void drawMainDisplay(const PowerData& power, bool force_redraw) {
        int main_area_y = STATUS_BAR_HEIGHT + 1 + STATUS_BAR_V_PADDING;
        int main_area_h = tft->height() - main_area_y;
        
        int power_area_y = 0;  // Relative to canvas
        int power_area_h = main_area_h * 3 / 5;
        int va_area_y = power_area_h;  // Relative to canvas
        int va_area_h = main_area_h - power_area_h;
        
        // Determine power color
        uint16_t current_power_color = getPowerColor(power.power_active);
        
        // Get drawing target (canvas if available, else TFT directly)
        Adafruit_GFX* target = getDrawTarget(main_canvas);
        bool using_canvas = (target == (Adafruit_GFX*)main_canvas);
        
        // If using canvas, we MUST redraw everything because we clear the canvas
        bool effective_force_redraw = force_redraw || using_canvas;
        
        // If using canvas, clear it first
        if (using_canvas) {
            main_canvas->fillScreen(BG_COLOR);
        }
        
        // Draw power value (coordinates relative to canvas/main area)
        drawPowerValue(target, power.power_active, current_power_color,
                      0, power_area_y, screen_width, power_area_h,
                      prev_power.power_active, prev_power_color, effective_force_redraw);
        
        // Draw voltage and current (coordinates relative to canvas/main area)
        drawVoltageCurrentRevised(target, power.voltage, power.current,
                                 VA_FONT_SIZE, VOLTAGE_COLOR, CURRENT_COLOR,
                                 0, va_area_y, screen_width, va_area_h,
                                 prev_power.voltage, prev_power.current, effective_force_redraw);
        
        // Flush canvas to screen atomically - no flicker!
        if (using_canvas) {
            flushCanvas(main_canvas, 0, main_area_y);
        }
        
        // Update previous values
        prev_power = power;
        prev_power_color = current_power_color;
    }

private:
    Adafruit_ST7789* tft;
    GFXcanvas16* status_canvas;
    GFXcanvas16* main_canvas;
    BufferMode buffer_mode;
    int screen_width;
    int wifi_icon_x, wifi_icon_y;
    int temp_text_right_x, time_text_left_x;
    
    // Previous state for change detection
    PowerData prev_power;
    StatusData prev_status;
    uint16_t prev_power_color;
    int prev_rssi_level;
    
    /**
     * Flush canvas to screen at specified position
     */
    void flushCanvas(GFXcanvas16* canvas, int x, int y) {
        if (canvas && canvas->getBuffer()) {
            tft->drawRGBBitmap(x, y, canvas->getBuffer(), canvas->width(), canvas->height());
        }
    }
    
    /**
     * Get drawing target (canvas or tft)
     */
    Adafruit_GFX* getDrawTarget(GFXcanvas16* canvas) {
        return (canvas && canvas->getBuffer()) ? (Adafruit_GFX*)canvas : (Adafruit_GFX*)tft;
    }
    
    /**
     * Map RSSI to signal level (0-4)
     */
    int getRSSILevel(long rssi) {
        if (rssi >= -55) return 4;
        if (rssi >= -65) return 3;
        if (rssi >= -75) return 2;
        if (rssi >= -85) return 1;
        return 0;
    }
    
    /**
     * Determine power display color based on value
     */
    uint16_t getPowerColor(float power) {
        if (power <= 1500.0f) return POWER_COLOR_NORMAL;
        if (power <= 2500.0f) return POWER_COLOR_MEDIUM;
        if (power <= 3500.0f) return POWER_COLOR_HIGH;
        return POWER_COLOR_MAX;
    }
    
    /**
     * Calculate centered text position
     */
    void getTextCenterPos(Adafruit_GFX* target, const String& text, int font_size, 
                         int area_x, int area_y, int area_w, int area_h,
                         int16_t& cursor_x, int16_t& cursor_y) {
        int16_t x1, y1;
        uint16_t w, h;
        target->setTextSize(font_size);
        target->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        cursor_x = area_x + (area_w - w) / 2;
        cursor_y = area_y + (area_h - h) / 2 + 1;
    }
    
    /**
     * Draw WiFi icon (rectangle bars version)
     */
    void drawWiFiIcon(Adafruit_GFX* target, int level) {
        const int bar_max_h = WIFI_ICON_HEIGHT;
        const int bar_w = 4;
        const int bar_gap = 2;
        int total_icon_w = 4 * bar_w + 3 * bar_gap;
        int start_x = wifi_icon_x + (WIFI_ICON_WIDTH - total_icon_w) / 2;
        
        target->fillRect(wifi_icon_x, wifi_icon_y, WIFI_ICON_WIDTH, WIFI_ICON_HEIGHT, 
                     STATUS_BAR_BG_COLOR);
        
        int current_x = start_x;
        for (int i = 0; i < 4; i++) {
            uint16_t color = (level >= i + 1) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR;
            int bar_h = bar_max_h * (i + 1) / 4;
            target->fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color);
            current_x += (bar_w + bar_gap);
        }
    }
    
    /**
     * Draw temperature display
     */
    void drawTemperature(Adafruit_GFX* target, long rounded_temp) {
        char temp_str[8];
        snprintf(temp_str, sizeof(temp_str), "%ld%cC", rounded_temp, (char)247);
        
        const int max_temp_width_pixels = 65;
        int clear_x = temp_text_right_x - max_temp_width_pixels;
        if (clear_x < 0) clear_x = 0;
        int clear_width = max_temp_width_pixels;
        if (clear_x == 0) clear_width = temp_text_right_x;
        
        target->fillRect(clear_x, 0, clear_width, STATUS_BAR_HEIGHT, STATUS_BAR_BG_COLOR);
        
        // Determine color based on temperature
        uint16_t temp_color;
        if (rounded_temp < 60) {
            temp_color = TEMP_COLOR_GREEN;
        } else if (rounded_temp <= 65) {
            temp_color = TEMP_COLOR_YELLOW;
        } else if (rounded_temp <= 70) {
            temp_color = TEMP_COLOR_ORANGE;
        } else {
            temp_color = TEMP_COLOR_RED;
        }
        
        target->setTextSize(STATUS_BAR_FONT_SIZE);
        target->setTextColor(temp_color);
        int16_t x1, y1;
        uint16_t w, h;
        target->getTextBounds(temp_str, 0, 0, &x1, &y1, &w, &h);
        int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1;
        int text_x = temp_text_right_x - w;
        target->setCursor(text_x, text_y);
        target->print(temp_str);
    }
    
    /**
     * Draw time segment
     */
    void drawTimeSegment(Adafruit_GFX* target, const String& text, const String& prev_text, 
                        int x_pos, bool force_redraw) {
        if (text != prev_text || force_redraw) {
            target->fillRect(x_pos, 0, TIME_SEGMENT_WIDTH, STATUS_BAR_HEIGHT, 
                         STATUS_BAR_BG_COLOR);
            
            target->setTextSize(TIME_FONT_SIZE);
            target->setTextColor(TIME_COLOR);
            int16_t x1, y1;
            uint16_t w, h;
            target->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
            int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1;
            int text_x = x_pos + TIME_SEGMENT_WIDTH - w;
            target->setCursor(text_x, text_y);
            target->print(text);
        }
    }
    
    /**
     * Draw time separator
     */
    void drawTimeSeparator(Adafruit_GFX* target, int x_pos, bool force_redraw) {
        if (force_redraw) {
            target->setTextSize(TIME_FONT_SIZE);
            target->setTextColor(TIME_COLOR);
            target->setCursor(x_pos, (STATUS_BAR_HEIGHT - 16) / 2 + 1);
            target->print(":");
        }
    }
    
    /**
     * Draw complete time display
     */
    void drawTime(Adafruit_GFX* target, const StatusData& status, bool force_redraw) {
        int time_start_x = time_text_left_x;
        
        drawTimeSegment(target, status.hours, prev_status.hours, time_start_x, force_redraw);
        
        int sep1_x = time_start_x + TIME_SEGMENT_WIDTH;
        drawTimeSeparator(target, sep1_x, force_redraw);
        
        int min_x = sep1_x + TIME_SEPARATOR_WIDTH;
        drawTimeSegment(target, status.minutes, prev_status.minutes, min_x, force_redraw);
        
        int sep2_x = min_x + TIME_SEGMENT_WIDTH;
        drawTimeSeparator(target, sep2_x, force_redraw);
        
        int sec_x = sep2_x + TIME_SEPARATOR_WIDTH;
        drawTimeSegment(target, status.seconds, prev_status.seconds, sec_x, force_redraw);
    }
    
    /**
     * Draw power value with W/kW conversion
     */
    void drawPowerValue(Adafruit_GFX* target, float value, uint16_t value_color,
                       int area_x, int area_y, int area_w, int area_h,
                       float prev_value, uint16_t prev_color, bool force_redraw) {
        
        bool value_changed = abs(value - prev_value) > POWER_CHANGE_THRESHOLD;
        bool color_changed = (value_color != prev_color);
        
        if (!value_changed && !color_changed && !force_redraw) {
            return;
        }
        
        // Use regular fillRect - works correctly inside transactions
        target->fillRect(area_x, area_y, area_w, area_h, BG_COLOR);
        
        String value_str;
        String unit_str;
        int unit_gap;
        
        if (value >= 1000.0f) {
            float kw_value = value / 1000.0f;
            value_str = String(kw_value, 1);
            unit_str = "kW";
            unit_gap = 2;
        } else {
            value_str = String((int)round(value), 10);
            unit_str = "W";
            unit_gap = 3;
        }
        
        int16_t x1, y1;
        uint16_t value_w, value_h, unit_w, unit_h;
        target->setTextSize(POWER_VALUE_FONT_SIZE);
        target->getTextBounds(value_str, 0, 0, &x1, &y1, &value_w, &value_h);
        target->setTextSize(POWER_UNIT_FONT_SIZE);
        target->getTextBounds(unit_str, 0, 0, &x1, &y1, &unit_w, &unit_h);
        
        uint16_t total_w = value_w + unit_gap + unit_w;
        int16_t start_x = area_x + (area_w - total_w) / 2;
        int16_t start_y = area_y + (area_h - value_h) / 2;
        
        target->setTextSize(POWER_VALUE_FONT_SIZE);
        target->setTextColor(value_color);
        target->setCursor(start_x, start_y);
        target->print(value_str);
        
        int16_t unit_y = start_y + (value_h - unit_h) - (value_h / 10);
        target->setTextSize(POWER_UNIT_FONT_SIZE);
        target->setTextColor(value_color);
        target->setCursor(start_x + value_w + unit_gap, unit_y);
        target->print(unit_str);
    }
    
    /**
     * Draw voltage and current (separately centered)
     */
    void drawVoltageCurrentRevised(Adafruit_GFX* target, float v, float c, int font_size,
                                  uint16_t v_color, uint16_t c_color,
                                  int area_x, int area_y, int area_w, int area_h,
                                  float prev_v, float prev_c, bool force_redraw) {
        
        char v_str[16], c_str[16];
        snprintf(v_str, sizeof(v_str), "%dV", (int)round(v));
        snprintf(c_str, sizeof(c_str), "%.1fA", c);
        
        bool v_changed = (round(v) != round(prev_v));
        bool c_changed = (abs(c - prev_c) > CURRENT_CHANGE_THRESHOLD);
        
        if (!v_changed && !c_changed && !force_redraw) {
            return;
        }
        
        int value_area_w = area_w / 2;
        int v_area_x = area_x;
        int c_area_x = area_x + value_area_w;
        
        if (v_changed || force_redraw) {
            // Use writeFillRect for faster rendering
            target->fillRect(v_area_x, area_y, value_area_w, area_h, BG_COLOR);
            int16_t cursor_x, cursor_y;
            getTextCenterPos(target, String(v_str), font_size, v_area_x, area_y, 
                           value_area_w, area_h, cursor_x, cursor_y);
            target->setTextSize(font_size);
            target->setTextColor(v_color);
            target->setCursor(cursor_x, cursor_y);
            target->print(v_str);
        }
        
        if (c_changed || force_redraw) {
            // Use writeFillRect for faster rendering
            target->fillRect(c_area_x, area_y, value_area_w, area_h, BG_COLOR);
            int16_t cursor_x, cursor_y;
            getTextCenterPos(target, String(c_str), font_size, c_area_x, area_y, 
                           value_area_w, area_h, cursor_x, cursor_y);
            target->setTextSize(font_size);
            target->setTextColor(c_color);
            target->setCursor(cursor_x, cursor_y);
            target->print(c_str);
        }
    }
};

#endif // DISPLAY_MANAGER_H
