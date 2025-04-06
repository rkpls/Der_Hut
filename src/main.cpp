#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include "font5x7.h"
#include "config.h"
#include <ArduinoJson.h>

#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
#define MATRIX_NUM_LEDS 160
#define VIRT_NUM_LEDS 480
#define font font5x7

const int VIRT_WIDTH = 48;
const int MATRIX_WIDTH = 16;
const int MATRIX_HEIGHT = 10;
const int CHAR_WIDTH = 5;
const int CHAR_HEIGHT = 7;

CRGB leds[VIRT_NUM_LEDS];
int brightness = 10;
int animation = 0;
int speed = 10;
bool webPageAccessed = false;
bool showTextNow = false;
String liveText = "";

std::vector<std::vector<std::vector<uint8_t>>> image;
std::vector<std::vector<std::vector<uint8_t>>> frames;
int numFrames = 0;
int currentAnimationId = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void sendLog(const String &log) {
    ws.textAll(log);
    Serial.println(log);
}

int getVirtualIndex(int x, int y) {
    int panel = x / 16;
    int localX = x % 16;
    int localIndex;
    if (y % 2 == 0) {
        localIndex = y * 16 + localX;
    } else {
        localIndex = y * 16 + (15 - localX);
    }
    if (panel == 0) return localIndex;
    else if (panel == 1) return localIndex + 160;
    else return localIndex + 320;
}

bool loadAnimationFrames(const char *fileName, const char *key, std::vector<std::vector<std::vector<uint8_t>>> &animationFrames, int &frameCount) {
    File file = LittleFS.open(fileName, "r");
    if (!file) {
        sendLog(String("Failed to open file: ") + fileName);
        return false;
    }
    DynamicJsonDocument doc(30000);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        sendLog(String("Failed to parse JSON file: ") + fileName);
        return false;
    }
    JsonArray jsonFrames = doc[key].as<JsonArray>();
    if (jsonFrames.isNull()) {
        sendLog(String("Key '") + key + "' not found in file: " + fileName);
        return false;
    }
    animationFrames.resize(jsonFrames.size());
    frameCount = jsonFrames.size();
    for (size_t i = 0; i < jsonFrames.size(); i++) {
        JsonArray frame = jsonFrames[i];
        animationFrames[i].resize(frame.size());
        for (size_t y = 0; y < frame.size(); y++) {
            JsonArray row = frame[y];
            animationFrames[i][y].resize(row.size() * 3);
            for (size_t x = 0; x < row.size(); x++) {
                JsonArray pixel = row[x];
                animationFrames[i][y][x * 3] = pixel[0].as<uint8_t>();
                animationFrames[i][y][x * 3 + 1] = pixel[1].as<uint8_t>();
                animationFrames[i][y][x * 3 + 2] = pixel[2].as<uint8_t>();
            }
        }
    }
    sendLog(String("Loaded ") + frameCount + String(" frames from: ") + fileName);
    return true;
}

void pride() {
    static uint16_t frame = 0;
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            uint8_t hue = (x * 10 + y * 10 + frame) % 255;
            leds[getVirtualIndex(x, y)] = CHSV(hue, 255, 255);
        }
    }
    FastLED.show();
    frame += speed / 10;
    if (frame >= 255) frame = 0;
    vTaskDelay(speed / portTICK_PERIOD_MS);
}

void icon12x10(int id) {
    static int offset = 0;
    static int lastId = -1;
    if (id != lastId) {
        if (id == 2) loadAnimationFrames("/vip.json", "vip_img", image, numFrames);
        else if (id == 3) loadAnimationFrames("/mod_img_12x10.json", "mod_img", image, numFrames);
        else {
            sendLog("Unbekannte ID in icon12x10: " + String(id));
            return;
        }
        lastId = id;
        offset = 0;
    }
    if (image.empty()) return;
    int imgWidth = image[0][0].size() / 3;
    int imgHeight = image[0].size();
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int imgX = (x + offset) % imgWidth;
            int imgY = y % imgHeight;
            int baseIndex = imgX * 3;
            leds[getVirtualIndex(x, y)] = CRGB(
                image[0][imgY][baseIndex],
                image[0][imgY][baseIndex + 1],
                image[0][imgY][baseIndex + 2]);
        }
    }
    FastLED.show();
    offset = (offset + 1) % imgWidth;
    vTaskDelay(speed * 12 / portTICK_PERIOD_MS);
}

