#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "config.h" // Wi-Fi credentials
#include <ArduinoJson.h> // For JSON parsing

#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
#define MATRIX_NUM_LEDS 160
#define VIRT_NUM_LEDS 480

const int VIRT_WIDTH = 48;
const int MATRIX_HEIGHT = 10;
const int SEGMENT_WIDTH = 16;

CRGB leds[VIRT_NUM_LEDS];

int brightness = 10; // Default brightness
int animation = 1;   // Default animation ID
int speed = 10;      // Default speed

AsyncWebServer server(80);

void loadSettings() {
    File file = SPIFFS.open("../files/settings.json", "r");
    if (!file) {
        Serial.println("Failed to open settings file");
        return;
    }
    JsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to parse settings file");
        return;
    }

    brightness = doc["brightness"] | 10;
    animation = doc["animation"] | 1;
    speed = doc["speed"] | 10;

    file.close();
}

void saveSettings() {
    JsonDocument doc(256);
    doc["brightness"] = brightness;
    doc["animation"] = animation;
    doc["speed"] = speed;

    File file = SPIFFS.open("../files/settings.json", "w");
    if (!file) {
        Serial.println("Failed to open settings file for writing");
        return;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to settings file");
    }

    file.close();
}

void pride() {
    static uint8_t frame = 0;
    for (int x = 0; x < VIRT_WIDTH; x++) {
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t hue = (x * 10 + y * 10 + frame) % 256;
            int index = y * VIRT_WIDTH + x; // Adjust as needed
            leds[index] = CHSV(hue, 255, brightness);
        }
    }
    frame = (frame + 1) % 256;
    FastLED.show();
    delay(speed);
}

void edm() {
    // Placeholder for EDM animation
}

void vip() {
    static uint8_t frame = 0;
    frame = (frame + 1) % 48; // Wrap the frame offset around after 48
}

void animationTask(void *parameter) {
    while (true) {
        switch (animation) {
            case 0:
                FastLED.clear();
                FastLED.show();
                break;
            case 1:
                pride();
                break;
            case 2:
                vip();
                break;
            case 3:
                edm();
                break;
            default:
                FastLED.clear();
                FastLED.show();
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);

    if (!SPIFFS.begin(true)) {
        Serial.println("An error occurred while mounting SPIFFS");
        return;
    }

    loadSettings();

    FastLED.addLeds<WS2812B, DATA_PIN_1, GRB>(leds, 0, MATRIX_NUM_LEDS);
    FastLED.addLeds<WS2812B, DATA_PIN_2, GRB>(leds, MATRIX_NUM_LEDS, MATRIX_NUM_LEDS);
    FastLED.addLeds<WS2812B, DATA_PIN_3, GRB>(leds, 2 * MATRIX_NUM_LEDS, MATRIX_NUM_LEDS);
    FastLED.setBrightness(brightness);
    FastLED.clear();
    FastLED.show();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
        Serial.print(".");
    }
    Serial.println("Connected to Wi-Fi");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "../files/main.html", "text/html");
    });

    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("brightness", true)) {
            brightness = request->getParam("brightness", true)->value().toInt();
        }
        if (request->hasParam("animation", true)) {
            animation = request->getParam("animation", true)->value().toInt();
        }
        if (request->hasParam("speed", true)) {
            speed = request->getParam("speed", true)->value().toInt();
        }
        saveSettings();
        request->send(200, "text/plain", "Settings updated");
    });

    server.begin();

    xTaskCreatePinnedToCore(animationTask, "AnimationTask", 10000, nullptr, 1, nullptr, 0);
}

void loop() {
    // Keep loop empty
}
