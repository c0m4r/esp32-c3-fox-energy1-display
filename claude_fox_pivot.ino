/*
 * ESP32 Power Monitor Display (Claude Pivot Version)
 * Displays power metrics fetched via HTTP on an ST7789 display.
 * Assumed vertical/pivot layout.
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with Claude 3.5 Sonnet
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Define pins
#define TFT_CS   7  // GPIO7
#define TFT_RST  3  // GPIO5
#define TFT_DC   2  // GPIO3
#define TFT_MOSI 6  // GPIO6
#define TFT_SCLK 4  // GPIO4

// Display dimensions
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Create display instance with explicit pin mapping
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// WiFi credentials
const char* ssid = "ssid";
const char* password = "password";

// API endpoint
const char* apiUrl = "http://192.168.0.101/0000/get_current_parameters";

// Time configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // Change to your timezone (GMT+1)
const int daylightOffset_sec = 3600;

// Interval for data refresh (in milliseconds)
const unsigned long dataRefreshInterval = 2000;
unsigned long lastDataRefresh = 0;

// Variables to store sensor data
String voltage = "0.0";
String current = "0.0";
String power = "0.0";
float temperature = 0.0;
int wifiSignalStrength = 0;

// Define colors
#define BLACK       0x0000
#define WHITE       0xFFFF
#define GREY        0x7BEF
#define LIGHTGREY   0xC618
#define DARKGREY    0x39E7
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define CYAN        0x07FF
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define ORANGE      0xFD20

// WiFi signal icons (3 levels plus disconnected)
const uint8_t WIFI_ICON_WIDTH = 16;
const uint8_t WIFI_ICON_HEIGHT = 16;

void setup() {
  Serial.begin(115200);
  
  Serial.println("\n\n--- ESP32 Power Monitor Starting ---");
  
  // Initialize the display
  Serial.println("Initializing display...");
  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.setRotation(0);
  tft.fillScreen(BLACK);
  
  // Draw startup screen
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.println("Power Monitor");
  tft.setTextSize(1);
  tft.setCursor(30, 150);
  tft.println("Starting up...");
  
  // Connect to WiFi
  connectWiFi();
  
  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Draw the static elements of the UI
  drawStatusBar();
  drawMainLayout();
  
  Serial.println("Setup complete");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Update time display
  updateTime();
  
  // Update WiFi signal strength
  updateWifiSignalStrength();
  
  // Update temperature
  updateTemperature();
  
  // Fetch and display data at interval
  if (currentMillis - lastDataRefresh >= dataRefreshInterval) {
    lastDataRefresh = currentMillis;
    fetchDataFromAPI();
    updateDisplayValues();
  }
  
  delay(100);  // Small delay to prevent CPU hogging
}

void connectWiFi() {
  tft.fillRect(30, 170, 180, 20, BLACK);
  tft.setCursor(30, 170);
  tft.setTextColor(WHITE);
  tft.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    tft.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.fillRect(30, 170, 180, 20, BLACK);
    tft.setCursor(30, 170);
    tft.println("WiFi connected!");
    Serial.println("WiFi connected!");
    delay(1000);
  } else {
    tft.fillRect(30, 170, 180, 20, BLACK);
    tft.setCursor(30, 170);
    tft.setTextColor(RED);
    tft.println("WiFi connection failed!");
    Serial.println("WiFi connection failed!");
    delay(3000);
  }
}

void fetchDataFromAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    http.begin(apiUrl);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("API Response: " + payload);
      
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        // Extract values from JSON
        String status = doc["status"].as<String>();
        
        if (status == "ok") {
          voltage = doc["voltage"].as<String>();
          current = doc["current"].as<String>();
          power = doc["power_active"].as<String>();
          Serial.println("Power: " + power + "W, Voltage: " + voltage + "V, Current: " + current + "A");
        } else {
          Serial.println("API status not OK");
        }
      } else {
        Serial.println("JSON parsing error");
      }
    } else {
      Serial.print("HTTP Error: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  } else {
    // Try to reconnect to WiFi if disconnected
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Reconnecting...");
      connectWiFi();
    }
  }
}

void updateWifiSignalStrength() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiSignalStrength = -1;  // Disconnected
  } else {
    long rssi = WiFi.RSSI();
    
    // Convert RSSI to signal strength (0-3)
    if (rssi <= -80) {
      wifiSignalStrength = 0;      // Poor signal
    } else if (rssi <= -70) {
      wifiSignalStrength = 1;      // Medium signal
    } else {
      wifiSignalStrength = 2;      // Strong signal
    }
  }
  
  // Draw WiFi icon
  drawWifiIcon();
}

void updateTemperature() {
  // ESP32 internal temperature sensor reading
  temperature = temperatureRead();
  
  // Display temperature
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(1);
  tft.setCursor(170, 10);
  tft.printf("%2.1f C", temperature);
}

void updateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    tft.setTextColor(RED, BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 10);
    tft.print("Time error");
    return;
  }
  
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 10);
  tft.printf("%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void drawWifiIcon() {
  int x = 210;
  int y = 10;
  
  // Clear the area first
  tft.fillRect(x, y, 16, 16, BLACK);
  
  if (wifiSignalStrength == -1) {
    // No connection - draw X
    tft.drawLine(x, y, x+16, y+16, RED);
    tft.drawLine(x+16, y, x, y+16, RED);
    return;
  }
  
  // Draw signal arcs based on strength
  if (wifiSignalStrength >= 0) {
    // Small arc - always visible if connected
    tft.drawCircleHelper(x+8, y+18, 12, 1, WHITE);
  }
  
  if (wifiSignalStrength >= 1) {
    // Medium arc
    tft.drawCircleHelper(x+8, y+18, 8, 1, WHITE);
  }
  
  if (wifiSignalStrength >= 2) {
    // Strong arc
    tft.drawCircleHelper(x+8, y+18, 4, 1, WHITE);
  }
  
  // Center dot
  tft.fillCircle(x+8, y+16, 1, WHITE);
}

void drawStatusBar() {
  // Draw status bar background
  tft.fillRect(0, 0, 240, 25, BLACK);
  tft.drawFastHLine(0, 25, 240, DARKGREY);
  
  // Initial placeholders
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(1);
  
  // Time placeholder
  tft.setCursor(5, 10);
  tft.print("--:--:--");
  
  // Temperature placeholder
  tft.setCursor(170, 10);
  tft.print("--.- C");
  
  // WiFi icon placeholder
  drawWifiIcon();
}

void drawMainLayout() {
  // Clear main area
  tft.fillRect(0, 26, 240, 294, BLACK);
  
  // Power section
  tft.setTextColor(LIGHTGREY, BLACK);
  tft.setTextSize(1);
  tft.setCursor(100, 70);
  tft.print("POWER");
  
  // Divider between power and voltage/current
  tft.drawFastHLine(20, 190, 200, DARKGREY);
  
  // Divider between voltage and current
  tft.drawFastVLine(120, 190, 120, DARKGREY);
  
  // Labels for voltage and current
  tft.setTextColor(LIGHTGREY, BLACK);
  tft.setTextSize(1);
  
  tft.setCursor(45, 200);
  tft.print("VOLTAGE");
  
  tft.setCursor(160, 200);
  tft.print("CURRENT");
  
  // Initial values
  updateDisplayValues();
}

void updateDisplayValues() {
  // Update power display (center, large)
  tft.setTextColor(GREEN, BLACK);
  tft.setTextSize(3);
  // Clear previous value
  tft.fillRect(40, 100, 160, 30, BLACK);
  
  // Center the text
  int16_t x1, y1;
  uint16_t w, h;
  String powerText = power + " W";
  tft.getTextBounds(powerText, 0, 0, &x1, &y1, &w, &h);
  int xPos = (SCREEN_WIDTH - w) / 2;
  
  tft.setCursor(xPos, 100);
  tft.print(power);
  tft.setTextSize(2);
  tft.print(" W");
  
  // Update voltage display (bottom left)
  tft.setTextColor(CYAN, BLACK);
  tft.setTextSize(2);
  // Clear previous value
  tft.fillRect(10, 220, 100, 50, BLACK);
  
  // Center in its area
  String voltageText = voltage + " V";
  tft.getTextBounds(voltageText, 0, 0, &x1, &y1, &w, &h);
  xPos = 10 + ((110 - w) / 2); // Center in left half
  
  tft.setCursor(xPos, 230);
  tft.print(voltage);
  tft.setTextSize(1);
  tft.print(" V");
  
  // Update current display (bottom right)
  tft.setTextColor(YELLOW, BLACK);
  tft.setTextSize(2);
  // Clear previous value
  tft.fillRect(130, 220, 100, 50, BLACK);
  
  // Center in its area
  String currentText = current + " A";
  tft.getTextBounds(currentText, 0, 0, &x1, &y1, &w, &h);
  xPos = 130 + ((110 - w) / 2); // Center in right half
  
  tft.setCursor(xPos, 230);
  tft.print(current);
  tft.setTextSize(1);
  tft.print(" A");
}
