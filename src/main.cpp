#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
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
int animation = 0;   // Default animation ID
int speed = 10;      // Default speed

std::vector<std::vector<std::vector<uint8_t>>> image;
std::vector<std::vector<std::vector<uint8_t>>> frames;
int numFrames = 0;
int currentAnimationId = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

//----------------------------------------------------------------
void sendLog(const String &log) {
    ws.textAll(log); // Broadcast log message to all connected WebSocket clients
    Serial.println(log); // Also print to Serial console for debugging
}

//----------------------------------------------------------------
int getVirtualIndex(int x, int y) {
    int panel = x / 16; // Determine which panel (0, 1, or 2)
    int localX = x % 16; // Local x-coordinate within the panel
    int localIndex;
    if (y % 2 == 0) {
        localIndex = y * 16 + localX; //odd rows
    } else {
        localIndex = y * 16 + (15 - localX); //even rows
    }
    if (panel == 0) {
        return localIndex; // Panel 1 (connected to pin 2)
    } else if (panel == 1) {
        return localIndex + 160; // Panel 2 (connected to pin 4)
    } else {
        return localIndex + 320; // Panel 3 (connected to pin 5)
    }
}

bool loadAnimationFrames(const char *fileName, const char *key, std::vector<std::vector<std::vector<uint8_t>>> &animationFrames, int &frameCount) {
    File file = LittleFS.open(fileName, "r");
    if (!file) {
        sendLog(String("Failed to open file: ") + fileName);
        return false;
    }

    DynamicJsonDocument doc(30000); // Adjust size based on file size
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
            animationFrames[i][y].resize(row.size() * 3); // Multiply by 3 for RGB triplets
            for (size_t x = 0; x < row.size(); x++) {
                JsonArray pixel = row[x];
                animationFrames[i][y][x * 3] = pixel[0].as<uint8_t>();     // Red
                animationFrames[i][y][x * 3 + 1] = pixel[1].as<uint8_t>(); // Green
                animationFrames[i][y][x * 3 + 2] = pixel[2].as<uint8_t>(); // Blue
            }
        }
    }

    sendLog(String("Loaded ") + frameCount + String(" frames from: ") + fileName);
    return true;
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
    FastLED.show();
    frame += speed / 10;
    if (frame >= 255) {
        frame = 0;
    }
    vTaskDelay(speed / portTICK_PERIOD_MS);
}

void vip() {
    static int offset = 0; // Offset for rotating the image
    int imgWidth = image[0][0].size() / 3; // Divide by 3 to get actual width
    int imgHeight = image[0].size();

    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int imgX = (x + offset) % imgWidth; // Rotate horizontally
            int imgY = y % imgHeight;
            int baseIndex = imgX * 3; // Calculate RGB index
            leds[getVirtualIndex(x, y)] = CRGB(
                image[0][imgY][baseIndex],     // Red
                image[0][imgY][baseIndex + 1], // Green
                image[0][imgY][baseIndex + 2]  // Blue
            );
        }
    }

    FastLED.show();
    offset = (offset + 1) % imgWidth; // Increment offset for rotation
    vTaskDelay(speed * 12 / portTICK_PERIOD_MS);
}


void edm() {
    static int frameIndex = 0; // Keeps track of the current frame
    int imgWidth = 10;        // Original width of the EDM frames
    int imgHeight = 10;       // Original height of the EDM frames

    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < VIRT_WIDTH; x++) {
            int canvasX = x % 12; // Determine position within a single canvas (12x10)
            int imgX = (canvasX > 0 && canvasX <= imgWidth) ? canvasX - 1 : -1; // Map to 10x10 if within bounds, otherwise -1 for padding
            int imgY = y % imgHeight;

            if (imgX == -1) {
                // Black padding for columns outside the 10x10 image
                leds[getVirtualIndex(x, y)] = CRGB(0, 0, 0);
            } else {
                // Map pixel from the frame to the LED
                int baseIndex = imgX * 3; // Calculate RGB index
                leds[getVirtualIndex(x, y)] = CRGB(
                    frames[frameIndex][imgY][baseIndex],     // Red
                    frames[frameIndex][imgY][baseIndex + 1], // Green
                    frames[frameIndex][imgY][baseIndex + 2]  // Blue
                );
            }
        }
    }

    FastLED.show();
    frameIndex = (frameIndex + 1) % numFrames; // Move to the next frame
    vTaskDelay(speed / portTICK_PERIOD_MS);    // Control animation speed
}



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
        sendLog("LittleFS mounted.");
    } else {
        sendLog("LittleFS mount failed.");
    }

    // Load VIP animation
    if (!loadAnimationFrames("/vip.json", "vip_img", image, numFrames)) {
        sendLog("Failed to load VIP animation.");
    }

    // Load EDM animation
    if (!loadAnimationFrames("/edm.json", "edm_animation", frames, numFrames)) {
        sendLog("Failed to load EDM animation.");
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
    sendLog("Connected to Wi-Fi");
    sendLog("IP Address: " + WiFi.localIP().toString());

    // Set up WebSocket
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            sendLog("WebSocket client connected.");
        } else if (type == WS_EVT_DISCONNECT) {
            sendLog("WebSocket client disconnected.");
        }
    });
    server.addHandler(&ws);

    // Set up routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/main.html", "text/html");
    });

    server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("b")) {
            brightness = request->getParam("b")->value().toInt();
            FastLED.setBrightness(brightness);
            request->send(200, "text/plain", "Brightness set to: " + String(brightness));
            sendLog("Brightness set to: " + String(brightness));
        }}       
    );

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("p")) {
            animation = request->getParam("p")->value().toInt();
            currentAnimationId = animation;
            request->send(200, "text/plain", "Animation set to ID: " + String(animation));
            sendLog("Animation set to ID: " + String(animation));
        }}
    );

    server.begin();
    sendLog("Server started at: http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(animationTask, "AnimationTask", 10000, nullptr, 1, nullptr, 0);
}

void loop() {
    ws.cleanupClients();
}