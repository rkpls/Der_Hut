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

bool loadAnimationFromJson(const char *filePath) {
    frames.clear();
    numFrames = 0;
    // Open the file
    File file = LittleFS.open(filePath, "r");
    if (!file) {
        Serial.println("Failed to open JSON file!");
        return false;
    }
    // Allocate a buffer for the file
    size_t fileSize = file.size();
    std::unique_ptr<char[]> jsonBuffer(new char[fileSize + 1]);
    file.readBytes(jsonBuffer.get(), fileSize);
    jsonBuffer[fileSize] = '\0';
    file.close();
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, jsonBuffer.get());
    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return false;
    }
    // Extract frames
    JsonArray jsonFrames = doc["frames"].as<JsonArray>();
    for (JsonArray frame : jsonFrames) {
        std::vector<std::vector<uint8_t>> frameData;
        for (JsonArray row : frame) {
            std::vector<uint8_t> rowData;
            for (uint8_t value : row) {
                rowData.push_back(value);
            }
            frameData.push_back(rowData);
        }
        frames.push_back(frameData);
    }

    numFrames = frames.size();
    Serial.print("Loaded ");
    Serial.print(numFrames);
    Serial.println(" frames from JSON.");

    return true;
}

//----------------------------------------------------------------
void pride() {
    static uint8_t frame = 0;
    while (currentAnimationId == 1) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            for (int y = 0; y < MATRIX_HEIGHT; y++) {
                uint8_t hue = (x * 10 + y * 10 + frame) % 256;
                int index = y * VIRT_WIDTH + x;
                leds[index] = CHSV(hue, 255, brightness);
            }
        }
        frame = (frame + 1) % 256;
        FastLED.show();
        vTaskDelay(speed * 12 / portTICK_PERIOD_MS);
    }
}

void vip() {
    if (!loadAnimationFromJson("/vip.json")) {
        Serial.println("Failed to load VIP animation!");
        return;
    }
    static int frame = 0;
    while (currentAnimationId == 2) {
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            for (int x = 0; x < MATRIX_WIDTH; x++) {
                int localX = (x + frame) % 16;
                int segment = (x + frame) / 16 % 3;
                int index = getVirtualIndex(x, y);
                leds[index] = CRGB(
                    image[y][localX][0], 
                    image[y][localX][1], 
                    image[y][localX][2]
                );
            }
        }
        FastLED.show();
        frame = (frame + 1) % MATRIX_WIDTH; // Wrap around the full width
        vTaskDelay(speed * 12 / portTICK_PERIOD_MS);
    }
}

void edm() {
    if (!loadAnimationFromJson("/edm.json")) {
        Serial.println("Failed to load EDM animation!");
        return;
    }
    int frameIndex = 0;
    while (currentAnimationId == 3) {
        const std::vector<std::vector<uint8_t>> &frame = frames[frameIndex];
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            for (int x = 0; x < MATRIX_WIDTH; x++) {
                int segment = x / 16;
                int localX = x % 16;
                int r = frame[y][localX * 3 + 0];
                int g = frame[y][localX * 3 + 1];
                int b = frame[y][localX * 3 + 2];
                leds[y * MATRIX_WIDTH + x] = CRGB(r, g, b);
            }
        }
        FastLED.show();
        frameIndex = (frameIndex + 1) % numFrames;
        vTaskDelay(speed * 2 / portTICK_PERIOD_MS);
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
