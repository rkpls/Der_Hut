#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
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
void loadSettings() {
    File file = SPIFFS.open("../files/settings.json", "r");
    if (!file) {
        Serial.println("Failed to open settings file");
        return;
    }
    DynamicJsonDocument doc(256);
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
    DynamicJsonDocument doc(256);
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
//----------------------------------------------------------------
int getVirtualIndex(int x, int y) {
    int panel = x / 16; // Determine which panel (0, 1, or 2)
    int localX = x % 16; // Local x-coordinate within the panel
    int localIndex;
    // Zigzag arrangement: even rows left-to-right, odd rows right-to-left
    if (y % 2 == 0) {
        localIndex = y * 16 + localX;
    } else {
        localIndex = y * 16 + (15 - localX);
    }
    // Map to the appropriate panel
    if (panel == 0) {
        return localIndex; // Panel 1 (connected to pin 2)
    } else if (panel == 1) {
        return localIndex + 160; // Panel 2 (connected to pin 4)
    } else {
        return localIndex + 320; // Panel 3 (connected to pin 5)
    }
}

bool loadAnimationFromJson(const char *filePath) {
    // Clear previous frames
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

    // Parse the JSON
    StaticJsonDocument<4096> doc; // Adjust size as needed for your JSON data
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

void vip() {
    if (!loadAnimationFromJson("/files/vip.json")) {
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
    if (!loadAnimationFromJson("../files/edm.json")) {
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


void dart() {
    // Placeholder for EDM animation
}

void text() {
    // Placeholder for EDM animation
}


//----------------------------------------------------------------
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
            case 4:
                dart();
                break;
            case 5:
                text();
                break;
            default:
                FastLED.clear();
                FastLED.show();
                break;
        }
    }
}
//----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    
    if (!LittleFS.begin()) {
        Serial.println("An error occurred while mounting LittleFS");
        return;
    }

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
