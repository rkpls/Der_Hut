#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "config.h" // Include the Wi-Fi credentials
#include "edm.json"
#include <SPIFFS.h> // For file system

// Global variables for animation data
#define NUM_LEDS 480
#define DATA_PIN_1 2
#define DATA_PIN_2 4
#define DATA_PIN_3 5
const int MATRIX_WIDTH = 48;
const int MATRIX_HEIGHT = 10;
const int SEGMENT_WIDTH = 16;
int numFrames = 0;
std::vector<std::vector<std::vector<uint8_t>>> frames;

// LED setup
#define NUM_LEDS 480
TaskHandle_t animationTaskHandle = NULL;
volatile int currentAnimationId = 0; // 0 = Off, 1 = pride

CRGB leds[NUM_LEDS];
uint8_t brightness = 10; // Default brightness

// Create an instance of the server
AsyncWebServer server(80);

// HTML Page
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=0.5">
    <title>The Hat Animation Control</title>
    <style>
        body {
            zoom: 2;
            background-color: #121212;
            color: #e0e0e0;
            font-family: Arial, sans-serif;
            text-align: center;
            padding: 20px;
            max-width: 400px;
            margin: auto;
        }
        h1 {
            margin-bottom: 20px;
            font-size: 24px;
        }
        button {
            background-color: #1f1f1f;
            color: #e0e0e0;
            border: 1px solid #444;
            padding: 10px 20px;
            margin: 10px 0;
            font-size: 18px;
            cursor: pointer;
            border-radius: 5px;
            width: 100%;
        }
        button:hover {
            background-color: #333;
        }
        #brightness-label {
            margin-bottom: 10px;
            font-size: 18px;
        }
        #brightness-slider {
            width: 100%;
        }
        #console {
            background-color: #1e1e1e;
            color: #80cbc4;
            border: 1px solid #444;
            padding: 10px;
            margin-top: 20px;
            height: 200px;
            overflow-y: hidden;
            text-align: left;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <h1>ESP32 Animation Control</h1>
    <div id="brightness-label">Brightness: 10%</div>
    <input id="brightness-slider" type="range" min="0" max="96" value="10" onchange="updateBrightness(this.value)">
    <br>
    <button onclick="sendCommand('0')">Turn Off</button><br>
    <button onclick="sendCommand('1')">Pride</button><br>
    <button onclick="sendCommand('2')">VIP</button><br>
    <button onclick="sendCommand('3')">EDM</button>
    <div id="console"></div>
    <script>
        function sendCommand(preset) {
            fetch('/set?p=' + preset)
                .then(response => response.text())
                .then(data => {
                    const consoleDiv = document.getElementById('console');
                    consoleDiv.innerHTML += `<div>ESP: ${data}</div>`;
                    consoleDiv.scrollTop = consoleDiv.scrollHeight;
                });
        }
        function updateBrightness(value) {
            const label = document.getElementById('brightness-label');
            label.textContent = `Brightness: ${Math.round((value / 96) * 100)}%`;
            fetch('/brightness?b=' + value)
                .then(response => response.text())
                .then(data => {
                    const consoleDiv = document.getElementById('console');
                    consoleDiv.innerHTML += `<div>ESP: ${data}</div>`;
                    consoleDiv.scrollTop = consoleDiv.scrollHeight;
                });
        }
    </script>
</body>
</html>
)rawliteral";

// add functions

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

void loadAnimation(const char *filename) {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount file system!");
        return;
    }

    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.println("Failed to open JSON file!");
        return;
    }

    // Parse the JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        return;
    }

    // Extract frames
    JsonArray jsonFrames = doc["frames"];
    for (JsonArray frame : jsonFrames) {
        std::vector<std::vector<uint8_t>> frameData;
        for (JsonArray row : frame) {
            std::vector<uint8_t> rowData;
            for (JsonArray pixel : row) {
                rowData.push_back(pixel[0]); // Red
                rowData.push_back(pixel[1]); // Green
                rowData.push_back(pixel[2]); // Blue
            }
            frameData.push_back(rowData);
        }
        frames.push_back(frameData);
    }

    numFrames = frames.size();
    Serial.println("Animation loaded successfully!");
}

void pride() {
// Pride Animation
static uint8_t frame = 0;
  for (int x = 0; x < 48; x++) {
    for (int y = 0; y < 10; y++) {
      uint8_t hue = (x * 10 + y * 10 + frame) % 256;
      int index = getVirtualIndex(x, y);
      leds[index] = CHSV(hue, 255, 255);
    }
  }
  frame = (frame + 1) % 256; // Wrap around hue smoothly
  FastLED.show();
  vTaskDelay(10 / portTICK_PERIOD_MS); // Controls animation speed
}

