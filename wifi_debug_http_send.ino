/*
 * WiFi Debug HTTP Send Utility
 * Connects ESP32-C3 to WiFi and periodically sends diagnostic data
 * (RSSI, free heap, uptime) to a specified server via HTTP GET request.
 * Blinks the built-in LED based on the HTTP response.
 * 
 * License: Public Domain (CC0 1.0 Universal)
 * Vibe-coded with Gemini 2.5 Pro Preview 03-25
 * https://github.com/c0m4r/esp32-c3-fox-energy1-display
 */

#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "ssid";
const char* password = "password";
const char* serverUrl = "http://192.168.0.101";

#define LED_BUILTIN 8  // Verify your ESP32-C3's built-in LED pin
#define BLINK_INTERVAL 150  // milliseconds between blinks

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
}

String getDiagnostics() {
  String data = "/?";
  
  // WiFi signal strength
  data += "rssi=";
  data += String(WiFi.RSSI());
  
  // Free heap memory
  data += "&heap=";
  data += String(esp_get_free_heap_size());
  
  // Uptime
  data += "&uptime=";
  data += String(millis() / 1000);
  
  return data;
}

void blinkLed(int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(BLINK_INTERVAL);
    digitalWrite(LED_BUILTIN, LOW);
    delay(BLINK_INTERVAL);
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String fullUrl = serverUrl + getDiagnostics();
    
    Serial.println("Sending: " + fullUrl);
    http.begin(fullUrl);
    
    int httpCode = http.GET();
    String payload = httpCode > 0 ? http.getString() : "Error";

    Serial.print("Response: ");
    Serial.println(httpCode);
    Serial.print("Payload: ");
    Serial.println(payload);

    payload.trim();
    if (payload == "OK") {
      blinkLed(2);
      Serial.println("HTTP OK");
    } else {
      blinkLed(4);
      Serial.println("HTTP NOT OK");
    }
    
    http.end();
  } else {
    Serial.println("WiFi Disconnected!");
    blinkLed(6);
  }

  delay(5000);
}
