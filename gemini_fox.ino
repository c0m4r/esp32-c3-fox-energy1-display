/*
 * ESP32 Power Monitor (Gemini Fox Version)
 * Fetches power data (Voltage, Current, Power) via HTTP GET from a local API
 * and displays it on an ST7789 TFT screen connected to an ESP32.
 * Includes time synchronization via NTP and Wi-Fi status.
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with Gemini 2.5 Pro Preview 03-25
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <time.h> // Required for time functionality

// --- Pin Definitions ---
// Corrected pins as per your request
#define TFT_CS   7
#define TFT_DC   2 // Corrected
#define TFT_RST  3 // Corrected

// --- Display Dimensions (Horizontal Layout) ---
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// --- WiFi Credentials ---
const char* ssid = "ssid";
const char* password = "password";

// --- API URL ---
const char* apiUrl = "http://192.168.0.101/0000/get_current_parameters";

// --- Timezone Configuration ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// --- Display Object ---
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- Global Variables ---
float currentVoltage = 0.0;
float currentCurrent = 0.0;
float currentPower = 0.0; // Watts
bool apiDataValid = false;
String lastApiError = "";
bool lastApiStateValid = false; // Track previous API state for error screen clearing

unsigned long lastApiUpdateTime = 0;
const unsigned long apiUpdateInterval = 5000; // Update every 5 seconds

unsigned long lastDisplayUpdateTime = 0;
const unsigned long displayUpdateInterval = 500; // Update display faster (0.5 sec)

// --- Colors ---
#define COLOR_BG        ST77XX_BLACK
#define COLOR_STATUS_FG ST77XX_WHITE
#define COLOR_DEFAULT   ST77XX_WHITE
#define COLOR_VOLTAGE   ST77XX_CYAN
#define COLOR_CURRENT   ST77XX_YELLOW
#define COLOR_POWER_OK  ST77XX_GREEN
#define COLOR_POWER_WARN ST77XX_ORANGE
#define COLOR_POWER_HIGH ST77XX_RED
#define COLOR_UNIT      ST77XX_LIGHTGREY // Slightly dimmer for units

// Custom Color Definitions Added
#define ST77XX_LIGHTGREY 0xdedb // Hex color code for light grey
#define ST77XX_DARKGREY 0x738e  // Hex color code for dark grey

// --- Variables to Store Last Displayed Values (for flicker reduction) ---
String lastTimeString = "--:--:--";
String lastTempString = "";
int lastRssiBars = -1; // Use bars instead of raw RSSI for less frequent updates
String lastPowerValueStr = "";
String lastPowerUnitStr = "";
String lastVoltageValueStr = "";
String lastCurrentValueStr = "";
bool showingApiError = false; // Track if the error message is currently shown

// --- Constants for Character Size (Approximation) ---
const int CHAR_WIDTH = 6;
const int CHAR_HEIGHT = 8;

// --- Function Declarations ---
void connectWiFi();
void fetchPowerData();
void updateDisplay();
void drawStatusBar();
void drawMainData();
void clearTextArea(int16_t x, int16_t y, int16_t w, int16_t h);
void drawValueWithUnit(int16_t x, int16_t y, const String& valueStr, const String& unitStr, const String& lastValueStr, const String& lastUnitStr,
                       int valueFontSize, int unitFontSize, uint16_t valueColor, uint16_t unitColor, bool alignRight = false);
String getFormattedTime();
void drawWifiIcon(int16_t x, int16_t y, int32_t rssi);


// =========================================================================
// SETUP FUNCTION
// =========================================================================
void setup() {
    Serial.begin(115200);
    // while (!Serial); // Optional: wait for serial connection
    Serial.println("ESP32 Power Monitor Starting...");

    // Initialize Display
    Serial.println("Initializing Display...");
    tft.init(SCREEN_HEIGHT, SCREEN_WIDTH);
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setTextWrap(false); // Disable text wrapping globally for easier positioning
    tft.setCursor(10, SCREEN_HEIGHT / 2 - 20);
    tft.println("Starting Up...");
    delay(500); // Show message briefly

    // Connect to Wi-Fi
    tft.setCursor(10, SCREEN_HEIGHT / 2);
    tft.setTextColor(ST77XX_WHITE, COLOR_BG); // Ensure background is cleared
    tft.print("Connecting WiFi"); // Use print not println
    connectWiFi(); // Will print dots for progress

    // Initialize Time
    Serial.println("Configuring Time...");
    tft.fillScreen(COLOR_BG); // Clear previous messages
    tft.setCursor(10, SCREEN_HEIGHT / 2 - 10);
    tft.setTextColor(ST77XX_WHITE, COLOR_BG);
    tft.print("Syncing Time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        tft.fillRect(10, SCREEN_HEIGHT / 2 + 10, 200, 20, COLOR_BG); // Clear area below
        tft.setCursor(10, SCREEN_HEIGHT / 2 + 10);
        tft.setTextColor(ST77XX_RED, COLOR_BG);
        tft.print("Time Sync Failed!");
        delay(2000); // Show error briefly
    } else {
        Serial.println("Time Synchronized");
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }

    // Initial Data Fetch
    Serial.println("Performing initial data fetch...");
    fetchPowerData();

    Serial.println("Setup Complete. Starting main loop.");
    tft.fillScreen(COLOR_BG); // <--- FINAL CLEAR before loop starts drawing UI

    // Force initial draw by ensuring last values are different
    lastTimeString = "";
    lastTempString = "";
    lastRssiBars = -1;
    lastPowerValueStr = "";
    lastPowerUnitStr = "";
    lastVoltageValueStr = "";
    lastCurrentValueStr = "";

    lastDisplayUpdateTime = millis(); // Ensure display updates soon
    lastApiUpdateTime = millis();
}

// =========================================================================
// MAIN LOOP FUNCTION
// =========================================================================
void loop() {
    unsigned long currentMillis = millis();

    // Fetch data from API periodically
    if (currentMillis - lastApiUpdateTime >= apiUpdateInterval) {
        lastApiUpdateTime = currentMillis;
        if (WiFi.status() == WL_CONNECTED) {
            fetchPowerData();
        } else {
            Serial.println("WiFi Disconnected. Trying to reconnect...");
            apiDataValid = false; // Mark data as invalid
            lastApiError = "WiFi Lost";
            // Reset last displayed values if connection is lost to force redraw on reconnect
            lastPowerValueStr = ""; lastPowerUnitStr = "";
            lastVoltageValueStr = ""; lastCurrentValueStr = "";
            connectWiFi(); // Attempt reconnection
        }
    }

    // Update the display more frequently
    if (currentMillis - lastDisplayUpdateTime >= displayUpdateInterval) {
        lastDisplayUpdateTime = currentMillis;
        updateDisplay();
    }

    delay(10); // Small delay
}

// =========================================================================
// CONNECT WIFI FUNCTION (CORRECTED)
// =========================================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    Serial.print("Connecting to "); Serial.println(ssid);
    WiFi.begin(ssid, password);
    int attempts = 0;
    tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, COLOR_BG); // Set text properties for dots
    // int16_t initialCursorX, initialCursorY; // <<-- REMOVED
    // tft.getCursor(&initialCursorX, &initialCursorY); // <<-- REMOVED: Not available & not needed

    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500); Serial.print("."); tft.print("."); // Just print dot at current location
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!"); Serial.print("IP address: "); Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
        lastApiError = "WiFi Fail"; // Update error message
    }
}


// =========================================================================
// FETCH POWER DATA FUNCTION (REST API CALL)
// =========================================================================
void fetchPowerData() {
    Serial.print("Fetching data from API: "); Serial.println(apiUrl);

    HTTPClient http;
    WiFiClient client; // Use WiFiClient for better stability with redirects/timeouts
    http.begin(client, apiUrl);
    http.setConnectTimeout(4000);
    http.setTimeout(4000);

    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) { // Allow redirects
             if (httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                // If moved permanently, potentially update apiUrl here for future calls if desired
                payload = http.getString(); // Get payload after redirect if needed
            }
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, payload);
            if (error) {
                Serial.print(F("deserializeJson() failed: ")); Serial.println(error.f_str());
                apiDataValid = false; lastApiError = "JSON Err";
            } else {
                const char* voltageStr = doc["voltage"];
                const char* currentStr = doc["current"];
                const char* powerStr = doc["power_active"];
                if (voltageStr && currentStr && powerStr) {
                    currentVoltage = atof(voltageStr);
                    currentCurrent = atof(currentStr);
                    currentPower = atof(powerStr);
                    apiDataValid = true; lastApiError = ""; // Success
                    Serial.printf("Data OK: V=%.1f, A=%.2f, W=%.1f\n", currentVoltage, currentCurrent, currentPower);
                } else {
                    Serial.println("JSON keys missing!");
                    apiDataValid = false; lastApiError = "Key Miss";
                }
            }
        } else {
            Serial.printf("[HTTP] GET... failed, code: %d\n", httpCode);
            apiDataValid = false; lastApiError = "HTTP:" + String(httpCode);
        }
    } else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        apiDataValid = false; lastApiError = "Connect Err";
    }
    http.end();
}


// =========================================================================
// UPDATE DISPLAY FUNCTION (CORRECTED)
// =========================================================================
void updateDisplay() {
    // --- Handle API Error Display ---
    if (!apiDataValid) {
        if (!showingApiError) { // Only redraw the error message area if it wasn't already showing
             // Clear the main data area before showing error
             int16_t mainDataTop = 30;
             int16_t mainDataBottom = SCREEN_HEIGHT - 10;
             clearTextArea(0, mainDataTop, SCREEN_WIDTH, mainDataBottom - mainDataTop);

             tft.setTextSize(3);
             tft.setTextColor(ST77XX_RED, COLOR_BG);
             // Calculate centered position
             int16_t textWidth = lastApiError.length() * CHAR_WIDTH * 3;
             int16_t textHeight = CHAR_HEIGHT * 3;
             int16_t x = (SCREEN_WIDTH - textWidth) / 2;
             int16_t y = mainDataTop + (mainDataBottom - mainDataTop - textHeight) / 2;
             // Use Arduino max() or cast 0 to int16_t
             tft.setCursor(max((int16_t)0, x), y); // <<-- CORRECTED max() call using cast
             // Or: tft.setCursor(max(0, x), y); // If Arduino Core defines a type-flexible max() macro (often does)

             tft.print(lastApiError);
             showingApiError = true; // Mark error as displayed
             // Clear last known values so data redraws fully when API recovers
             lastPowerValueStr = ""; lastPowerUnitStr = "";
             lastVoltageValueStr = ""; lastCurrentValueStr = "";
        }
        // Still draw the status bar even if API has errors
        drawStatusBar();
    } else {
        // --- API is Valid - Draw Normal Data ---
        if (showingApiError) {
            // If error was previously shown, clear the entire screen to reset layout cleanly
            tft.fillScreen(COLOR_BG);
            // Force redraw of all elements next time
            lastTimeString = ""; lastTempString = ""; lastRssiBars = -1;
            lastPowerValueStr = ""; lastPowerUnitStr = "";
            lastVoltageValueStr = ""; lastCurrentValueStr = "";
            showingApiError = false; // Error is no longer showing
        }
        // Draw status bar and main data areas (potentially only updating changed elements)
        drawStatusBar();
        drawMainData();
    }
     lastApiStateValid = apiDataValid; // Store current API state for next cycle comparison
}

// =========================================================================
// Utility to clear a specific rectangular area
// =========================================================================
void clearTextArea(int16_t x, int16_t y, int16_t w, int16_t h) {
     // Ensure dimensions are not negative which can cause issues with fillRect
     if (w <= 0 || h <= 0) return;
     // Add boundary checks (optional but safer)
     // if (x < 0) x = 0;
     // if (y < 0) y = 0;
     // if (x + w > tft.width()) w = tft.width() - x;
     // if (y + h > tft.height()) h = tft.height() - y;

     tft.fillRect(x, y, w, h, COLOR_BG);
}

// =========================================================================
// DRAW STATUS BAR (Top Section) - Optimized
// =========================================================================
void drawStatusBar() {
    int16_t barHeight = 24; // Height of the status bar
    int16_t textY = 5;      // Y position for text within the bar
    int16_t textMargin = 5; // Left/Right margin

    // --- Time (Left) ---
    String currentTimeString = getFormattedTime();
    if (currentTimeString != lastTimeString) {
        int timeFontSize = 3;
        int16_t oldWidth = lastTimeString.length() * CHAR_WIDTH * timeFontSize;
        // Clear previous time area
        clearTextArea(textMargin, textY - 1, oldWidth + 2, (CHAR_HEIGHT * timeFontSize) + 2); // Slightly larger clear area
        // Draw new time
        tft.setTextSize(timeFontSize);
        tft.setTextColor(COLOR_STATUS_FG, COLOR_BG);
        tft.setCursor(textMargin, textY);
        tft.print(currentTimeString);
        lastTimeString = currentTimeString; // Update last drawn time
    }

    // --- Temp & WiFi (Right) ---
    // Temp
    String tempString = "--.-C"; // Default
    float tempC = temperatureRead();
    if (!isnan(tempC)) {
         tempString = String(tempC, 1) + "C";
    }
    if (tempString != lastTempString) {
         int tempFontSize = 3;
         // Estimate width based on LAST string for clearing
         int16_t oldWidth = lastTempString.length() * CHAR_WIDTH * tempFontSize;
         int16_t oldTempX = SCREEN_WIDTH - oldWidth - textMargin; // Approx X of OLD text
          if (lastTempString.length() > 0) { // Avoid clearing 0-width if last string was empty
            clearTextArea(oldTempX - 2, textY - 1, oldWidth + 4, (CHAR_HEIGHT * tempFontSize) + 2);
          }

         // Calculate position for NEW temp string
         int16_t newWidth = tempString.length() * CHAR_WIDTH * tempFontSize;
         int16_t newTempX = SCREEN_WIDTH - newWidth - textMargin; // Approx X of NEW text

        // Draw new temp
         tft.setTextSize(tempFontSize);
         tft.setTextColor(COLOR_STATUS_FG, COLOR_BG);
         tft.setCursor(newTempX, textY);
         tft.print(tempString);
         lastTempString = tempString;
    }

    // WiFi Icon
    int32_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
    int bars = 0;
    if (rssi >= -60) bars = 4; else if (rssi >= -70) bars = 3; else if (rssi >= -80) bars = 2; else if (rssi < -80 && rssi != -100) bars = 1; else bars = 0;

    if (bars != lastRssiBars) {
        int wifiIconWidth = 28; // Width needed for wifi icon
        int16_t spacing = 34;
        // Calculate wifi X based on CURRENT temp string width
        int16_t tempWidthNow = lastTempString.length() * CHAR_WIDTH * 2;
        int16_t tempXNow = SCREEN_WIDTH - tempWidthNow - textMargin;
        int16_t wifiX = tempXNow - wifiIconWidth - spacing;

        // Clear previous icon area (fixed position relative to temp)
        clearTextArea(wifiX, textY - 1, wifiIconWidth, 18); // Icon height approx 17 + buffer
        // Draw new icon
        drawWifiIcon(wifiX, textY, rssi); // Y position aligns with text baseline
        lastRssiBars = bars;
    }
}


// =========================================================================
// GET FORMATTED TIME STRING (HH:MM:SS)
// =========================================================================
String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }
    char timeStr[9]; // HH:MM:SS\0
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
}


// =========================================================================
// DRAW WIFI ICON (Simple Bars)
// =========================================================================
void drawWifiIcon(int16_t x, int16_t y, int32_t rssi) {
    int bars = 0;
    if (rssi >= -60) bars = 4;       // Excellent
    else if (rssi >= -70) bars = 3;  // Good
    else if (rssi >= -80) bars = 2;  // Fair
    else if (rssi < -80 && rssi != -100) bars = 1; // Weak
    else bars = 0; // No signal or disconnected

    uint16_t barColor = (WiFi.status() == WL_CONNECTED) ? ST77XX_WHITE : ST77XX_DARKGREY;
    uint16_t barHeight[] = {5, 9, 13, 17}; // Heights of the 4 bars
    uint16_t barWidth = 4;
    uint16_t barSpacing = 2;
    // Try aligning bottom of icon slightly lower than text bottom (more visually centered in status bar)
    int16_t startY = y + (CHAR_HEIGHT*2) - barHeight[3] + 2; // Adjusted alignment slightly


    for (int i = 0; i < 4; i++) {
        uint16_t currentBarColor = (i < bars) ? barColor : ST77XX_DARKGREY; // Dim unused bars
        // Draw rect from top-left: x, y_start + (total_height - bar_height), width, bar_height
        tft.fillRect(x + (barWidth + barSpacing) * i, startY + (barHeight[3] - barHeight[i]), barWidth, barHeight[i], currentBarColor);
    }
}

// =========================================================================
// DRAW MAIN DATA (Power, Voltage, Current) - Optimized
// =========================================================================
void drawMainData() {
    if (!apiDataValid) return; // Should be handled by updateDisplay, but extra check

    // Define approximate areas
    int16_t mainDataTop = 30;
    int16_t mainDataBottom = SCREEN_HEIGHT - 10;
    int16_t middleY = mainDataTop + (mainDataBottom - mainDataTop) / 2;
    int16_t bottomLineY = mainDataBottom - (CHAR_HEIGHT * 3) - 5; // Y position for Voltage/Current baseline (Size 3)


    // --- Power Display (Center, Large) ---
    String powerValueStr;
    String powerUnitStr;
    float displayPower = currentPower;
    uint16_t powerColor = COLOR_POWER_OK;

    if (currentPower > 3500.0) powerColor = COLOR_POWER_HIGH;
    else if (currentPower > 2000.0) powerColor = COLOR_POWER_WARN;

    if (currentPower > 999.0) {
        displayPower = currentPower / 1000.0;
        powerValueStr = String(displayPower, (displayPower < 10.0) ? 2 : 1);
        powerUnitStr = "kW";
    } else {
        powerValueStr = String(currentPower, 0);
        powerUnitStr = "W";
    }

    int powerValueFontSize = 12;
    int powerUnitFontSize = 4;

    // Only redraw if power value or unit string has changed
    if (powerValueStr != lastPowerValueStr || powerUnitStr != lastPowerUnitStr) {
         // --- Calculate widths based on OLD strings for clearing ---
         int16_t oldPowerValueWidth = lastPowerValueStr.length() * CHAR_WIDTH * powerValueFontSize;
         int16_t oldPowerUnitWidth = lastPowerUnitStr.length() * CHAR_WIDTH * powerUnitFontSize;
         int16_t oldTotalPowerWidth = 0;
         if (lastPowerValueStr.length() > 0) { // Handle initial empty state
            oldTotalPowerWidth = oldPowerValueWidth + oldPowerUnitWidth + 5;
         }

         // Clear the combined area of the OLD text
         int16_t oldPowerValueX = (SCREEN_WIDTH - oldTotalPowerWidth) / 2;
         int16_t oldPowerValueY = middleY - (powerValueFontSize * CHAR_HEIGHT) / 2; // Centered vertically approx
         int16_t oldClearHeight = (powerValueFontSize * CHAR_HEIGHT); // Max height based on larger font
         if (oldTotalPowerWidth > 0) { // Only clear if there was something previously
             clearTextArea(oldPowerValueX - 2, oldPowerValueY - 2, oldTotalPowerWidth + 4, oldClearHeight + 4); // Clear slightly larger area
         }

        // --- Calculate positions based on NEW strings ---
         int16_t powerValueWidth = powerValueStr.length() * CHAR_WIDTH * powerValueFontSize;
         int16_t powerUnitWidth = powerUnitStr.length() * CHAR_WIDTH * powerUnitFontSize;
         int16_t totalPowerWidth = powerValueWidth + powerUnitWidth + 5; // Spacing


         // --- Draw NEW Power Value ---
         int16_t powerValueX = (SCREEN_WIDTH - totalPowerWidth) / 2;
         int16_t powerValueY = middleY - (powerValueFontSize * CHAR_HEIGHT) / 2; // Centered vertically approx
         tft.setTextSize(powerValueFontSize);
         tft.setTextColor(powerColor, COLOR_BG);
         tft.setCursor(max((int16_t)0,powerValueX), powerValueY); // Ensure X isn't negative
         tft.print(powerValueStr);

         // --- Draw NEW Power Unit ---
         int16_t powerUnitX = powerValueX + powerValueWidth + 5;
         // Align unit baseline roughly with value baseline
         int16_t powerUnitY = powerValueY + (powerValueFontSize - powerUnitFontSize) * CHAR_HEIGHT;
         tft.setTextSize(powerUnitFontSize);
         tft.setTextColor(COLOR_UNIT, COLOR_BG);
         tft.setCursor(max((int16_t)0, powerUnitX), powerUnitY); // Ensure X isn't negative
         tft.print(powerUnitStr);

         // Update last known strings
         lastPowerValueStr = powerValueStr;
         lastPowerUnitStr = powerUnitStr;
    }


    // --- Voltage Display (Bottom Left) ---
    int voltageValueFontSize = 3;
    int voltageUnitFontSize = 2;
    String voltageValueStr = String(currentVoltage, 1);
    String voltageUnitStr = "V";

    if (voltageValueStr != lastVoltageValueStr) {
         int16_t voltageX = 15; // Indent from left
         int16_t voltageY = bottomLineY;

         // Clear OLD voltage value/unit area
         if (lastVoltageValueStr.length() > 0) { // Handle initial case
            int16_t oldVoltageValueWidth = lastVoltageValueStr.length() * CHAR_WIDTH * voltageValueFontSize;
            int16_t oldVoltageUnitWidth = voltageUnitStr.length() * CHAR_WIDTH * voltageUnitFontSize; // Unit assumed constant V
            int16_t oldTotalWidth = oldVoltageValueWidth + oldVoltageUnitWidth + 3;
            clearTextArea(voltageX - 1, voltageY - 2, oldTotalWidth + 2, (voltageValueFontSize * CHAR_HEIGHT) + 4);
         }

         // Draw NEW voltage value
         tft.setTextSize(voltageValueFontSize);
         tft.setTextColor(COLOR_VOLTAGE, COLOR_BG);
         tft.setCursor(voltageX, voltageY);
         tft.print(voltageValueStr);

        // Draw NEW voltage unit (always V)
         int16_t voltageValueWidth = voltageValueStr.length() * CHAR_WIDTH * voltageValueFontSize; // Use NEW width
         tft.setTextSize(voltageUnitFontSize);
         tft.setTextColor(COLOR_UNIT, COLOR_BG);
         // Align unit baseline roughly
         tft.setCursor(voltageX + voltageValueWidth + 3, voltageY + (voltageValueFontSize - voltageUnitFontSize)*CHAR_HEIGHT);
         tft.print(voltageUnitStr);

         lastVoltageValueStr = voltageValueStr; // Update last known string
    }


    // --- Current Display (Bottom Right) ---
    int currentValueFontSize = 3;
    int currentUnitFontSize = 2;
    String currentValueStr = String(currentCurrent, 2);
    String currentUnitStr = "A";

    if(currentValueStr != lastCurrentValueStr) {

        // Clear OLD current value/unit area (need to calculate based on OLD string)
        if (lastCurrentValueStr.length() > 0) { // Handle initial case
            int16_t oldCurrentValueWidth = lastCurrentValueStr.length() * CHAR_WIDTH * currentValueFontSize;
            int16_t oldCurrentUnitWidth = currentUnitStr.length() * CHAR_WIDTH * currentUnitFontSize; // Assumed constant A
            int16_t oldTotalWidth = oldCurrentValueWidth + oldCurrentUnitWidth + 3;
            int16_t oldCurrentX = SCREEN_WIDTH - oldTotalWidth - 15;
            clearTextArea(oldCurrentX - 1, bottomLineY - 2, oldTotalWidth + 2, (currentValueFontSize * CHAR_HEIGHT) + 4);
        }

        // Calculate position based on NEW string width for right-alignment
        int16_t currentValueWidth = currentValueStr.length() * CHAR_WIDTH * currentValueFontSize;
        int16_t currentUnitWidth = currentUnitStr.length() * CHAR_WIDTH * currentUnitFontSize;
        int16_t totalCurrentWidth = currentValueWidth + currentUnitWidth + 3;
        int16_t currentX = SCREEN_WIDTH - totalCurrentWidth - 15; // Position from right edge
        int16_t currentY = bottomLineY; // Align vertically with voltage

        // Draw NEW Current Value
        tft.setTextSize(currentValueFontSize);
        tft.setTextColor(COLOR_CURRENT, COLOR_BG);
        tft.setCursor(max((int16_t)0,currentX), currentY); // Ensure X non-negative
        tft.print(currentValueStr);

        // Draw NEW Current Unit
        tft.setTextSize(currentUnitFontSize);
        tft.setTextColor(COLOR_UNIT, COLOR_BG);
        // Align unit baseline roughly
        // CORRECTED LINE BELOW: Cast result of addition to int16_t to match first argument of max()
        tft.setCursor(max((int16_t)0, (int16_t)(currentX + currentValueWidth + 3)), currentY + (currentValueFontSize - currentUnitFontSize)*CHAR_HEIGHT);
        tft.print(currentUnitStr);

        lastCurrentValueStr = currentValueStr; // Update last known string
    }
}
