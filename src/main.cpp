#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "font5x7.h"
#include "config.h"

// Pins und LED-Definitionen
#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
#define MATRIX_NUM_LEDS 160
#define VIRT_NUM_LEDS 480
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 10
#define VIRT_WIDTH 48
#define CHAR_WIDTH 5
#define CHAR_HEIGHT 7

// Globale Variablen
CRGB leds[VIRT_NUM_LEDS];         // Haupt-LED-Array
CRGB ledsBuffer[VIRT_NUM_LEDS];    // Buffer für Double Buffering
int brightness = 10;
int speed = 10;
volatile int currentAnimation = 0;
bool webAccessed = false;
bool showText = false;
String displayText = "";
unsigned long lastUpdateTime = 0;
unsigned long animationInterval = 40; // Startwert, wird später angepasst

// Datenstrukturen für Bilder und Animationen
std::vector<std::vector<std::vector<uint8_t>>> edmFrames;
int edmFrameCount = 0;
std::vector<std::vector<std::vector<uint8_t>>> vipImage;
std::vector<std::vector<std::vector<uint8_t>>> modImage;

// Webserver und Websocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Forward declarations
void updateLEDs();

// Hilfsfunktionen
void sendLog(String msg) {
    Serial.println(msg);
    ws.textAll(msg);
}

int getVirtualIndex(int x, int y) {
    int panel = x / 16;
    int localX = x % 16;
    int localIndex = (y % 2 == 0) ? y * 16 + localX : y * 16 + (15 - localX);
    return localIndex + panel * MATRIX_NUM_LEDS;
}

// Funktion zum Laden von JSON-Bilddaten
bool loadJSONImage(const char *file, const char *key, std::vector<std::vector<std::vector<uint8_t>>> &out, int &count) {
    File f = LittleFS.open(file, "r");
    if (!f) {
        sendLog("File not found: " + String(file));
        return false;
    }
    DynamicJsonDocument doc(35000); // Größe des Dokuments anpassen!
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        sendLog("Parse error: " + String(err.c_str()));
        return false;
    }

    JsonArray arr = doc[key].as<JsonArray>();
    if (arr.isNull()) {
        sendLog("Key not found: " + String(key));
        return false;
    }

    out.clear();
    out.shrink_to_fit();
    out.resize(arr.size());
    count = arr.size();
    for (size_t i = 0; i < arr.size(); i++) {
        JsonArray frame = arr[i];
        out[i].resize(frame.size());
        for (size_t y = 0; y < frame.size(); y++) {
            JsonArray row = frame[y];
            out[i][y].resize(row.size() * 3);
            for (size_t x = 0; x < row.size(); x++) {
                JsonArray px = row[x];
                out[i][y][x * 3 + 0] = px[0];
                out[i][y][x * 3 + 1] = px[1];
                out[i][y][x * 3 + 2] = px[2];
            }
        }
    }
    sendLog("Loaded " + String(count) + " frames from " + file);
    return true;
}

// Animationsfunktionen (schreiben jetzt in den Buffer)
void pride(int activeAnim) {
    static uint8_t t = 0;
    if (currentAnimation != activeAnim) return;
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            ledsBuffer[getVirtualIndex(x, y)] = CHSV((x * 5 + y * 10 + t) % 255, 255, 255);
        }
    }
    t = (t + 1) % 255;
}

void scrollText(String txt, int activeAnim, CRGB col = CRGB::White) {
    static int offset = VIRT_WIDTH;
    if (currentAnimation != activeAnim && !showText) return;

    fill_solid(ledsBuffer, VIRT_NUM_LEDS, CRGB::Black); // Buffer löschen

    for (int i = 0; i < txt.length(); i++) {
        int x0 = offset + i * (CHAR_WIDTH + 1);
        if (x0 >= -CHAR_WIDTH && x0 < VIRT_WIDTH) {
            const uint8_t *bitmap = font5x7[txt[i] - 32];
            for (int x = 0; x < CHAR_WIDTH; x++) {
                for (int y = 0; y < CHAR_HEIGHT; y++) {
                    if (bitmap[x] & (1 << y)) {
                        int vx = x0 + x;
                        int vy = y + 1;
                        if (vx >= 0 && vx < VIRT_WIDTH && vy < MATRIX_HEIGHT)
                            ledsBuffer[getVirtualIndex(VIRT_WIDTH - 1 - vx, vy)] = col;
                    }
                }
            }
        }
    }
    offset--;
    if (offset < - (int)(txt.length() * (CHAR_WIDTH + 1))){
        offset = VIRT_WIDTH;
        showText = false; // Text einmal anzeigen
    }
}

void showImage(const std::vector<std::vector<std::vector<uint8_t>>> &imgData, int activeAnim) {
    static int offset = 0;
    if (currentAnimation != activeAnim) return;
    if (imgData.empty()) return; // Sicherstellen, dass das Bild geladen ist

    int w = imgData[0][0].size() / 3;
    int h = imgData[0].size();
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int xx = (x + offset) % w;
            int yy = y % h;
            int base = xx * 3;
            ledsBuffer[getVirtualIndex(x, y)] = CRGB(
                imgData[0][yy][base], imgData[0][yy][base + 1], imgData[0][yy][base + 2]);
        }
    }
    offset = (offset + 1) % w;
}

