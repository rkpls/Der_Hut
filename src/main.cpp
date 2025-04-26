#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "font5x7.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

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
CRGB ledsBuffer[VIRT_NUM_LEDS];  // Buffer für Double Buffering
int brightness = 10;
int speed = 10;
volatile int currentAnimation = 0;
volatile bool interruptRequested = false;
bool webAccessed = false;
bool showText = false;
String displayText = "";

// Datenstrukturen für Bilder und Animationen
std::vector<std::vector<std::vector<uint8_t>>> image;
std::vector<std::vector<std::vector<uint8_t>>> frames;
int frameCount = 0;

// Webserver und Websocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Timer Handles
TimerHandle_t animationTimer;
// Forward declarations
void animationTimerCallback(TimerHandle_t xTimer);
void updateLEDs();
BaseType_t xTimerIsRunning(TimerHandle_t xTimer); // Deklaration von xTimerIsRunning

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

void showImage(const char *file, const char *key, int activeAnim) {
    static int offset = 0;
    if (currentAnimation != activeAnim) return;
    if (image.empty()) return; // Sicherstellen, dass das Bild geladen ist

    int w = image[0][0].size() / 3;
    int h = image[0].size();
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int xx = (x + offset) % w;
            int yy = y % h;
            int base = xx * 3;
            ledsBuffer[getVirtualIndex(x, y)] = CRGB(
                image[0][yy][base], image[0][yy][base + 1], image[0][yy][base + 2]);
        }
    }
    offset = (offset + 1) % w;
}

void edm(int activeAnim) {
    static int i = 0;
    if (currentAnimation != activeAnim) return;
    if (frames.empty()) return; // Sicherstellen, dass die Frames geladen sind.

    int w = frames[0][0].size() / 3;
    int h = frames[0].size();
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
                    frames[i][yy][base], frames[i][yy][base + 1], frames[i][yy][base + 2]);
            }
        }
    }
    i = (i + 1) % frameCount;
}

void displayIP() {
    scrollText(WiFi.localIP().toString(), -99, CRGB::White);
}

// Timer Callback Funktion
void animationTimerCallback(TimerHandle_t xTimer) {
    if (showText) {
        scrollText(displayText, -1);
    }
    else {
        switch (currentAnimation) {
            case 0:
                fill_solid(ledsBuffer, VIRT_NUM_LEDS, CRGB::Black);
                break;
            case 1:
                pride(currentAnimation);
                break;
            case 2:
                showImage("/vip.json", "vip_img", currentAnimation);
                break;
            case 3:
                showImage("/mod.json", "mod_img", currentAnimation);
                break;
            case 4:
                edm(currentAnimation);
                break;
            default:
                fill_solid(ledsBuffer, VIRT_NUM_LEDS, CRGB::Black);
                break;
        }
    }
    updateLEDs(); // Kopiere den Buffer zum Anzeigen
}

// Funktion zum Kopieren des Buffers auf die LEDs und Anzeigen
void updateLEDs() {
    memcpy(leds, ledsBuffer, sizeof(leds)); // Schnelles Kopieren
    FastLED.show();
}

// Animation Task (jetzt hauptsächlich für Timer-Erstellung und Verwaltung)
void animationTask(void *param) {
    // Erstelle den Timer, aber starte ihn noch nicht
    animationTimer = xTimerCreate(
        "animationTimer",      // Name des Timers
        pdMS_TO_TICKS(speed), // Timer-Periode (kann später geändert werden)
        pdTRUE,                 // Auto-Reload
        (void *)0,             // Timer-ID (nicht verwendet)
        animationTimerCallback); // Callback-Funktion

     if (animationTimer == NULL) {
        sendLog("Failed to create animation timer");
        // Handle error appropriately
    }

    while (true) {
        if (!webAccessed) {
            displayIP(); //Zeigt die IP nur an, wenn nicht auf die Webseite zugegriffen wurde.
        }
        else if (showText){
             xTimerChangePeriod(animationTimer, pdMS_TO_TICKS(50), 0); //Text Scroll Speed
             xTimerStart(animationTimer, 0);
        }
        else {
            xTimerChangePeriod(animationTimer, pdMS_TO_TICKS(speed * 4), 0); //Setze die Timer-Periode für die aktuelle Animation
            if (interruptRequested) {
                interruptRequested = false;
                xTimerStop(animationTimer, 0); // Stop timer wenn interrupt
                fill_solid(leds, VIRT_NUM_LEDS, CRGB::Black);
                FastLED.show();
            }
            // Starte den Timer, falls er nicht läuft.  Wichtig: Starte ihn *nur* hier, nicht in den Animationsfunktionen.
            if (xTimerIsRunning(animationTimer) == pdFALSE && currentAnimation != 0) {
                xTimerStart(animationTimer, 0);
            }
            else if (currentAnimation == 0){
                xTimerStop(animationTimer, 0);
                fill_solid(leds, VIRT_NUM_LEDS, CRGB::Black);
                FastLED.show();
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Kleinere Verzögerung
    }
}

// Setup-Funktion
void setup() {
    Serial.begin(115200);
    LittleFS.begin();

    // Lade Animationsdaten beim Start
    if (!loadJSONImage("/edm.json", "edm_animation", frames, frameCount)) {
        sendLog("Failed to load /edm.json");
        // Handle error: z.B. alle Animationen deaktivieren
        frameCount = 0; // Damit edm() nicht auf ungültige Daten zugreift
    }
    //Lade Bilder für Animationen
     if (!loadJSONImage("/vip.json", "vip_img", image, frameCount)) {
        sendLog("Failed to load /vip.json");
    }
    if (!loadJSONImage("/mod.json", "mod_img", image, frameCount)) {
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
            interruptRequested = true;
            r->send(200, "text/plain", "Animation gesetzt: " + String(currentAnimation));
        }
    });

    server.on("/text", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("t")) {
            displayText = r->getParam("t")->value();
            showText = true;
            currentAnimation = -1;
            interruptRequested = true;
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
            xTimerChangePeriod(animationTimer, pdMS_TO_TICKS(speed), 0); // Update Timer speed.
            r->send(200, "text/plain", "Speed: " + String(speed));
        }
    });

    server.on("/ip", HTTP_GET, [](AsyncWebServerRequest *r) {
        displayIP();
        r->send(200, "text/plain", "IP wird angezeigt");
    });

    server.begin();
    sendLog("HTTP Server ready");

    // Starte den Animation Task
    xTaskCreatePinnedToCore(animationTask, "animationTask", 10000, NULL, 1, NULL, 0);
}

void loop() {
    ws.cleanupClients();
}

