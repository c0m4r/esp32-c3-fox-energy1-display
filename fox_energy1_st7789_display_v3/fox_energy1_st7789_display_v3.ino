/*
 * Fox Energy Power Monitor Display (Version 3 - Refactored)
 * Displays power metrics (Voltage, Current, Power) fetched from a Fox REST API
 * on an ST7789 display connected to an ESP32-C3.
 * 
 * Features:
 * - WiFi persistence using NVS (survives power loss)
 * - Tile-based double buffering (eliminates flicker)
 * - Modular architecture with clean separation of concerns
 * - Optimized performance and memory usage
 * - Robust error handling and auto-reconnection
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with:
 * - v2 - Gemini 2.5 Pro Preview 03-25
 * - v3 - Claude Sonnet 4.5 (Thinking) + Gemini 3 Pro (High)
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include "secrets.h"
#include "config.h"
#include "types.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "data_fetcher.h"

// =========================================================================
// ===                      GLOBAL MANAGER INSTANCES                     ===
// =========================================================================

WiFiManager wifiMgr;
DisplayManager displayMgr;
DataFetcher dataFetcher;

// =========================================================================
// ===                      GLOBAL STATE VARIABLES                       ===
// =========================================================================

PowerData currentPower;
StatusData currentStatus;
unsigned long last_data_fetch = 0;
unsigned long last_time_update = 0;
bool first_loop = true;

// =========================================================================
// ===                        HELPER FUNCTIONS                           ===
// =========================================================================

/**
 * Update time from NTP server
 */
void updateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        currentStatus.hours = "--";
        currentStatus.minutes = "--";
        currentStatus.seconds = "--";
        currentStatus.time_str = "--:--:--";
        return;
    }
    
    char h_buffer[3], m_buffer[3], s_buffer[3], full_buffer[9];
    
    strftime(h_buffer, sizeof(h_buffer), "%H", &timeinfo);
    strftime(m_buffer, sizeof(m_buffer), "%M", &timeinfo);
    strftime(s_buffer, sizeof(s_buffer), "%S", &timeinfo);
    strftime(full_buffer, sizeof(full_buffer), "%H:%M:%S", &timeinfo);
    
    currentStatus.hours = String(h_buffer);
    currentStatus.minutes = String(m_buffer);
    currentStatus.seconds = String(s_buffer);
    currentStatus.time_str = String(full_buffer);
}

/**
 * Initialize NTP time synchronization
 */
void initializeNTP() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    delay(1000); // Wait for time sync
    updateTime();
    Serial.print("Time synchronized: ");
    Serial.println(currentStatus.time_str);
}

// =========================================================================
// ===                           SETUP                                   ===
// =========================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n========================================");
    Serial.println("ESP32 Energy Monitor Display - V3");
    Serial.println("Refactored with modular architecture");
    Serial.println("========================================\n");
    
    // Initialize display first
    displayMgr.begin();
    displayMgr.drawFullScreenMessage("Starting...", 2, ST77XX_WHITE);
    delay(500);
    
    // Initialize WiFi manager
    wifiMgr.begin();
    
    // Attempt WiFi connection with infinite retry
    Serial.println("Attempting WiFi connection...");
    displayMgr.drawFullScreenMessage("Connecting WiFi...", 2, ST77XX_YELLOW);
    
    while (!wifiMgr.connect()) {
        Serial.println("WiFi connection failed! Retrying in 30 seconds...");
        displayMgr.drawFullScreenMessage("WiFi Failed!\nRetrying in 30s...", 2, ST77XX_RED);
        
        // Countdown display
        for (int i = 30; i > 0; i--) {
            char msg[40];
            snprintf(msg, sizeof(msg), "WiFi Failed!\nRetry in %ds...", i);
            displayMgr.drawFullScreenMessage(msg, 2, ST77XX_RED);
            delay(1000);
        }
        
        Serial.println("Retrying WiFi connection...");
        displayMgr.drawFullScreenMessage("Connecting WiFi...", 2, ST77XX_YELLOW);
    }
    
    // Initialize current status
    currentStatus.internal_temp = temperatureRead();
    currentStatus.rssi = wifiMgr.getRSSI();
    
    // Initialize NTP
    initializeNTP();
    
    // Show connection success
    Serial.println("\n========================================");
    Serial.println("System Ready!");
    Serial.printf("WiFi SSID: %s\n", wifiMgr.getSSID().c_str());
    Serial.printf("RSSI: %ld dBm\n", currentStatus.rssi);
    Serial.printf("Temperature: %.1f°C\n", currentStatus.internal_temp);
    Serial.printf("Time: %s\n", currentStatus.time_str.c_str());
    Serial.println("========================================\n");
    
    displayMgr.drawFullScreenMessage("Connected!", 2, ST77XX_GREEN);
    delay(1500);
    
    // Draw initial UI
    displayMgr.drawInitialUI();
    first_loop = true;
}

