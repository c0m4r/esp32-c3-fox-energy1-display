/*
 * Data Fetcher for Fox Energy API
 * Handles HTTP requests and JSON parsing with error recovery
 */

#ifndef DATA_FETCHER_H
#define DATA_FETCHER_H

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "types.h"

class DataFetcher {
public:
    DataFetcher() : 
        last_fetch_time(0),
        last_fetch_successful(false),
        consecutive_failures(0) {}
    
    /**
     * Fetch power data from the API
     * Returns true if data was successfully fetched and parsed
     */
    bool fetchPowerData(const char* url, PowerData& data) {
        Serial.print("Fetching data from: ");
        Serial.println(url);
        
        // Begin HTTP connection
        http.begin(url);
        http.setTimeout(HTTP_TIMEOUT_MS);
        
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            http.end(); // Release resources early
            
            if (parseJSON(payload, data)) {
                last_fetch_time = millis();
                last_fetch_successful = true;
                consecutive_failures = 0;
                
                Serial.printf("Data received: V=%.1f, C=%.2f, P=%.1f\n", 
                             data.voltage, data.current, data.power_active);
                return true;
            } else {
                last_fetch_successful = false;
                consecutive_failures++;
                Serial.println("JSON parsing failed");
                return false;
            }
        } else if (httpCode > 0) {
            // HTTP error
            http.end();
            last_fetch_successful = false;
            consecutive_failures++;
            Serial.printf("HTTP Error: %d - %s\n", 
                         httpCode, HTTPClient::errorToString(httpCode).c_str());
            return false;
        } else {
            // Connection error
            http.end();
            last_fetch_successful = false;
            consecutive_failures++;
            Serial.printf("HTTP Connection Failed: %s\n", 
                         http.errorToString(httpCode).c_str());
            return false;
        }
    }
    
    /**
     * Check if last fetch was successful
     */
    bool isDataValid() {
        return last_fetch_successful;
    }
    
    /**
     * Get time of last successful fetch
     */
    unsigned long getLastFetchTime() {
        return last_fetch_time;
    }
    
    /**
     * Get number of consecutive failures
     */
    int getConsecutiveFailures() {
        return consecutive_failures;
    }
    
    /**
     * Reset failure counter (e.g., after WiFi reconnection)
     */
    void resetFailures() {
        consecutive_failures = 0;
    }

private:
    HTTPClient http;
    unsigned long last_fetch_time;
    bool last_fetch_successful;
    int consecutive_failures;
    
    /**
     * Parse JSON payload and extract power data
     * Returns true if parsing was successful
     */
    bool parseJSON(const String& payload, PowerData& data) {
        // Use StaticJsonDocument for better performance
        StaticJsonDocument<512> doc;
        
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            Serial.print("JSON Parse Error: ");
            Serial.println(error.c_str());
            return false;
        }
        
        // Extract values with type checking
        if (!doc.containsKey("voltage") || 
            !doc.containsKey("current") || 
            !doc.containsKey("power_active")) {
            Serial.println("JSON missing required fields");
            return false;
        }
        
        data.voltage = doc["voltage"].as<float>();
        data.current = doc["current"].as<float>();
        data.power_active = doc["power_active"].as<float>();
        
        // Validate data ranges (sanity checks)
        if (data.voltage < 0 || data.voltage > 500) {
            Serial.println("Invalid voltage value");
            return false;
        }
        
        if (data.current < 0 || data.current > 100) {
            Serial.println("Invalid current value");
            return false;
        }
        
        if (data.power_active < 0 || data.power_active > 50000) {
            Serial.println("Invalid power value");
            return false;
        }
        
        return true;
    }
};

#endif // DATA_FETCHER_H
