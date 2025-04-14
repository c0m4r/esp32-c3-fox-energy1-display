/*
 * ST7789 Display Test
 * Basic test program for verifying ST7789 TFT display functionality on ESP32-C3.
 * Cycles through basic colors and text.
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with Gemini 2.5 Pro Preview 03-25
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

// Define the pins used for the ESP32-C3 Super Mini
// Use the specific GPIO numbers you connected the wires to.
#define TFT_CS        7  // Connected to display CS (using marked SS pin)
#define TFT_RST       3  // Connected to display RST (Example: GPIO3)
#define TFT_DC        2  // Connected to display DC (Example: GPIO2)

// SCK (GPIO4) and MOSI (GPIO6) are usually handled by the SPI library automatically
// when using the default hardware SPI pins.

// Initialize the display object
// For hardware SPI, SCK and MOSI don't need to be passed to the constructor if using default SPI pins
// The ESP32-C3 might use different SPI instances (SPI2, SPI3).
// Adafruit library often defaults correctly, but sometimes you might need &SPI syntax if issues arise.
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// If the default SPI pins (like GPIO4, GPIO6) are not working automatically,
// you might need to specify them explicitly (less common with Adafruit's constructor):
// Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST); // Might need SPI specific adjustments for C3

void setup(void) {
  Serial.begin(115200);
  Serial.println("ST7789 Test!");

  // Initialize the display
  tft.init(240, 320); // Initialize ST7789 screen (240x320)

  // OPTIONAL: Set rotation if needed (0, 1, 2 or 3)
  // tft.setRotation(1);

  Serial.println("Initialized");

  uint16_t time = millis();
  tft.fillScreen(ST77XX_BLACK); // Use ST77XX_BLACK or specific color code
  time = millis() - time;

  Serial.print("Screen fill time: "); Serial.println(time);

  delay(500);
}

void loop() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println("Hello World!");
  delay(1000);

  tft.fillScreen(ST77XX_RED);
  delay(1000);
  tft.fillScreen(ST77XX_GREEN);
  delay(1000);
  tft.fillScreen(ST77XX_BLUE);
  delay(1000);
}
