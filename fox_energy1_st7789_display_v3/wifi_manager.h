/*
 * WiFi Manager with NVS Persistence
 * Handles WiFi connection, reconnection, and credential storage
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "types.h"

class WiFiManager {
public:
    WiFiManager() : 
        state(WIFI_DISCONNECTED),
        reconnect_attempts(0),
        last_reconnect_attempt(0),
        preferences_initialized(false) {}
    
    /**
     * Initialize WiFi manager and load credentials from NVS
     */
    void begin() {
        preferences.begin("wifi_config", false); // false = read/write mode
        preferences_initialized = true;
        
        // Load credentials from NVS or use defaults
        if (loadCredentials()) {
            Serial.println("WiFi credentials loaded from NVS");
        } else {
            Serial.println("No saved credentials, using defaults");
            saved_ssid = DEFAULT_SSID;
            saved_password = DEFAULT_PASSWORD;
            saveCredentials(saved_ssid.c_str(), saved_password.c_str());
        }
        
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("ESP32-EnergyMon");
    }
    
    /**
     * Connect to WiFi using saved credentials
     * Returns true if connected, false otherwise
     */
    bool connect() {
        return connect(saved_ssid.c_str(), saved_password.c_str());
    }
    
    /**
     * Connect to WiFi using provided credentials
     * Saves credentials to NVS if connection successful
     */
    bool connect(const char* ssid, const char* password) {
        state = WIFI_CONNECTING;
        Serial.print("Connecting to WiFi: ");
        Serial.println(ssid);
        
        WiFi.begin(ssid, password);
        
        int timeout = WIFI_CONNECT_TIMEOUT;
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print(".");
            timeout--;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            state = WIFI_CONNECTED;
            Serial.println("WiFi connected!");
            Serial.print("  IP: ");
            Serial.println(WiFi.localIP());
            Serial.print("  RSSI: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            
            // Save credentials if they're different from what's stored
            if (String(ssid) != saved_ssid || String(password) != saved_password) {
                saveCredentials(ssid, password);
            }
            
            reconnect_attempts = 0;
            return true;
        } else {
            state = WIFI_FAILED;
            Serial.println("WiFi connection failed!");
            return false;
        }
    }
    
    /**
     * Check if WiFi is currently connected
     */
    bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
    }
    
    /**
     * Attempt to reconnect to WiFi
     * Uses exponential backoff to avoid overwhelming the network
     */
    bool reconnect() {
        // Check if we're already connected
        if (isConnected()) {
            state = WIFI_CONNECTED;
            return true;
        }
        
        // Implement exponential backoff
        unsigned long current_time = millis();
        unsigned long backoff_delay = WIFI_RECONNECT_BACKOFF_MS * (1 << reconnect_attempts);
        backoff_delay = min(backoff_delay, 16000UL); // Max 16 second delay
        
        if (current_time - last_reconnect_attempt < backoff_delay) {
            return false; // Too soon to retry
        }
        
        last_reconnect_attempt = current_time;
        state = WIFI_RECONNECTING;
        
        Serial.println("Attempting WiFi reconnection...");
        Serial.print("  Attempt: ");
        Serial.print(reconnect_attempts + 1);
        Serial.print("/");
        Serial.println(WIFI_RECONNECT_MAX_ATTEMPTS);
        
        WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        
        int timeout = WIFI_RECONNECT_TIMEOUT;
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print("*");
            timeout--;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            state = WIFI_CONNECTED;
            reconnect_attempts = 0;
            Serial.println("WiFi reconnected!");
            Serial.print("  RSSI: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            return true;
        } else {
            reconnect_attempts++;
            if (reconnect_attempts >= WIFI_RECONNECT_MAX_ATTEMPTS) {
                state = WIFI_FAILED;
                Serial.println("Max reconnection attempts reached");
            }
            return false;
        }
    }
    
    /**
     * Get current WiFi RSSI (signal strength)
     * Returns -100 if not connected
     */
    long getRSSI() {
        if (isConnected()) {
            return WiFi.RSSI();
        }
        return -100;
    }
    
    /**
     * Get current WiFi state
     */
    WiFiState getState() {
        return state;
    }
    
    /**
     * Save WiFi credentials to NVS
     */
    void saveCredentials(const char* ssid, const char* password) {
        if (!preferences_initialized) {
            Serial.println("Preferences not initialized, cannot save credentials");
            return;
        }
        
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        saved_ssid = ssid;
        saved_password = password;
        
        Serial.println("WiFi credentials saved to NVS");
    }
    
    /**
     * Load WiFi credentials from NVS
     * Returns true if credentials were found, false otherwise
     */
    bool loadCredentials() {
        if (!preferences_initialized) {
            Serial.println("Preferences not initialized, cannot load credentials");
            return false;
        }
        
        saved_ssid = preferences.getString("ssid", "");
        saved_password = preferences.getString("password", "");
        
        // Check if credentials are valid (not empty)
        if (saved_ssid.length() > 0 && saved_password.length() > 0) {
            Serial.print("Loaded SSID: ");
            Serial.println(saved_ssid);
            return true;
        }
        
        return false;
    }
    
    /**
     * Reset stored WiFi credentials
     */
    void clearCredentials() {
        if (!preferences_initialized) {
            return;
        }
        
        preferences.clear();
        saved_ssid = "";
        saved_password = "";
        Serial.println("WiFi credentials cleared from NVS");
    }
    
    /**
     * Get saved SSID
     */
    String getSSID() {
        return saved_ssid;
    }

private:
    Preferences preferences;
    String saved_ssid;
    String saved_password;
    WiFiState state;
    int reconnect_attempts;
    unsigned long last_reconnect_attempt;
    bool preferences_initialized;
};

#endif // WIFI_MANAGER_H
