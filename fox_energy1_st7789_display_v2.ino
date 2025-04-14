/*
 * Fox Energy Power Monitor Display (Version 2)
 * Displays power metrics (Voltage, Current, Power) fetched from a Fox REST API
 * on an ST7789 display connected to an ESP32-C3. Includes time, Wi-Fi status.
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
#include <time.h>           // For NTP time
#include <sys/time.h>       // For time functions

// NOTE: temperatureRead() returns Celsius on ESP32, but Fahrenheit on ESP32-S2/C3/etc.
// This code assumes Celsius.

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
#define LOOP_DELAY_MS 1000 // Changed: API request interval from 5000ms to 1000ms (1 second)
#define HTTP_TIMEOUT_MS 4000 // Max time to wait for HTTP response
#define TIME_UPDATE_INTERVAL 1000  // Update time every second

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // GMT+1
const int   daylightOffset_sec = 3600; // Daylight saving time

// --- UI Appearance (Compile-Time Constants) ---
#define STATUS_BAR_HEIGHT     40
#define STATUS_BAR_V_PADDING  20  // Extra vertical padding below status bar
#define STATUS_BAR_FONT_SIZE  2
#define POWER_VALUE_FONT_SIZE 11  // Adjust if '999.9kW' overflows width (unlikely)
#define POWER_UNIT_FONT_SIZE  3
#define VA_FONT_SIZE          5
#define TIME_FONT_SIZE        2

// --- Icon Dimensions & Paddings (Compile-Time) ---
#define WIFI_ICON_WIDTH       24
#define WIFI_ICON_HEIGHT      18
#define WIFI_RIGHT_PADDING    5   // Padding from right edge to WiFi icon
#define TEMP_WIFI_GAP         8   // Gap between Temp text and WiFi icon
#define TEMP_MAX_WIDTH        45  // Max estimated width for clearing Temp text background
#define TIME_MAX_WIDTH        85  // Increased width for time display (HH:MM:SS)

// --- Colors ---
#define ST77XX_DARKGREY       0x4228 // Define custom dark grey if needed
#define BG_COLOR              ST77XX_BLACK
#define STATUS_BAR_BG_COLOR   ST77XX_BLACK
#define STATUS_BAR_TEXT_COLOR ST77XX_WHITE
#define STATUS_BAR_LINE_COLOR ST77XX_DARKGREY
// Power Colors (Thresholds applied to 'power_active')
#define POWER_COLOR_NORMAL    ST77XX_GREEN  // 0 - 1500 W
#define POWER_COLOR_MEDIUM    ST77XX_YELLOW // >1500 - 2500 W
#define POWER_COLOR_HIGH      ST77XX_ORANGE // >2500 - 3500 W
#define POWER_COLOR_MAX       ST77XX_RED    // >3500 W
// Other Colors
#define VOLTAGE_COLOR         ST77XX_CYAN
#define CURRENT_COLOR         ST77XX_MAGENTA
#define WIFI_ICON_COLOR       ST77XX_WHITE
#define TEMP_COLOR            ST77XX_YELLOW  // Changed from ST77XX_ORANGE to ST77XX_YELLOW
#define TIME_COLOR            ST77XX_WHITE

// =========================================================================
// ===                      GLOBAL VARIABLES & OBJECTS                   ===
// =========================================================================

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
HTTPClient http;

// --- Fetched Data Storage ---
float voltage = 0.0;
float current = 0.0;
float power_active = 0.0;
float internal_temp = 0.0; // Stores ESP32 internal temperature

// --- Previous State Storage (for Flicker Reduction & Change Detection) ---
float prev_voltage = -1.0;
float prev_current = -1.0;
float prev_power_active = -1.0;
uint16_t prev_power_color = BG_COLOR; // Store previous power color
float prev_internal_temp = -100.0;
int prev_rssi_level = -1;
String prev_time_str = "";
bool first_loop = true; // Flag for first loop execution after setup/reconnect

// --- Current State ---
String time_str = "--:--:--";
long current_rssi = -100;

// --- Runtime UI Positions (calculated in setup) ---
int screen_width = 0;
int wifi_icon_x = 0;
int wifi_icon_y = 0;
int temp_text_right_x = 0; // X coordinate for the RIGHT edge of the temperature text
int time_text_left_x = 0; // Left padding for time display

// --- Time tracking ---
unsigned long last_time_update = 0;

#define TIME_SEGMENT_WIDTH  28  // Fixed width for each time segment (HH, MM, SS)
#define TIME_SEPARATOR_WIDTH 8   // Width for ":" separator
#define TIME_TOTAL_WIDTH     (3*TIME_SEGMENT_WIDTH + 2*TIME_SEPARATOR_WIDTH)  // Total width for "HH:MM:SS"

// Separate time string components
String hours_str = "--";
String minutes_str = "--";
String seconds_str = "--";
String prev_hours_str = "";
String prev_minutes_str = "";
String prev_seconds_str = "";

// =========================================================================
// ===                        HELPER FUNCTIONS                           ===
// =========================================================================

// --- Update Time from NTP ---
void updateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        hours_str = "--";
        minutes_str = "--";
        seconds_str = "--";
        time_str = "--:--:--"; // Keep full string for other uses
        return;
    }
    
    // Format each component separately with leading zeros
    char h_buffer[3]; // HH + null
    char m_buffer[3]; // MM + null
    char s_buffer[3]; // SS + null
    char full_buffer[9]; // HH:MM:SS + null
    
    strftime(h_buffer, sizeof(h_buffer), "%H", &timeinfo);
    strftime(m_buffer, sizeof(m_buffer), "%M", &timeinfo);
    strftime(s_buffer, sizeof(s_buffer), "%S", &timeinfo);
    strftime(full_buffer, sizeof(full_buffer), "%H:%M:%S", &timeinfo);
    
    hours_str = String(h_buffer);
    minutes_str = String(m_buffer);
    seconds_str = String(s_buffer);
    time_str = String(full_buffer); // Keep full string for other uses
}

// --- Calculate Text Bounds and Center Position ---
// This function was accidentally removed but is needed by drawVoltageCurrentRevised
void getTextCenterPos(const String& text, int font_size, int area_x, int area_y, int area_w, int area_h, int16_t &cursor_x, int16_t &cursor_y) {
    int16_t x1, y1; uint16_t w, h;
    tft.setTextSize(font_size);
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h); // Get bounds relative to (0,0)
    cursor_x = area_x + (area_w - w) / 2;            // Center horizontally
    cursor_y = area_y + (area_h - h) / 2 + 1;        // Center vertically (adjust baseline slightly)
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

    for (int i = 0; i < 4; i++) {
        color = (level >= i + 1) ? WIFI_ICON_COLOR : STATUS_BAR_LINE_COLOR;
        bar_h = bar_max_h * (i + 1) / 4;
        tft.fillRect(current_x, wifi_icon_y + bar_max_h - bar_h, bar_w, bar_h, color);
        current_x += (bar_w + bar_gap);
    }
}

// --- Map RSSI to Level (0-4) ---
int getRSSILevel(long rssi) {
    if (rssi >= -55) return 4; if (rssi >= -65) return 3; if (rssi >= -75) return 2;
    if (rssi >= -85) return 1; return 0;
}

// --- Draw Time Segment (Helper function) ---
void drawTimeSegment(const String& text, const String& prev_text, int x_pos, bool force_redraw) {
    if (text != prev_text || force_redraw) {
        // Clear exactly this segment's area
        tft.fillRect(x_pos, 0, TIME_SEGMENT_WIDTH, STATUS_BAR_HEIGHT, STATUS_BAR_BG_COLOR);
        
        // Draw the new text (right-aligned within its segment)
        tft.setTextSize(TIME_FONT_SIZE);
        tft.setTextColor(TIME_COLOR);
        int16_t x1, y1; 
        uint16_t w, h;
        tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        // Center vertically and right-align within segment
        int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1;
        int text_x = x_pos + TIME_SEGMENT_WIDTH - w;
        tft.setCursor(text_x, text_y);
        tft.print(text);
    }
}

// --- Draw Time Separator (Helper function) ---
void drawTimeSeparator(int x_pos, bool force_redraw) {
    if (force_redraw) {
        // Only draw separators if forced (they don't change)
        tft.setTextSize(TIME_FONT_SIZE);
        tft.setTextColor(TIME_COLOR);
        tft.setCursor(x_pos, (STATUS_BAR_HEIGHT - 16) / 2 + 1); // Approximate height
        tft.print(":");
    }
}

// --- Update the Status Bar (Selectively) ---
// Uses global positions: temp_text_right_x, wifi_icon_x
void updateStatusBar(bool force_redraw_all) {
    int current_level = getRSSILevel(current_rssi);
    long rounded_temp = round(internal_temp);
    String temp_str = String(rounded_temp) + (char)247 + "C"; // Degree symbol + C

    bool time_changed = (hours_str != prev_hours_str || minutes_str != prev_minutes_str || seconds_str != prev_seconds_str);
    bool rssi_level_changed = (current_level != prev_rssi_level);
    // Compare rounded temperatures for change detection
    bool temp_changed = (rounded_temp != round(prev_internal_temp));

    // --- Update WiFi Icon ---
    if (rssi_level_changed || force_redraw_all) {
        drawWiFiIcon(current_level);
        prev_rssi_level = current_level;
    }

    // --- Update Temperature ---
    if (temp_changed || force_redraw_all) {
        // Define the maximum possible width for the temperature string (e.g., "-XX°C")
        // Max 5 chars * (approx 6px/char * font_size 2) + buffer = ~65px
        const int max_temp_width_pixels = 65;

        // Calculate the starting X coordinate for the clearing rectangle,
        // ensuring it doesn't go off the left edge of the screen.
        int clear_x = temp_text_right_x - max_temp_width_pixels;
        if (clear_x < 0) clear_x = 0;
        int clear_width = max_temp_width_pixels;
        // Adjust width if clearing starts at 0 to avoid clearing beyond temp_text_right_x
        if (clear_x == 0) clear_width = temp_text_right_x;

        // Clear the calculated background area
        tft.fillRect(clear_x, 0, clear_width, STATUS_BAR_HEIGHT, STATUS_BAR_BG_COLOR);

        // Draw the new temperature text, right-aligned
        tft.setTextSize(STATUS_BAR_FONT_SIZE);
        tft.setTextColor(TEMP_COLOR);
        int16_t x1, y1; uint16_t w, h;
        // Get the actual width of the *current* temperature string
        tft.getTextBounds(temp_str, 0, 0, &x1, &y1, &w, &h);
        int text_y = (STATUS_BAR_HEIGHT - h) / 2 + 1; // Center vertically
        // Calculate the X position for right alignment based on the actual width
        int text_x = temp_text_right_x - w;
        tft.setCursor(text_x, text_y);
        tft.print(temp_str);

        prev_internal_temp = internal_temp; // Store unrounded temp for next comparison
    }

    // --- Update Time (Segment-by-Segment) ---
    if (time_changed || force_redraw_all) {
        // Calculate base position for time display
        int time_start_x = time_text_left_x;
        
        // Draw hours segment
        drawTimeSegment(hours_str, prev_hours_str, time_start_x, force_redraw_all);
        
        // Draw first separator
        int sep1_x = time_start_x + TIME_SEGMENT_WIDTH;
        drawTimeSeparator(sep1_x, force_redraw_all);
        
        // Draw minutes segment
        int min_x = sep1_x + TIME_SEPARATOR_WIDTH;
        drawTimeSegment(minutes_str, prev_minutes_str, min_x, force_redraw_all);
        
        // Draw second separator
        int sep2_x = min_x + TIME_SEGMENT_WIDTH;
        drawTimeSeparator(sep2_x, force_redraw_all);
        
        // Draw seconds segment
        int sec_x = sep2_x + TIME_SEPARATOR_WIDTH;
        drawTimeSegment(seconds_str, prev_seconds_str, sec_x, force_redraw_all);
        
        // Update previous values for next comparison
        prev_hours_str = hours_str;
        prev_minutes_str = minutes_str;
        prev_seconds_str = seconds_str;
    }
}

// --- Draw Power Value (Handles W/kW Conversion and Coloring) ---
void drawPowerValue(float value, uint16_t value_color,
                    int area_x, int area_y, int area_w, int area_h,
                    float prev_value, uint16_t prev_color, bool force_redraw) {

    // Use a tolerance for float comparison to avoid redraws due to tiny fluctuations
    bool value_changed = abs(value - prev_value) > 0.5; // Redraw if change > 0.5W
    bool color_changed = (value_color != prev_color);

    if (!value_changed && !color_changed && !force_redraw) {
        return; // Skip if value and color are unchanged
    }

    tft.fillRect(area_x, area_y, area_w, area_h, BG_COLOR); // Clear area

    String value_str;
    String unit_str;
    int unit_gap;

    // --- W / kW Conversion ---
    if (value >= 1000.0) {
        float kw_value = value / 1000.0;
        value_str = String(kw_value, 1); // Format to 1 decimal place
        unit_str = "kW";
        unit_gap = 2; // Smaller gap looks better with 'kW'
    } else {
        value_str = String(round(value), 0); // Display as integer Watts
        unit_str = "W";
        unit_gap = 3;
    }

    // --- Calculate Text Positions (after formatting) ---
    int16_t x1, y1; uint16_t value_w, value_h, unit_w, unit_h;
    tft.setTextSize(POWER_VALUE_FONT_SIZE);
    tft.getTextBounds(value_str, 0, 0, &x1, &y1, &value_w, &value_h);
    tft.setTextSize(POWER_UNIT_FONT_SIZE);
    tft.getTextBounds(unit_str, 0, 0, &x1, &y1, &unit_w, &unit_h);

    uint16_t total_w = value_w + unit_gap + unit_w;
    int16_t start_x = area_x + (area_w - total_w) / 2; // Center combined block horizontally
    int16_t start_y = area_y + (area_h - value_h) / 2; // Center vertically based on larger font height

    // --- Draw Value ---
    tft.setTextSize(POWER_VALUE_FONT_SIZE);
    tft.setTextColor(value_color);
    tft.setCursor(start_x, start_y);
    tft.print(value_str);

    // --- Draw Unit (aligned baseline) ---
    // Adjust unit baseline to align better with the bottom of the large value font
    int16_t unit_y = start_y + (value_h - unit_h) - (value_h / 10);
    tft.setTextSize(POWER_UNIT_FONT_SIZE);
    tft.setTextColor(value_color); // Use same color for unit
    tft.setCursor(start_x + value_w + unit_gap, unit_y);
    tft.print(unit_str);
}

// --- Draw Voltage and Current Line (Separately Centered) ---
void drawVoltageCurrentRevised(float v, float c, int font_size, uint16_t v_color, uint16_t c_color,
                               int area_x, int area_y, int area_w, int area_h,
                               float prev_v, float prev_c, bool force_redraw)
{
    // --- Format strings ---
    String v_str = String(round(v), 0) + "V"; // Voltage: Rounded integer + V
    String c_str = String(c, 1) + "A";     // Current: 1 decimal place + A

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


// --- Update Main Display Area (Power, V/A line) ---
void updateMainDisplayArea(float v, float c, float p, bool force_redraw) {
    // Use global screen_width calculated in setup
    int main_area_y = STATUS_BAR_HEIGHT + 1 + STATUS_BAR_V_PADDING; // Start below status bar + padding
    int main_area_h = tft.height() - main_area_y;

    // Divide area for Power and V/A Line
    int power_area_y = main_area_y;
    int power_area_h = main_area_h * 3 / 5; // ~60% for Power
    int va_area_y = power_area_y + power_area_h;
    int va_area_h = main_area_h - power_area_h; // V/A gets the rest

    // --- Determine Power Color based on Thresholds ---
    uint16_t current_power_color;
    if (p <= 1500.0) {
        current_power_color = POWER_COLOR_NORMAL;
    } else if (p <= 2500.0) {
        current_power_color = POWER_COLOR_MEDIUM;
    } else if (p <= 3500.0) {
        current_power_color = POWER_COLOR_HIGH;
    } else { // p > 3500.0
        current_power_color = POWER_COLOR_MAX;
    }

    // --- Draw Power (with W/kW conversion and color) ---
    drawPowerValue(p, current_power_color,
                   0, power_area_y, screen_width, power_area_h,
                   prev_power_active, prev_power_color, force_redraw);

    // --- Draw V/A line ---
    drawVoltageCurrentRevised(v, c, VA_FONT_SIZE, VOLTAGE_COLOR, CURRENT_COLOR,
                              0, va_area_y, screen_width, va_area_h,
                              prev_voltage, prev_current, force_redraw);

    // Store current power color as previous for next comparison
    prev_power_color = current_power_color;
}


// --- Draw Initial UI Structure ---
void initialDrawUI() {
    // Fill screen with the defined background color
    tft.fillScreen(BG_COLOR);

    // --- Draw UI Elements Over the Background --- 
    tft.drawFastHLine(0, STATUS_BAR_HEIGHT, screen_width, STATUS_BAR_LINE_COLOR); // Use screen_width
    bool force = true; // Force initial draw of all elements
    // Reset time string to match new format if needed for initial draw
    hours_str = "--";
    minutes_str = "--";
    seconds_str = "--";
    time_str = "--:--:--";
    updateStatusBar(force);
    // Force draw initial main area with 0 values and default color
    voltage = 0.0; current = 0.0; power_active = 0.0; // Ensure known state for initial draw
    updateMainDisplayArea(voltage, current, power_active, force);
    // Store initial values as previous state
    prev_voltage = voltage;
    prev_current = current;
    prev_power_active = power_active;
    prev_power_color = POWER_COLOR_NORMAL; // Set initial previous color
    prev_internal_temp = internal_temp; // Use current temp read during setup
    prev_rssi_level = getRSSILevel(current_rssi); // Use current level calculated during setup
    prev_hours_str = hours_str;
    prev_minutes_str = minutes_str;
    prev_seconds_str = seconds_str;
}

// --- Display Fullscreen Message ---
void displayFullScreenMessage(const String& text, int textSize, uint16_t color) {
    tft.fillScreen(BG_COLOR); tft.setTextWrap(true); tft.setTextSize(textSize);
    tft.setTextColor(color); int16_t x1, y1; uint16_t w, h;
    tft.setCursor(0,0); // Set cursor before getTextBounds for wrapping calc
    tft.getTextBounds(text, tft.getCursorX(), tft.getCursorY(), &x1, &y1, &w, &h);
    // Center based on potentially wrapped height
    int16_t x = (tft.width() - w) / 2;
    int16_t y = (tft.height() - h) / 2;
    tft.setCursor(x, y); tft.print(text); tft.setTextWrap(false); // Turn off wrap after printing
}

// =========================================================================
// ===                           SETUP                                   ===
// =========================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\nESP32 Energy Monitor Display - V9 (Time + Temp Fix)");

    // Initialize temperature sensor (already enabled by default on most ESP32 cores)
    internal_temp = temperatureRead(); // Get initial reading

    tft.init(240, 320);
    tft.setRotation(3); // Landscape: 320x240
    screen_width = tft.width(); // Store width AFTER rotation

    // --- Calculate Runtime UI Positions ---
    wifi_icon_y = (STATUS_BAR_HEIGHT - WIFI_ICON_HEIGHT) / 2;
    wifi_icon_x = screen_width - WIFI_ICON_WIDTH - WIFI_RIGHT_PADDING;
    temp_text_right_x = wifi_icon_x - TEMP_WIFI_GAP; // Position temp relative to wifi icon
    time_text_left_x = 5; // Left padding for time display

    Serial.printf("Runtime Pos: Width=%d, WiFi(%d,%d), TempRgtX(%d), TimeLeftX(%d)\n",
                  screen_width, wifi_icon_x, wifi_icon_y, temp_text_right_x, time_text_left_x);

    displayFullScreenMessage("Starting...", 2, ST77XX_WHITE); delay(500);

    // Initialize WiFi
    Serial.print("Connecting to WiFi: "); Serial.println(ssid);
    displayFullScreenMessage("Connecting WiFi...", 2, ST77XX_YELLOW);
    WiFi.mode(WIFI_STA); // Set WiFi mode to Station explicitly
    WiFi.setHostname("ESP32-EnergyMon");
    WiFi.begin(ssid, password);

    int connect_timeout = 20; // 10 seconds timeout (20 * 500ms)
    while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
        delay(500);
        Serial.print(".");
        connect_timeout--;
    }
    Serial.println();

    // Check Connection Result
    if (WiFi.status() == WL_CONNECTED) {
        current_rssi = WiFi.RSSI();
        internal_temp = temperatureRead(); // Update temp after connection potentially heats chip

        // Initialize NTP
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        delay(1000); // Wait for time sync
        updateTime();

        Serial.println("WiFi connected!");
        Serial.printf("  RSSI: %d dBm, Temp: %.1f C, Time: %s\n", 
                     current_rssi, internal_temp, time_str.c_str());
        displayFullScreenMessage("Connected!", 2, ST77XX_GREEN); delay(1500);
        initialDrawUI(); // Draw the screen layout
    } else {
        current_rssi = -100;
        Serial.println("WiFi connection Failed!");
        displayFullScreenMessage("WiFi Failed!", 3, ST77XX_RED);
        while (true) { delay(1000); } // Halt execution
    }
    first_loop = true; // Set flag for first loop execution
}


// =========================================================================
// ===                            MAIN LOOP                              ===
// =========================================================================
void loop() {
    bool dataFetchedSuccessfully = false;
    // Force redraw on the very first loop execution or after a reconnect
    bool force_redraw = first_loop;
    
    // Save current time at the beginning of the loop
    unsigned long current_time = millis();
    
    // --- Check if we need to update time display ---
    if (current_time - last_time_update >= TIME_UPDATE_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            // Update status values that change frequently
            internal_temp = temperatureRead();
            current_rssi = WiFi.RSSI();
            
            // Update time from NTP
            updateTime();
            last_time_update = current_time;
            
            // Update only the time portion of the status bar (more efficient)
            // We use false for force_redraw as we only want to redraw if time changed
            bool time_changed = (hours_str != prev_hours_str || minutes_str != prev_minutes_str || seconds_str != prev_seconds_str);
            if (time_changed) {
                // Calculate base position for time display
                int time_start_x = time_text_left_x;
                
                // Draw hours segment if changed
                if (hours_str != prev_hours_str || force_redraw)
                    drawTimeSegment(hours_str, prev_hours_str, time_start_x, force_redraw);
                
                // Draw first separator if needed
                int sep1_x = time_start_x + TIME_SEGMENT_WIDTH;
                if (force_redraw)
                    drawTimeSeparator(sep1_x, force_redraw);
                
                // Draw minutes segment if changed
                int min_x = sep1_x + TIME_SEPARATOR_WIDTH;
                if (minutes_str != prev_minutes_str || force_redraw)
                    drawTimeSegment(minutes_str, prev_minutes_str, min_x, force_redraw);
                
                // Draw second separator if needed
                int sep2_x = min_x + TIME_SEGMENT_WIDTH;
                if (force_redraw)
                    drawTimeSeparator(sep2_x, force_redraw);
                
                // Draw seconds segment (most frequently changing part)
                int sec_x = sep2_x + TIME_SEPARATOR_WIDTH;
                if (seconds_str != prev_seconds_str || force_redraw)
                    drawTimeSegment(seconds_str, prev_seconds_str, sec_x, force_redraw);
                
                // Update previous values for next comparison
                prev_hours_str = hours_str;
                prev_minutes_str = minutes_str;
                prev_seconds_str = seconds_str;
            }
        }
    }
    
    // --- Determine if it's time to fetch data ---
    static unsigned long last_fetch_time = 0;
    bool should_fetch_data = (current_time - last_fetch_time >= LOOP_DELAY_MS) || first_loop;
    
    if (should_fetch_data) {
        last_fetch_time = current_time;  // Update the last fetch time
        
        if (WiFi.status() == WL_CONNECTED) {
            // --- Update all status values ---
            current_rssi = WiFi.RSSI();
            internal_temp = temperatureRead();
            
            // --- Fetch Data via HTTP ---
            Serial.printf("Fetching data... (RSSI: %d dBm, Temp: %.1f C)\n", current_rssi, internal_temp);
            
            // Use a single HTTPClient instance per fetch
            HTTPClient localHttp; // Use local instance to avoid potential state issues
            localHttp.begin(dataUrl);
            localHttp.setTimeout(HTTP_TIMEOUT_MS); // Set timeout
            int httpCode = localHttp.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = localHttp.getString();
                // Adjust JSON doc size if needed based on actual payload complexity
                StaticJsonDocument<512> doc; // Reduced size, likely sufficient
                DeserializationError error = deserializeJson(doc, payload);
                if (!error) {
                    // Use .as<float>() or provide default if key might be missing
                    voltage = doc["voltage"].as<float>();
                    current = doc["current"].as<float>();
                    power_active = doc["power_active"].as<float>();
                    Serial.printf("Data Received: V=%.1f, C=%.2f, P=%.1f\n", voltage, current, power_active);
                    dataFetchedSuccessfully = true;
                } else {
                    Serial.print("JSON Parse Error: "); Serial.println(error.c_str());
                    // Keep old values if parse fails
                }
            } else if (httpCode > 0) {
                Serial.printf("HTTP Error: %d - %s\n", httpCode, HTTPClient::errorToString(httpCode).c_str());
                // Keep old values on HTTP error
            } else { // httpCode < 0 indicates connection error
                Serial.printf("HTTP Connection Failed: %s\n", localHttp.errorToString(httpCode).c_str());
                // Keep old values on connection error
            }
            localHttp.end(); // Important: release resources
            
            // --- Update Display (only if data fetch wasn't a complete failure, or force redraw) ---
            // We do a full status bar update when new data arrives or on force redraw
            updateStatusBar(force_redraw);
            // Only update main area if data was fetched or forced
            // (Keeps last valid reading on screen during temporary errors)
            if (dataFetchedSuccessfully || force_redraw) {
                updateMainDisplayArea(voltage, current, power_active, force_redraw);
                // Update previous state AFTER drawing comparison is done
                prev_voltage = voltage;
                prev_current = current;
                prev_power_active = power_active;
                // prev_power_color is updated inside updateMainDisplayArea
            }
            // prev_internal_temp, prev_rssi_level, prev_time_str are updated inside updateStatusBar if changed
            
        } else {
            // --- Handle WiFi Disconnection ---
            Serial.println("WiFi Disconnected. Reconnecting...");
            current_rssi = -100;
            internal_temp = -99; // Indicate status
            // Reset previous states that change during disconnect
            prev_rssi_level = -1;
            prev_time_str = "";
            prev_internal_temp = -100.0;
            prev_power_color = BG_COLOR; // Reset power color state
            
            displayFullScreenMessage("WiFi Lost!\nReconnecting...", 2, ST77XX_RED);
            
            WiFi.begin(ssid, password); // Attempt reconnect
            int reconnect_timeout = 10; // 5 seconds timeout
            while (WiFi.status() != WL_CONNECTED && reconnect_timeout > 0) {
                delay(500); Serial.print("*"); reconnect_timeout--;
            }
            
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("Reconnect failed.");
                displayFullScreenMessage("Reconnect Failed", 2, ST77XX_RED); delay(4000);
                // Re-draw initial UI to show failure state clearly
                initialDrawUI(); // Shows "Reconnect Failed" IP, 0 values etc.
                force_redraw = true; // Ensure next loop redraws everything if connection comes back later
            } else {
                current_rssi = WiFi.RSSI();
                internal_temp = temperatureRead();
                
                // Initialize NTP
                configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
                delay(1000); // Wait for time sync
                updateTime();
                
                Serial.println("WiFi Reconnected!");
                Serial.printf("  RSSI: %d dBm, Temp: %.1f C, Time: %s\n", 
                             current_rssi, internal_temp, time_str.c_str());
                displayFullScreenMessage("WiFi Reconnected!", 2, ST77XX_GREEN); delay(1500);
                // Re-draw the entire UI after successful reconnect
                initialDrawUI();
                force_redraw = true; // Ensure the first fetch after reconnect updates everything
            }
        }
        
        first_loop = false; // First loop execution or post-reconnect redraw is complete
    }
    
    // Short delay to allow CPU to do other tasks and reduce power consumption
    // Not a full 5-second delay anymore - we use a shorter delay to update the clock more frequently
    delay(200);  // 200ms delay provides responsive time updates while not taxing the CPU too much
}