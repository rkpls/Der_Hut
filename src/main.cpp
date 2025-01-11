#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "config.h" // Include the Wi-Fi credentials
#include "../data/vip.json"
#include "../data/edm.json"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>



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
volatile int currentAnimationId = 0;

CRGB leds[NUM_LEDS];
uint8_t brightness = 10; // Default brightness

// Create an instance of the server
AsyncWebServer server(80);

// Function to load and serve the HTML file
String loadHTML(const char *path) {
    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.println("Failed to open file!");
        return String(); // Return an empty string on failure
    }

    String htmlContent;
    while (file.available()) {
        htmlContent += char(file.read());
    }
    file.close();
    return htmlContent;
}

// HTML Page
const char webpage[] PROGMEM = R"rawliteral(
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
          delay(10); // edm animation
        }
        else{
          delay(10);
        }
    }
}

// code

void setup() {
    Serial.begin(9600);

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
        String htmlContent = loadHTML("/main.html"); // Load the file
        if (htmlContent.length() > 0) {
            request->send(200, "text/html", htmlContent); // Serve the content
        } else {
            request->send(500, "text/plain", "Failed to load HTML file");
        }
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
}


void loop() {
    // No tasks in loop; everything is event-driven
}