void vip() {
    // Define the 16x10 RGB matrix
    const uint8_t image[10][16][3] = {
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185},
        {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185},
        {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {236, 98, 211}, {255, 255, 255}, {255, 255, 255},
        {255, 255, 255}, {255, 255, 255}, {235, 98, 211}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 8, 186}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
        {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {224, 8, 186}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {254, 243, 252}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
        {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {253, 243, 252}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {253, 236, 250}, {255, 255, 255}, {255, 255, 255},
        {255, 255, 255}, {255, 255, 255}, {253, 236, 250}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {255, 255, 255}, {255, 255, 255},
        {255, 255, 255}, {255, 255, 255}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {253, 236, 250},
        {253, 236, 250}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185},
        {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}},
        {{224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185},
        {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}, {224, 5, 185}}
    };

    static int frame = 0; // Animation frame offset

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 48; x++) {
            int segment = (x + frame) / 16 % 3;  // Determine which repeated segment (0, 1, 2)
            int localX = (x + frame) % 16;       // Local x-coordinate within the segment

            // Map the color from the 16x10 matrix to the virtual matrix
            int index = getVirtualIndex(x, y);
            leds[index] = CRGB(image[y][localX][0], image[y][localX][1], image[y][localX][2]);
        }
    }

    FastLED.show();
    frame = (frame + 1) % 48; // Wrap the frame offset around after 48
    vTaskDelay(120 / portTICK_PERIOD_MS); // Adjust delay for animation speed
}


void edm() {
    // Load the animation file
    loadAnimation("/edm.json"); // Load the "edm.json" file

    if (numFrames == 0) {
        Serial.println("No frames found in the animation!");
        return;
    }

    int frameIndex = 0; // Start with the first frame

    while (currentAnimationId == 3) { // Assuming "3" is the ID for the EDM animation
        // Get the current frame data
        const std::vector<std::vector<uint8_t>> &frame = frames[frameIndex];

        // Write the frame data to the LEDs
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            for (int x = 0; x < MATRIX_WIDTH; x++) {
                int segment = x / SEGMENT_WIDTH;     // Which 16x10 segment
                int localX = x % SEGMENT_WIDTH;      // Local x within the segment
                int r = frame[y][localX * 3 + 0];    // Red
                int g = frame[y][localX * 3 + 1];    // Green
                int b = frame[y][localX * 3 + 2];    // Blue
                leds[y * MATRIX_WIDTH + x] = CRGB(r, g, b);
            }
        }
        FastLED.show();

        // Move to the next frame
        frameIndex = (frameIndex + 1) % numFrames;

        // Control the animation speed
        vTaskDelay(20 / portTICK_PERIOD_MS); // Adjust for 8 FPS
    }
}

// animation Task
void animationTask(void *parameter) {
    while (true) {
        if (currentAnimationId == 0) {
            // Turn off LEDs
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            FastLED.show();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        } else if (currentAnimationId == 1) {
          pride();
        }
        else if (currentAnimationId == 2) {
          vip();
        }
        else if (currentAnimationId == 3) {
          edm();
        }
        else{
          delay(10);
        }
    }
}

// code

void setup() {
    Serial.begin(115200);

    // Initialize LEDs
    FastLED.addLeds<WS2812B, 2, GRB>(leds, 0, 160);
    FastLED.addLeds<WS2812B, 4, GRB>(leds, 160, 160);
    FastLED.addLeds<WS2812B, 5, GRB>(leds, 320, 160);
    FastLED.clear();
    FastLED.setBrightness(brightness);
    FastLED.show();

    // Connect to Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to Wi-Fi");
    Serial.println(WiFi.localIP());

    // Serve the webpage
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", webpage);
    });

    // Handle commands
    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam("p")) {
            String preset = request->getParam("p")->value();
            if (preset == "0") {
                currentAnimationId = 0; // Off
                message = "Turned Off";
            } else if (preset == "1") {
                currentAnimationId = 1; // Pride
                message = "Started Pride";
            } else if (preset == "2") {
                currentAnimationId = 2; // VIP
                message = "Started VIP";
            } else if (preset == "3") {
                currentAnimationId = 3; // Placeholder for future animation
                message = "Started Animation 3";
            } else {
                message = "Unknown Preset";
            }
            Serial.println(message);
            request->send(200, "text/plain", message);
        } else {
            message = "No preset selected";
            Serial.println(message);
            request->send(400, "text/plain", message);
        }
    });

    server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("b")) {
            brightness = request->getParam("b")->value().toInt();
            FastLED.setBrightness(brightness);
            FastLED.show();
            Serial.println("Brightness set to " + String(brightness));
            request->send(200, "text/plain", "Brightness set to " + String(brightness));
        } else {
            request->send(400, "text/plain", "No brightness value provided");
        }
    });

    server.begin();

    // Start the animation task
    xTaskCreatePinnedToCore(
        animationTask,
        "Animation Task",
        4096,
        NULL,
        1,
        &animationTaskHandle,
        1
    );
}


void loop() {
    // No tasks in loop; everything is event-driven
}