void edm(int activeAnim) {
    static int i = 0;
    if (currentAnimation != activeAnim) return;
    if (edmFrames.empty()) return; // Sicherstellen, dass die Frames geladen sind.

    int w = edmFrames[0][0].size() / 3;
    int h = edmFrames[0].size();
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int cx = x % (w + 2);
            int xx = (cx > 0 && cx <= w) ? cx - 1 : -1;
            int yy = y % h;
            if (xx == -1)
                ledsBuffer[getVirtualIndex(x, y)] = CRGB::Black;
            else {
                int base = xx * 3;
                ledsBuffer[getVirtualIndex(x, y)] = CRGB(
                    edmFrames[i][yy][base], edmFrames[i][yy][base + 1], edmFrames[i][yy][base + 2]);
            }
        }
    }
    i = (i + 1) % edmFrameCount;
}

void displayIP() {
    scrollText(WiFi.localIP().toString(), -99, CRGB::White);
}

// Funktion zum Kopieren des Buffers auf die LEDs und Anzeigen
void updateLEDs() {
    memcpy(leds, ledsBuffer, sizeof(leds)); // Schnelles Kopieren
    FastLED.show();
}

void handleAnimation() {
    if (showText) {
        scrollText(displayText, -1);
        animationInterval = 50; // Text Scroll Speed
    } else {
        switch (currentAnimation) {
            case 0:
                fill_solid(ledsBuffer, VIRT_NUM_LEDS, CRGB::Black);
                animationInterval = speed * 4;
                break;
            case 1:
                pride(currentAnimation);
                animationInterval = speed * 4;
                break;
            case 2:
                showImage(vipImage, currentAnimation);
                animationInterval = speed * 10;
                break;
            case 3:
                showImage(modImage, currentAnimation);
                animationInterval = speed * 10;
                break;
            case 4:
                edm(currentAnimation);
                animationInterval = speed * 6;
                break;
            default:
                fill_solid(ledsBuffer, VIRT_NUM_LEDS, CRGB::Black);
                animationInterval = speed * 4;
                break;
        }
    }
    updateLEDs();
}

void setup() {
    Serial.begin(115200);
    LittleFS.begin();

    // Lade Animationsdaten beim Start
    int dummyCount = 0;
    if (!loadJSONImage("/edm.json", "edm_animation", edmFrames, edmFrameCount)) {
        sendLog("Failed to load /edm.json");
        edmFrameCount = 0;
    }
    if (!loadJSONImage("/vip.json", "vip_img", vipImage, dummyCount)) {
        sendLog("Failed to load /vip.json");
    }
    if (!loadJSONImage("/mod.json", "mod_img", modImage, dummyCount)) {
        sendLog("Failed to load /mod.json");
    }

    FastLED.addLeds<WS2812B, DATA_PIN_1, GRB>(leds, 0, MATRIX_NUM_LEDS);
    FastLED.addLeds<WS2812B, DATA_PIN_2, GRB>(leds, MATRIX_NUM_LEDS, MATRIX_NUM_LEDS);
    FastLED.addLeds<WS2812B, DATA_PIN_3, GRB>(leds, 2 * MATRIX_NUM_LEDS, MATRIX_NUM_LEDS);
    FastLED.setBrightness(brightness);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
        Serial.print(".");
    }
    sendLog("Connected: " + WiFi.localIP().toString());

    ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *a, uint8_t *d, size_t l) {
        if (t == WS_EVT_CONNECT)
            sendLog("WS connected");
        else if (t == WS_EVT_DISCONNECT)
            sendLog("WS disconnected");
    });
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        webAccessed = true;
        req->send(LittleFS, "/main.html", "text/html");
    });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("p")) {
            currentAnimation = r->getParam("p")->value().toInt();
            showText = false;
            r->send(200, "text/plain", "Animation gesetzt: " + String(currentAnimation));
        }
    });

    server.on("/text", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("t")) {
            displayText = r->getParam("t")->value();
            showText = true;
            currentAnimation = -1;
            r->send(200, "text/plain", "Text wird angezeigt: " + displayText);
        }
    });

    server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("b")) {
            brightness = r->getParam("b")->value().toInt();
            FastLED.setBrightness(brightness);
            r->send(200, "text/plain", "Brightness: " + String(brightness));
        }
    });

    server.on("/speed", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("s")) {
            speed = r->getParam("s")->value().toInt();
            r->send(200, "text/plain", "Speed: " + String(speed));
        }
    });

    server.on("/ip", HTTP_GET, [](AsyncWebServerRequest *r) {
        displayIP();
        r->send(200, "text/plain", "IP wird angezeigt");
    });

    server.begin();
    sendLog("HTTP Server ready");
}

void loop() {
    ws.cleanupClients();

    if (!webAccessed) {
        sendLog("Display IP");
        displayIP();
        delay(5000); // Zeige die IP für 5 Sekunden
    } else {
        unsigned long currentTime = millis();
        if (currentTime - lastUpdateTime >= animationInterval) {
            lastUpdateTime = currentTime;
            handleAnimation();
        }
    }
}