/*
 * Fox Energy Power Monitor Display (Version 1)
 * Displays power metrics on an ST7789 display connected to an ESP32.
 * This is an older version (v1).
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with Gemini 2.5 Pro Preview 03-25
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // For parsing JSON data

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <math.h>         // Include for trig functions (sin, cos, radians) needed by some GFX functions

// Include for temperature sensor if necessary (usually included by default core)
// #include "driver/temp_sensor.h" // Maybe needed for older cores, but temperatureRead() is standard now

// =========================================================================
// ===                         USER CONFIGURATION                        ===
// =========================================================================

// --- WiFi Settings ---
const char* ssid     = "ssid"; // *** WIFI SSID ***
const char* password = "password"; // *** WIFI PASSWORD ***

// --- Data Source ---
const char* dataUrl  = "http://192.168.0.101/0000/get_current_parameters"; // *** FOX REST API URL ***

// --- Display Pins ---
#define TFT_CS        7  // Chip Select
#define TFT_RST       3  // Reset
#define TFT_DC        2  // Data/Command

// --- Timing ---
#define LOOP_DELAY_MS 5000 // Simple delay between fetch cycles

// --- UI Appearance (Compile-Time Constants) ---
#define STATUS_BAR_HEIGHT     40
#define STATUS_BAR_V_PADDING  10  // Extra vertical padding below status bar
#define STATUS_BAR_FONT_SIZE  2
#define POWER_VALUE_FONT_SIZE 11  // Adjust if '99999W' overflows width
#define POWER_UNIT_FONT_SIZE  3
#define VA_FONT_SIZE          5

// --- Icon Dimensions & Paddings (Compile-Time) ---
#define WIFI_ICON_WIDTH       24
#define WIFI_ICON_HEIGHT      18
#define WIFI_RIGHT_PADDING    5   // Padding from right edge to WiFi icon
#define TEMP_WIFI_GAP         8   // Gap between Temp text and WiFi icon
#define TEMP_MAX_WIDTH        45  // Max estimated width for clearing Temp text background

// --- Colors ---
#define ST77XX_DARKGREY       0x4228 // Define custom dark grey if needed
#define BG_COLOR              ST77XX_BLACK
#define STATUS_BAR_BG_COLOR   ST77XX_BLACK
#define STATUS_BAR_TEXT_COLOR ST77XX_WHITE
#define STATUS_BAR_LINE_COLOR ST77XX_DARKGREY
#define POWER_VALUE_COLOR     ST77XX_YELLOW
#define POWER_UNIT_COLOR      ST77XX_YELLOW
#define VOLTAGE_COLOR         ST77XX_CYAN
#define CURRENT_COLOR         ST77XX_MAGENTA
#define WIFI_ICON_COLOR       ST77XX_WHITE
#define TEMP_COLOR            ST77XX_ORANGE

// =========================================================================
// ===                      GLOBAL VARIABLES & OBJECTS                   ===
// =========================================================================

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
HTTPClient http;

// --- Fetched Data Storage ---
float voltage = 0.0;
float current = 0.0;
float power_active = 0.0;
float internal_temp = 0.0;

// --- Previous State Storage (for Flicker Reduction & Change Detection) ---
float prev_voltage = -1.0;
float prev_current = -1.0;
float prev_power_active = -1.0;
float prev_internal_temp = -100.0;
int prev_rssi_level = -1;
String prev_wifi_ip_addr = "";
bool first_loop = true; // Flag for first loop execution after setup/reconnect

// --- Current State ---
String wifi_ip_addr = "---.---.---.---";
long current_rssi = -100;

// --- Runtime UI Positions (calculated in setup) ---
int screen_width = 0;
int wifi_icon_x = 0;
int wifi_icon_y = 0;
int temp_text_right_x = 0; // X coordinate for the RIGHT edge of the temperature text

// =========================================================================
// ===                        HELPER FUNCTIONS                           ===
// =========================================================================

// --- Calculate Text Bounds and Center Position ---
void getTextCenterPos(const String& text, int font_size, int area_x, int area_y, int area_w, int area_h, int16_t &cursor_x, int16_t &cursor_y) {
    int16_t x1, y1; uint16_t w, h;
    tft.setTextSize(font_size);
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h); // Get bounds relative to (0,0)
    cursor_x = area_x + (area_w - w) / 2;            // Center horizontally
    cursor_y = area_y + (area_h - h) / 2;            // Center vertically
}

// --- Draw WiFi Icon (Rectangle Bars Version) ---
// Uses global wifi_icon_x, wifi_icon_y
void drawWiFiIcon(int level) {
    const int bar_max_h = WIFI_ICON_HEIGHT; const int bar_w = 4; const int bar_gap = 2;
    int total_icon_w = 4 * bar_w + 3 * bar_gap;
    int start_x = wifi_icon_x + (WIFI_ICON_WIDTH - total_icon_w) / 2; // Center bars within icon area

    tft.fillRect(wifi_icon_x, wifi_icon_y, WIFI_ICON_WIDTH, WIFI_ICON_HEIGHT, STATUS_BAR_BG_COLOR); // Clear area

    int bar_h; int current_x = start_x;
    uint16_t color;

    color = (level >= 1) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR; // Dim or colored based on level
    bar_h = bar_max_h / 4; tft.fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color); current_x += (bar_w + bar_gap);
    color = (level >= 2) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR;
    bar_h = bar_max_h / 2; tft.fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color); current_x += (bar_w + bar_gap);
    color = (level >= 3) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR;
    bar_h = bar_max_h * 3 / 4; tft.fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color); current_x += (bar_w + bar_gap);
    color = (level >= 4) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR;
    bar_h = bar_max_h; tft.fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color);
}

// --- Map RSSI to Level (0-4) ---
int getRSSILevel(long rssi) {
    if (rssi >= -55) return 4; if (rssi >= -65) return 3; if (rssi >= -75) return 2;
    if (rssi >= -85) return 1; return 0;
}

// --- Update the Status Bar (Selectively - Refresh Indicator Removed) ---
// Uses global positions: temp_text_right_x, wifi_icon_x
void updateStatusBar(bool force_redraw_all) {
    int current_level = getRSSILevel(current_rssi);
    long rounded_temp = round(internal_temp);
    String temp_str = String(rounded_temp) + (char)247 + "C"; // Degree symbol + C

    bool ip_changed = (wifi_ip_addr != prev_wifi_ip_addr);
    bool rssi_level_changed = (current_level != prev_rssi_level);
    bool temp_changed = (rounded_temp != round(prev_internal_temp));

    // --- Update WiFi Icon ---
    if (rssi_level_changed || force_redraw_all) {
        drawWiFiIcon(current_level);
        prev_rssi_level = current_level;
    }

    // --- Update Temperature ---
    if (temp_changed || force_redraw_all) {
        // Clear area for temperature text (left of its right edge)
        tft.fillRect(temp_text_right_x - TEMP_MAX_WIDTH, 0, TEMP_MAX_WIDTH + 2, STATUS_BAR_HEIGHT, STATUS_BAR_BG_COLOR);

        tft.setTextSize(STATUS_BAR_FONT_SIZE);
        tft.setTextColor(TEMP_COLOR);
        int16_t x1, y1; uint16_t w, h;
        tft.getTextBounds(temp_str, 0, 0, &x1, &y1, &w, &h); // Get width
        int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1;        // Vertical center
        int text_x = temp_text_right_x - w;                  // Calculate X for right alignment
        tft.setCursor(text_x, text_y);
        tft.print(temp_str);
        prev_internal_temp = internal_temp; // Store unrounded temp
    }

    // --- Update IP Address ---
    if (ip_changed || force_redraw_all) {
         // Clear area left of temperature text
         int ip_clear_width = temp_text_right_x - TEMP_MAX_WIDTH - 5;
         if (ip_clear_width < 10) ip_clear_width = 10;
         tft.fillRect(0, 0, ip_clear_width, STATUS_BAR_HEIGHT, STATUS_BAR_BG_COLOR);

         // Draw IP Address (Left Aligned)
         tft.setTextSize(STATUS_BAR_FONT_SIZE);
         tft.setTextColor(STATUS_BAR_TEXT_COLOR);
         int16_t x1, y1; uint16_t w, h;
         tft.getTextBounds(wifi_ip_addr, 0, 0, &x1, &y1, &w, &h);
         int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1;
         tft.setCursor(5, text_y); // Left padding
         tft.print(wifi_ip_addr);
         prev_wifi_ip_addr = wifi_ip_addr;
    }
}

// --- Draw Power Value (Large Number + Smaller Unit) ---
void drawPowerValue(long value, uint16_t value_color, uint16_t unit_color,
                    int area_x, int area_y, int area_w, int area_h,
                    long prev_value, bool force_redraw) {
    if (value == prev_value && !force_redraw) { return; } // Skip if no change
    tft.fillRect(area_x, area_y, area_w, area_h, BG_COLOR); // Clear area
    String value_str = String(value); String unit_str = "W"; int unit_gap = 3;
    int16_t x1, y1; uint16_t value_w, value_h, unit_w, unit_h;
    tft.setTextSize(POWER_VALUE_FONT_SIZE); tft.getTextBounds(value_str, 0, 0, &x1, &y1, &value_w, &value_h);
    tft.setTextSize(POWER_UNIT_FONT_SIZE); tft.getTextBounds(unit_str, 0, 0, &x1, &y1, &unit_w, &unit_h);
    uint16_t total_w = value_w + unit_gap + unit_w;
    int16_t start_x = area_x + (area_w - total_w) / 2; // Center combined block
    int16_t start_y = area_y + (area_h - value_h) / 2; // Center vertically based on larger font
    // Draw value
    tft.setTextSize(POWER_VALUE_FONT_SIZE); tft.setTextColor(value_color); tft.setCursor(start_x, start_y); tft.print(value_str);
    // Draw unit, adjusting baseline
    int16_t unit_y = start_y + (value_h - unit_h) - (value_h / 10);
    tft.setTextSize(POWER_UNIT_FONT_SIZE); tft.setTextColor(unit_color); tft.setCursor(start_x + value_w + unit_gap, unit_y); tft.print(unit_str);
}

// --- Draw Voltage and Current Line (Separately Centered) ---
void drawVoltageCurrentRevised(float v, float c, int font_size, uint16_t v_color, uint16_t c_color,
                               int area_x, int area_y, int area_w, int area_h,
                               float prev_v, float prev_c, bool force_redraw)
{
    // --- Format strings (Voltage: Rounded, Current: 1 decimal, NO unit) ---
    String v_str = String(round(v), 0) + "V";
    String c_str = String(c, 1) + "A"; // Format to 1 decimal place

    // --- Check for changes ---
    bool v_changed = (round(v) != round(prev_v));
    bool c_changed = (abs(c - prev_c) > 0.05); // Compare float current with tolerance

    if (!v_changed && !c_changed && !force_redraw) { return; } // Skip if nothing changed

    // --- Define separate areas (Left for V, Right for C) ---
    int value_area_w = area_w / 2; // Split the total area width
    int v_area_x = area_x;
    int c_area_x = area_x + value_area_w;

    // --- Draw Voltage (Centered in Left Half) ---
    if (v_changed || force_redraw) {
        tft.fillRect(v_area_x, area_y, value_area_w, area_h, BG_COLOR); // Clear V area
        int16_t cursor_x, cursor_y;
        getTextCenterPos(v_str, font_size, v_area_x, area_y, value_area_w, area_h, cursor_x, cursor_y);
        tft.setTextSize(font_size); tft.setTextColor(v_color); tft.setCursor(cursor_x, cursor_y); tft.print(v_str);
    }

    // --- Draw Current (Centered in Right Half) ---
    if (c_changed || force_redraw) {
        tft.fillRect(c_area_x, area_y, value_area_w, area_h, BG_COLOR); // Clear C area
        int16_t cursor_x, cursor_y;
        getTextCenterPos(c_str, font_size, c_area_x, area_y, value_area_w, area_h, cursor_x, cursor_y);
        tft.setTextSize(font_size); tft.setTextColor(c_color); tft.setCursor(cursor_x, cursor_y); tft.print(c_str);
    }
}


// --- Update Main Display Area (Power, V/A line - Increased Padding) ---
void updateMainDisplayArea(float v, float c, float p, bool force_redraw) {
    // Use global screen_width calculated in setup
    int main_area_y = STATUS_BAR_HEIGHT + 1 + STATUS_BAR_V_PADDING; // Start below status bar + padding
    int main_area_h = tft.height() - main_area_y;

    // Adjust vertical division if needed, keeping V/A reasonable height
    int power_area_y = main_area_y;
    int power_area_h = main_area_h * 3 / 5; // ~60% for Power (adjust if needed)
    int va_area_y = power_area_y + power_area_h;
    int va_area_h = main_area_h - power_area_h; // V/A gets the rest

    // Draw Power
    drawPowerValue(round(p), POWER_VALUE_COLOR, POWER_UNIT_COLOR,
                   0, power_area_y, screen_width, power_area_h,
                   round(prev_power_active), force_redraw);

    // Draw V/A line using the revised centering function
    drawVoltageCurrentRevised(v, c, VA_FONT_SIZE, VOLTAGE_COLOR, CURRENT_COLOR,
                              0, va_area_y, screen_width, va_area_h,
                              prev_voltage, prev_current, force_redraw);
}


// --- Draw Initial UI Structure ---
void initialDrawUI() {
    tft.fillScreen(BG_COLOR);
    tft.drawFastHLine(0, STATUS_BAR_HEIGHT, screen_width, STATUS_BAR_LINE_COLOR); // Use screen_width
    bool force = true; // Force initial draw of all elements
    updateStatusBar(force);
    updateMainDisplayArea(0.0, 0.0, 0.0, force);
    // Store initial values as previous state
    prev_voltage = 0.0; prev_current = 0.0; prev_power_active = 0.0;
    prev_internal_temp = internal_temp; // Use current temp read during setup
    prev_rssi_level = getRSSILevel(current_rssi); // Use current level calculated during setup
    prev_wifi_ip_addr = wifi_ip_addr; // Store the IP drawn
}

// --- Display Fullscreen Message ---
void displayFullScreenMessage(const String& text, int textSize, uint16_t color) {
    tft.fillScreen(BG_COLOR); tft.setTextWrap(true); tft.setTextSize(textSize);
    tft.setTextColor(color); int16_t x1, y1; uint16_t w, h;
    tft.setCursor(0,0); tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (tft.width() - w) / 2; int16_t y = (tft.height() - h) / 2;
    tft.setCursor(x, y); tft.print(text); tft.setTextWrap(false);
}

// =========================================================================
// ===                           SETUP                                   ===
// =========================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\nESP32 Energy Monitor Display - V7 (Refactored)");
    // Ensure internal temperature sensor is enabled (usually is by default)
    #ifdef CONFIG_IDF_TARGET_ESP32 // Example conditional compilation if needed
      // temp_sensor_set_config(TSENS_CONFIG_DEFAULT());
      // temp_sensor_start();
    #endif

    tft.init(240, 320);
    tft.setRotation(3); // 320x240 AFTER rotation!
    screen_width = tft.width(); // Store width after rotation

    // --- Calculate Runtime UI Positions ---
    wifi_icon_y = (STATUS_BAR_HEIGHT - WIFI_ICON_HEIGHT) / 2;
    wifi_icon_x = screen_width - WIFI_ICON_WIDTH - WIFI_RIGHT_PADDING;
    temp_text_right_x = wifi_icon_x - TEMP_WIFI_GAP; // Position temp relative to wifi icon

    Serial.printf("Runtime Pos: Width=%d, WiFi(%d,%d), TempRgtX(%d)\n",
                  screen_width, wifi_icon_x, wifi_icon_y, temp_text_right_x);

    displayFullScreenMessage("Starting...", 2, ST77XX_WHITE); delay(500);

    // Initialize WiFi
    Serial.print("Connecting to WiFi: "); Serial.println(ssid);
    displayFullScreenMessage("Connecting WiFi...", 2, ST77XX_YELLOW);
    WiFi.begin(ssid, password); WiFi.setHostname("ESP32-EnergyMon");

    int connect_timeout = 20;
    while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) { delay(500); Serial.print("."); connect_timeout--; }
    Serial.println();

    // Check Connection Result
    if (WiFi.status() == WL_CONNECTED) {
        wifi_ip_addr = WiFi.localIP().toString(); current_rssi = WiFi.RSSI();
        internal_temp = temperatureRead(); // Read temperature
        Serial.println("WiFi connected!");
        Serial.printf("  IP: %s, RSSI: %d dBm, Temp: %.1f C\n", wifi_ip_addr.c_str(), current_rssi, internal_temp);
        displayFullScreenMessage("Connected!", 2, ST77XX_GREEN); delay(1500);
        initialDrawUI(); // Draw the screen layout
    } else {
        wifi_ip_addr = "No Connection";
        Serial.println("WiFi connection Failed!");
        displayFullScreenMessage("WiFi Failed!", 3, ST77XX_RED);
        while (true) { delay(1000); } // Halt
    }
    first_loop = true; // Set flag for first loop execution
}


// =========================================================================
// ===                            MAIN LOOP                              ===
// =========================================================================
void loop() {
    bool dataFetchedSuccessfully = false;
    bool force_redraw = first_loop; // Use flag to force redraw on first pass

    if (WiFi.status() == WL_CONNECTED) {
        // --- Update Status Values ---
        current_rssi = WiFi.RSSI();
        internal_temp = temperatureRead();

        // --- Fetch Data via HTTP ---
        Serial.printf("Fetching data... (RSSI: %d dBm, Temp: %.1f C)\n", current_rssi, internal_temp);
        http.begin(dataUrl);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                voltage = doc["voltage"].as<float>();
                current = doc["current"].as<float>();
                power_active = doc["power_active"].as<float>();
                Serial.printf("Data Received: V=%.1f, C=%.2f, P=%.1f\n", voltage, current, power_active);
                dataFetchedSuccessfully = true;
            } else { Serial.print("JSON Parse Error: "); Serial.println(error.c_str()); }
        } else if (httpCode > 0) { Serial.printf("HTTP Error: %d\n", httpCode); }
          else { Serial.printf("HTTP Connection Failed: %s\n", http.errorToString(httpCode).c_str()); }
        http.end();

        // --- Update Display ---
        updateStatusBar(force_redraw);
        updateMainDisplayArea(voltage, current, power_active, force_redraw);

        // Update previous state AFTER drawing comparison is done
        prev_voltage = voltage;
        prev_current = current;
        prev_power_active = power_active;
        // Others updated internally if they changed

    } else {
        // --- Handle WiFi Disconnection ---
        Serial.println("WiFi Disconnected. Reconnecting...");
        wifi_ip_addr = "Reconnecting..."; current_rssi = -100; internal_temp = -99;
        prev_rssi_level = -1; prev_wifi_ip_addr = ""; prev_internal_temp = -100;
        displayFullScreenMessage("WiFi Lost!\nReconnecting...", 2, ST77XX_RED);

        WiFi.begin(ssid, password);
        int reconnect_timeout = 10;
        while (WiFi.status() != WL_CONNECTED && reconnect_timeout > 0) { delay(500); Serial.print("*"); reconnect_timeout--; }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Reconnect failed.");
            wifi_ip_addr = "Reconnect Failed";
            displayFullScreenMessage("Reconnect Failed", 2, ST77XX_RED); delay(4000);
            initialDrawUI();
            force_redraw = true; // Ensure next loop redraws if connection comes back
        } else {
            wifi_ip_addr = WiFi.localIP().toString(); current_rssi = WiFi.RSSI(); internal_temp = temperatureRead();
            Serial.println("WiFi Reconnected!");
            Serial.printf("  IP: %s, RSSI: %d dBm, Temp: %.1f C\n", wifi_ip_addr.c_str(), current_rssi, internal_temp);
            displayFullScreenMessage("WiFi Reconnected!", 2, ST77XX_GREEN); delay(1500);
            initialDrawUI();
            force_redraw = true; // Ensure next loop redraws
        }
    }

    first_loop = false; // First loop execution is complete

    // --- Wait before next cycle ---
    Serial.printf("Waiting %d ms...\n", LOOP_DELAY_MS);
    delay(LOOP_DELAY_MS);

} // End of loop