void edm() {
    static int frameIndex = 0;
    int imgWidth = 10;
    int imgHeight = 10;
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int canvasX = x % 12;
            int imgX = (canvasX > 0 && canvasX <= imgWidth) ? canvasX - 1 : -1;
            int imgY = y % imgHeight;
            if (imgX == -1) {
                leds[getVirtualIndex(x, y)] = CRGB(0, 0, 0);
            } else {
                int baseIndex = imgX * 3;
                leds[getVirtualIndex(x, y)] = CRGB(
                    frames[frameIndex][imgY][baseIndex],
                    frames[frameIndex][imgY][baseIndex + 1],
                    frames[frameIndex][imgY][baseIndex + 2]);
            }
        }
    }
    FastLED.show();
    frameIndex = (frameIndex + 1) % numFrames;
    vTaskDelay(speed * 4 / portTICK_PERIOD_MS);
}

void scrollText(const String &text, CRGB color = CRGB::White) {
    int textLength = text.length() * (CHAR_WIDTH + 1);
    for (int offset = VIRT_WIDTH; offset > -textLength; offset--) {
        fill_solid(leds, VIRT_NUM_LEDS, CRGB::Black);
        for (int i = 0; i < text.length(); i++) {
            int charX = offset + i * (CHAR_WIDTH + 1);
            if (charX >= -CHAR_WIDTH && charX < VIRT_WIDTH) {
                const uint8_t *bitmap = font5x7[text[i] - 32];
                for (int x = 0; x < CHAR_WIDTH; x++) {
                    for (int y = 0; y < CHAR_HEIGHT; y++) {
                        if (bitmap[x] & (1 << y)) {
                            int vx = charX + x;
                            int vy = y + 1;
                            if (vx >= 0 && vx < VIRT_WIDTH && vy < MATRIX_HEIGHT) {
                                leds[getVirtualIndex(VIRT_WIDTH - 1 - vx, vy)] = color;
                            }
                        }
                    }
                }
            }
        }
        FastLED.show();
        delay(100);
    }
    showTextNow = false; // Reset after showing
}

void displayIPAddress() {
    IPAddress ip = WiFi.localIP();
    String ipText = ip.toString();
    scrollText(ipText);
}

void animationTask(void *parameter) {
    while (true) {
        if (!webPageAccessed) {
            displayIPAddress();
        } else if (showTextNow) {
            scrollText(liveText);
        } else {
            switch (currentAnimationId) {
                case 0: FastLED.clear(); FastLED.show(); break;
                case 1: pride(); break;
                case 2: icon12x10(2); break;
                case 3: icon12x10(3); break;
                case 4: edm(); break;
                default: FastLED.clear(); FastLED.show(); break;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    LittleFS.begin();

    loadAnimationFrames("/edm.json", "edm_animation", frames, numFrames);

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
    sendLog("Connected to Wi-Fi");
    sendLog("IP Address: " + WiFi.localIP().toString());

    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) sendLog("WebSocket client connected.");
        else if (type == WS_EVT_DISCONNECT) sendLog("WebSocket client disconnected.");
    });
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        webPageAccessed = true;
        request->send(LittleFS, "/main.html", "text/html");
    });

    server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("b")) {
            brightness = request->getParam("b")->value().toInt();
            FastLED.setBrightness(brightness);
            request->send(200, "text/plain", "Brightness set to: " + String(brightness));
            sendLog("Brightness set to: " + String(brightness));
        }
    });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("p")) {
            animation = request->getParam("p")->value().toInt();
            currentAnimationId = animation;
            showTextNow = false;
            request->send(200, "text/plain", "Animation set to ID: " + String(animation));
            sendLog("Animation set to ID: " + String(animation));
        }
    });

    server.on("/text", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("t")) {
            liveText = request->getParam("t")->value();
            showTextNow = true;
            request->send(200, "text/plain", "Text wird angezeigt: " + liveText);
        } else {
            request->send(400, "text/plain", "Kein Text angegeben");
        }
    });

    server.on("/ip", HTTP_GET, [](AsyncWebServerRequest *request) {
        displayIPAddress();
        request->send(200, "text/plain", "IP-Adresse wird angezeigt");
    });

    server.begin();
    sendLog("Server started at: http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(animationTask, "AnimationTask", 10000, nullptr, 1, nullptr, 0);
}

void loop() {
    ws.cleanupClients();
}
