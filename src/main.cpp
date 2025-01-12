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
void pride() {
    static uint16_t frame = 0; // Keeps track of the animation frame
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            // Calculate the color hue based on position and frame
            uint8_t hue = (x * 10 + y * 10 + frame) % 255;
            leds[getVirtualIndex(x, y)] = CHSV(hue, 255, 255); // Full saturation and brightness
        }
    }

    // Show the updated LEDs
    FastLED.show();

    // Increment the frame for the next iteration
    frame += speed / 10; // Adjust speed dynamically
    if (frame >= 255) {
        frame = 0; // Reset frame to loop the animation
    }

    // Delay to control the animation speed
    vTaskDelay(speed / portTICK_PERIOD_MS);
}

void animationTask(void *parameter) {
    while (true) {
        switch (currentAnimationId) {
            case 0:
                FastLED.clear();
                FastLED.show();
                vTaskDelay(100 / portTICK_PERIOD_MS);
                break;
            case 1:
                pride();
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

    server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("b")) {
            brightness = request->getParam("b")->value().toInt();
            FastLED.setBrightness(brightness);
            request->send(200, "text/plain", "Brightness updated");
        } else {
            request->send(400, "text/plain", "Missing parameter");
        }
    });

    server.on("/speed", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("b")) {
            speed = request->getParam("b")->value().toInt();
            request->send(200, "text/plain", "Speed updated");
        } else {
            request->send(400, "text/plain", "Missing parameter");
        }
    });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("p")) {
            animation = request->getParam("p")->value().toInt();
            request->send(200, "text/plain", "Animation updated");
        } else {
            request->send(400, "text/plain", "Missing parameter");
        }
    });

    server.begin();
    Serial.println("Server started at: http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(animationTask, "AnimationTask", 10000, nullptr, 1, nullptr, 0);
}

void loop() {
    // Keeping empty
}