// =========================================================================
// ===                            MAIN LOOP                              ===
// =========================================================================

void loop() {
    unsigned long current_time = millis();
    bool force_redraw = first_loop;
    
    // =====================================================================
    // === TIME UPDATE (Every Second)                                   ===
    // =====================================================================
    
    if (current_time - last_time_update >= TIME_UPDATE_INTERVAL) {
        if (wifiMgr.isConnected()) {
            // Update status values
            currentStatus.internal_temp = temperatureRead();
            currentStatus.rssi = wifiMgr.getRSSI();
            updateTime();
            
            // Update status bar
            displayMgr.drawStatusBar(currentStatus, false);
            
            last_time_update = current_time;
        }
    }
    
    // =====================================================================
    // === DATA FETCH (Every LOOP_DELAY_MS)                             ===
    // =====================================================================
    
    bool should_fetch = (current_time - last_data_fetch >= LOOP_DELAY_MS) || first_loop;
    
    if (should_fetch) {
        last_data_fetch = current_time;
        
        // Check WiFi connection
        if (!wifiMgr.isConnected()) {
            // Handle disconnection
            handleWiFiDisconnection();
            force_redraw = true; // Redraw after reconnection
        } else {
            // Fetch power data
            if (dataFetcher.fetchPowerData(DATA_URL, currentPower)) {
                // Successfully fetched data
                Serial.printf("[%s] V=%.1fV, C=%.2fA, P=%.1fW\n",
                             currentStatus.time_str.c_str(),
                             currentPower.voltage,
                             currentPower.current,
                             currentPower.power_active);
                
                // Update main display
                displayMgr.drawMainDisplay(currentPower, force_redraw);
                
                // Status bar is updated in its own timer loop (lines 150-162)
                // removing redundant call here to prevent flickering
                
            } else {
                // Data fetch failed
                Serial.printf("Data fetch failed (%d consecutive failures)\n",
                             dataFetcher.getConsecutiveFailures());
                
                // Keep displaying last valid data
                // Only show error if we have too many consecutive failures
                if (dataFetcher.getConsecutiveFailures() >= 5) {
                    Serial.println("WARNING: Multiple data fetch failures");
                }
            }
        }
        
        first_loop = false;
    }
    
    // =====================================================================
    // === LOOP TIMING                                                  ===
    // =====================================================================
    
    // Dynamic delay to maintain consistent loop timing
    unsigned long loop_duration = millis() - current_time;
    if (loop_duration < 200) {
        delay(200 - loop_duration);
    } else {
        // Loop took longer than expected, give CPU a minimal break
        delay(10);
    }
}

// =========================================================================
// ===                      WIFI DISCONNECTION HANDLER                   ===
// =========================================================================

void handleWiFiDisconnection() {
    Serial.println("\n!!! WiFi Disconnected !!!");
    Serial.println("Attempting reconnection...");
    
    // Update display to show disconnection
    currentStatus.rssi = -100;
    currentStatus.internal_temp = temperatureRead();
    displayMgr.drawStatusBar(currentStatus, true);
    
    // Show reconnection message
    displayMgr.drawFullScreenMessage("WiFi Lost!\nReconnecting...", 2, ST77XX_RED);
    
    // Attempt reconnection
    if (wifiMgr.reconnect()) {
        // Reconnection successful
        Serial.println("WiFi reconnected successfully!");
        
        // Re-initialize NTP
        initializeNTP();
        
        // Update status
        currentStatus.rssi = wifiMgr.getRSSI();
        currentStatus.internal_temp = temperatureRead();
        
        Serial.printf("WiFi restored - RSSI: %ld dBm\n", currentStatus.rssi);
        
        // Show success message
        displayMgr.drawFullScreenMessage("WiFi Reconnected!", 2, ST77XX_GREEN);
        delay(1500);
        
        // Redraw UI
        displayMgr.drawInitialUI();
        
        // Reset data fetcher failure counter
        dataFetcher.resetFailures();
        
    } else {
        // Reconnection failed
        Serial.println("Reconnection failed, will retry...");
        
        // Show failure state
        displayMgr.drawFullScreenMessage("Reconnecting...\nPlease wait", 2, ST77XX_ORANGE);
        delay(2000);
        
        // Redraw UI to show disconnected state
        displayMgr.drawInitialUI();
    }
}
