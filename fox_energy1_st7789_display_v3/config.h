/*
 * Configuration Header for Fox Energy Power Monitor Display
 * Contains all compile-time constants, pin definitions, and settings
 */

#ifndef CONFIG_H
#define CONFIG_H

// =========================================================================
// ===                     HARDWARE CONFIGURATION                        ===
// =========================================================================

// --- Display Pins (ST7789) ---
#define TFT_CS        7  // Chip Select
#define TFT_RST       3  // Reset
#define TFT_DC        2  // Data/Command

// --- Display Resolution ---
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// =========================================================================
// ===                     NETWORK CONFIGURATION                         ===
// =========================================================================

// --- NTP Configuration ---
#define NTP_SERVER           "pool.ntp.org"
#define GMT_OFFSET_SEC       3600      // GMT+1
#define DAYLIGHT_OFFSET_SEC  3600      // Daylight saving time

// =========================================================================
// ===                      TIMING CONFIGURATION                         ===
// =========================================================================

#define LOOP_DELAY_MS           1000   // API request interval (1 second)
#define HTTP_TIMEOUT_MS         4000   // Max time to wait for HTTP response
#define TIME_UPDATE_INTERVAL    1000   // Update time every second
#define WIFI_CONNECT_TIMEOUT    20     // WiFi connection timeout (20 * 500ms = 10s)
#define WIFI_RECONNECT_TIMEOUT  10     // WiFi reconnection timeout (10 * 500ms = 5s)

// =========================================================================
// ===                    UI APPEARANCE CONSTANTS                        ===
// =========================================================================

// --- Status Bar Layout ---
#define STATUS_BAR_HEIGHT     40
#define STATUS_BAR_V_PADDING  20       // Extra vertical padding below status bar
#define STATUS_BAR_FONT_SIZE  2

// --- Font Sizes ---
#define POWER_VALUE_FONT_SIZE 11       // Large power value
#define POWER_UNIT_FONT_SIZE  3        // "W" or "kW" unit
#define VA_FONT_SIZE          5        // Voltage and Current display
#define TIME_FONT_SIZE        2        // Time display

// --- Icon Dimensions & Paddings ---
#define WIFI_ICON_WIDTH       24
#define WIFI_ICON_HEIGHT      18
#define WIFI_RIGHT_PADDING    5        // Padding from right edge to WiFi icon
#define TEMP_WIFI_GAP         8        // Gap between Temp text and WiFi icon
#define TEMP_MAX_WIDTH        45       // Max estimated width for clearing Temp text
#define TIME_MAX_WIDTH        85       // Width for time display (HH:MM:SS)

// --- Time Display Layout ---
#define TIME_SEGMENT_WIDTH     28      // Fixed width for each time segment (HH, MM, SS)
#define TIME_SEPARATOR_WIDTH   8       // Width for ":" separator
#define TIME_TOTAL_WIDTH       (3*TIME_SEGMENT_WIDTH + 2*TIME_SEPARATOR_WIDTH)

// =========================================================================
// ===                         COLOR DEFINITIONS                         ===
// =========================================================================

// --- Basic Colors ---
#define ST77XX_DARKGREY       0x4228   // Custom dark grey
#define BG_COLOR              ST77XX_BLACK
#define STATUS_BAR_BG_COLOR   ST77XX_BLACK
#define STATUS_BAR_TEXT_COLOR ST77XX_WHITE
#define STATUS_BAR_LINE_COLOR ST77XX_DARKGREY

// --- Power Display Colors (Thresholds) ---
#define POWER_COLOR_NORMAL    ST77XX_GREEN   // 0 - 1500 W
#define POWER_COLOR_MEDIUM    ST77XX_YELLOW  // >1500 - 2500 W
#define POWER_COLOR_HIGH      ST77XX_ORANGE  // >2500 - 3500 W
#define POWER_COLOR_MAX       ST77XX_RED     // >3500 W

// --- Other Element Colors ---
#define VOLTAGE_COLOR         ST77XX_CYAN
#define CURRENT_COLOR         ST77XX_MAGENTA
#define WIFI_ICON_COLOR       ST77XX_WHITE
#define TIME_COLOR            ST77XX_WHITE

// --- Temperature Colors (Thresholds) ---
#define TEMP_COLOR_GREEN      ST77XX_GREEN   // < 60 C
#define TEMP_COLOR_YELLOW     ST77XX_YELLOW  // 60 - 65 C
#define TEMP_COLOR_ORANGE     ST77XX_ORANGE  // 66 - 70 C
#define TEMP_COLOR_RED        ST77XX_RED     // > 70 C

// =========================================================================
// ===                    DOUBLE BUFFERING CONFIG                        ===
// =========================================================================

// --- Tile-Based Buffering ---
#define ENABLE_DOUBLE_BUFFER  true     // Enabled - canvas approach fixed
#define TILE_HEIGHT           30       // Height of each tile in pixels
#define NUM_TILES             (SCREEN_HEIGHT / TILE_HEIGHT)  // 8 tiles for 240px height
#define TILE_BUFFER_SIZE      (SCREEN_WIDTH * TILE_HEIGHT)   // 9600 pixels = 19.2KB

// =========================================================================
// ===                    PERFORMANCE TUNING                             ===
// =========================================================================

// --- Update Thresholds (avoid unnecessary redraws) ---
#define POWER_CHANGE_THRESHOLD    0.5   // Redraw power if change > 0.5W
#define CURRENT_CHANGE_THRESHOLD  0.05  // Redraw current if change > 0.05A

// --- WiFi Reconnection ---
#define WIFI_RECONNECT_MAX_ATTEMPTS  3     // Max reconnection attempts before showing error
#define WIFI_RECONNECT_BACKOFF_MS    1000  // Initial backoff delay (exponential)

#endif // CONFIG_H
