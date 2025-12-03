/*
 * Common Type Definitions for Fox Energy Power Monitor Display
 * Contains data structures used across the application
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// =========================================================================
// ===                        DATA STRUCTURES                            ===
// =========================================================================

/**
 * Power measurement data from Fox Energy API
 */
struct PowerData {
    float voltage;        // Voltage in V
    float current;        // Current in A
    float power_active;   // Active power in W
    
    // Constructor with default values
    PowerData() : voltage(0.0f), current(0.0f), power_active(0.0f) {}
    
    PowerData(float v, float c, float p) 
        : voltage(v), current(c), power_active(p) {}
};

/**
 * Status bar display data
 */
struct StatusData {
    float internal_temp;  // ESP32 internal temperature in °C
    long rssi;            // WiFi signal strength in dBm
    String time_str;      // Full time string "HH:MM:SS"
    String hours;         // Hours component "HH"
    String minutes;       // Minutes component "MM"
    String seconds;       // Seconds component "SS"
    
    // Constructor with default values
    StatusData() : internal_temp(0.0f), rssi(-100), 
                   time_str("--:--:--"), hours("--"), 
                   minutes("--"), seconds("--") {}
};

/**
 * Double buffering modes
 */
enum BufferMode {
    DIRECT,    // No buffering - direct drawing (fallback mode)
    TILE,      // Tile-based buffering (memory efficient)
    FULL       // Full screen buffering (not recommended for ESP32-C3)
};

/**
 * WiFi connection state
 */
enum WiFiState {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_RECONNECTING,
    WIFI_FAILED
};

/**
 * Dirty region tracking for optimized rendering
 */
struct DirtyRegion {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    bool is_dirty;
    
    DirtyRegion() : x(0), y(0), width(0), height(0), is_dirty(false) {}
    
    void mark(int16_t x_, int16_t y_, int16_t w_, int16_t h_) {
        x = x_;
        y = y_;
        width = w_;
        height = h_;
        is_dirty = true;
    }
    
    void clear() {
        is_dirty = false;
    }
};

#endif // TYPES_H
