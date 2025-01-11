#include <Arduino.h>
#include <FS.h>
#include "config.h" // Include the Wi-Fi credentials
#include "../data/settings.json" // settings to use at restart (brightness, animation ID, speed)
#include "../data/vip.json" // frames: 1, 16x10 px
#include "../data/edm.json" // frames: 40, 16x10 px
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>

// Global variables for animation data
#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
#define MATRIX_NUM_LEDS = 160
#define VIRT_NUM_LEDS 480
const int VIRT_WIDTH = 48;
const int MATRIX_HEIGHT = 10;
const int SEGMENT_WIDTH = 16;

// LED setup

int getVirtualIndex(int x, int y) {
    int panel = x / 16;
    int localX = x % 16;
    int localIndex;
    if (y % 2 == 0) {
        localIndex = y * 16 + localX;
    } else {
        localIndex = y * 16 + (15 - localX);
    }
    if (panel == 0) {
        return localIndex;
    } else if (panel == 1) {
        return localIndex + 160;
    } else {
        return localIndex + 320;
    }
}

void pride() { // "pride" rainbow effect
static uint8_t frame = 0;
  for (int x = 0; x < 48; x++) {
    for (int y = 0; y < 10; y++) {
      uint8_t hue = (x * 10 + y * 10 + frame) % 256;
      int index = getVirtualIndex(x, y);
      leds[index] = CHSV(hue, 255, 255);
    }
  }
  frame = (frame + 1) % 256; // Wrap
  FastLED.show();
  vTaskDelay(speed / portTICK_PERIOD_MS); // Speed value from settings.json 
}

void vip() {
    // display the 16x10 vip.json 3 times on the virtual matrix and wrap around smoothly 
    FastLED.show();
    frame = (frame + 1) % 48; // Wrap the frame offset around after 48
    vTaskDelay(speed*12 / portTICK_PERIOD_MS); // Speed value from settings.json * 12
}

// animation Task 
void animationTask(void *parameter) {
    while (true) {
        //read settings from settings.json and call the animation set
        // animation IDS: 0= Off; 1= pride; 2= VIP; 3= EDM; 4= dart (placeholder); 5= text to image (placeholder)
    }
}

void setup() {
    Serial.begin(9600);

    // Initialize LEDs
    FastLED.addLeds<WS2812B, 2, GRB>(leds, 0, 160);
    FastLED.addLeds<WS2812B, 4, GRB>(leds, 160, 160);
    FastLED.addLeds<WS2812B, 5, GRB>(leds, 320, 160);
    FastLED.clear();
    FastLED.setBrightness(brightness); // get brightness from settings.json
    FastLED.show();

    // Connect to Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
    }
    // Serve the webpage
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String htmlContent = loadHTML("../data/main.html"); // Load the file from
        // ...
    });
    // Handle input write to settings.json
    // send input / update to console log on the page

    server.begin();
}
void loop() {
    // No tasks in loop; everything is event-driven
}
