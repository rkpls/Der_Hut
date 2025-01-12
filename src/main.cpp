#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include "config.h" // Wi-Fi credentials
#include <ArduinoJson.h> // For JSON parsing

#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
#define MATRIX_NUM_LEDS 160
#define VIRT_NUM_LEDS 480

const int VIRT_WIDTH = 48;
const int MATRIX_WIDTH = 16;
const int MATRIX_HEIGHT = 10;

CRGB leds[VIRT_NUM_LEDS];

int brightness = 10; // Default brightness
int animation = 1;   // Default animation ID
int speed = 10;      // Default speed

std::vector<std::vector<std::vector<uint8_t>>> image;
std::vector<std::vector<std::vector<uint8_t>>> frames;
int numFrames = 0;
int currentAnimationId = 0;

AsyncWebServer server(80);

//----------------------------------------------------------------
int getVirtualIndex(int x, int y) {
    int panel = x / 16; // Determine which panel (0, 1, or 2)
    int localX = x % 16; // Local x-coordinate within the panel
    int localIndex;
    if (y % 2 == 0) {
        localIndex = y * 16 + localX;
    } else {
        localIndex = y * 16 + (15 - localX);
    }
    if (panel == 0) {
        return localIndex; // Panel 1 (connected to pin 2)
    } else if (panel == 1) {
        return localIndex + 160; // Panel 2 (connected to pin 4)
    } else {
        return localIndex + 320; // Panel 3 (connected to pin 5)
    }
}

//----------------------------------------------------------------
void animationTask(void *parameter) {
    while (true) {
        switch (currentAnimationId) {
            case 0:
                FastLED.clear();
                FastLED.show();
                break;
            default:
                FastLED.clear();
                FastLED.show();
                break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Prevent busy looping
    }
}
//----------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    if (LittleFS.begin()) {
        Serial.println("LittleFS mounted.");
    } else {
        Serial.println("LittleFS mount failed.");
    }

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
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Set up routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/main.html", "text/html");
    });

    server.on("/setAnimation", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("body", true)) {
            String body = request->getParam("body", true)->value();
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, body);
            if (!error) {
                int animationId = doc["animation"];
                // Set the animation ID accordingly
                currentAnimationId = animationId;
                request->send(200, "application/json", "{\"status\":\"success\"}");
            } else {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body found\"}");
        }
    });

    server.on("/setBrightness", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("body", true)) {
            String body = request->getParam("body", true)->value();
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, body);
            if (!error) {
                int brightnessValue = doc["brightness"];
                // Set the brightness accordingly
                brightness = brightnessValue;
                FastLED.setBrightness(brightness);
                request->send(200, "application/json", "{\"status\":\"success\"}");
            } else {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body found\"}");
        }
    });

    server.on("/setSpeed", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("body", true)) {
            String body = request->getParam("body", true)->value();
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, body);
            if (!error) {
                int speedValue = doc["speed"];
                // Set the speed accordingly
                speed = speedValue;
                request->send(200, "application/json", "{\"status\":\"success\"}");
            } else {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body found\"}");
        }
    });

    // Start server
    server.begin();
    Serial.println("Server started at: http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(animationTask, "AnimationTask", 10000, nullptr, 1, nullptr, 0);
}

void loop() {
    // Keeping empty
}
