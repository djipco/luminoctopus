/*
  ==================================================================================================
   
   LUMINOCTOPUS
   © 2025, Jean-Philippe Côté (djip.co)

   GNU General Public License v3.0

   This program is free software: you can redistribute it and/or modify it under the terms of the 
   GNU General Public License version 3 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
   General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program. If 
   not, see <https://www.gnu.org/licenses/>.

  ==================================================================================================
*/

// The Luminoctopus firmware uses the OctoWS2811 LED Library by Paul Stoffregen which is freely
// available on GitHub (no license specified): https://github.com/PaulStoffregen/OctoWS2811
#include <OctoWS2811.h>

// Debugging flag
#define DEBUG 0

// Library name and version
constexpr const char* LIB_NAME               = "Luminoctopus";
constexpr const char* LIB_VERSION            = "1.0.0-alpha.4";

// General constants
constexpr uint8_t  CHANNEL_COUNT             =      8; // Number of channels available on device
constexpr uint8_t  BROADCAST_CHANNEL         =    255; // 255 => "broadcast to all channels"
constexpr uint16_t MAX_RGB_LEDS_PER_CHANNEL  =   1365; // Hardware limit due to DMA transfer speed
constexpr uint16_t MAX_RGBW_LEDS_PER_CHANNEL =   1023; // Hardware limit due to DMA transfer speed
constexpr uint16_t MAX_PAYLOAD               =   8192; // Max payload size (in bytes)
constexpr uint8_t  SOF_MARKER                =   0x00; // Start of frame marker
constexpr uint16_t SYSTEM_ID                 = 0x0001; // Luminoctopus system ID (0x00 0x01)

// Command identifier constants
constexpr uint8_t  CMD_CONFIGURE             =   0x01;  // Configure system
constexpr uint8_t  CMD_SYSTEM_EXCLUSIVE      =   0x0A;  // Command meant for a specific system only
constexpr uint8_t  CMD_ASSIGN_COLORS         =   0x10;  // Assign colors
constexpr uint8_t  CMD_FILL_COLOR            =   0x11;  // Fill color
constexpr uint8_t  CMD_UPDATE                =   0x20;  // Update display

// Map of allowed color orders using OctoWS2811's constants 
const int colorOrderMap[] = {
  WS2811_RGB,   //  0
  WS2811_RBG,   //  1
  WS2811_GRB,   //  2
  WS2811_GBR,   //  3
  WS2811_BRG,   //  4
  WS2811_BGR,   //  5
  WS2811_RGBW,  //  6
  WS2811_RBGW,  //  7
  WS2811_GRBW,  //  8
  WS2811_GBRW,  //  9
  WS2811_BRGW,  // 10
  WS2811_BGRW,  // 11
  WS2811_WRGB,  // 12
  WS2811_WRBG,  // 13
  WS2811_WGRB,  // 14
  WS2811_WGBR,  // 15
  WS2811_WBRG,  // 16
  WS2811_WBGR,  // 17
  WS2811_RWGB,  // 18
  WS2811_RWBG,  // 19
  WS2811_GWRB,  // 20
  WS2811_GWBR,  // 21
  WS2811_BWRG,  // 22
  WS2811_BWGR,  // 23
  WS2811_RGWB,  // 24
  WS2811_RBWG,  // 25
  WS2811_GRWB,  // 26
  WS2811_GBWR,  // 27
  WS2811_BRWG,  // 28
  WS2811_BGWR   // 29
};

// Number of available color orders
constexpr int COLOR_ORDER_COUNT = sizeof(colorOrderMap) / sizeof(colorOrderMap[0]);

// Actual names for the color orders
const char* colorOrderNames[] = {
  "RGB", "RBG", "GRB", "GBR", "BRG", "BGR", "RGBW", "RBGW", "GRBW", "GBRW", "BRGW", "BGRW", "WRGB",
  "WRBG", "WGRB", "WGBR", "WBRG", "WBGR", "RWGB", "RWBG", "GWRB", "GWBR", "BWRG", "BWGR", "RGWB",
  "RBWG", "GRWB", "GBWR", "BRWG", "BGWR"
};

// Dynamic LED count and buffer management
uint8_t componentsPerPixel = 3;                       // 3 for RGB, 4 for RGBW
bool connected = false;                               // Whether serial is connected
uint8_t checksumData = 0;                             // Data used to calculate checksum
uint8_t cmd = 0;                                      // Identified command
bool frameReady = false;                              // Whether a new frame is ready to display
bool isRGBW = false;                                  // Whether we are using 4-channel LEDs
uint16_t ledsPerChannel = MAX_RGBW_LEDS_PER_CHANNEL;  // Number of LEDs per channel
uint16_t len = 0;                                     // Length of payload

enum class ParseState : uint8_t {                     // Serial parser state machine
  WAIT_SOF,
  READ_CMD,
  READ_LEN1,
  READ_LEN2,
  READ_DATA,
  READ_CHK
};

ParseState state = ParseState::WAIT_SOF;              // Current state
uint8_t payload[MAX_PAYLOAD];                         // Byte array for the payload
uint16_t payloadIndex = 0;                            // Position of next byte to write in payload

int* dmaBuffer = nullptr;                             // DMA buffer
int* drawBuffer = nullptr;                            // Draw buffer
OctoWS2811* leds = nullptr;                           // OctoWS2811

// Debugging
#if DEBUG
elapsedMillis timer;
bool busy = false;
#endif

bool allocateBuffers(uint16_t ledCount) {

  // Free existing buffers if they exist
  if (dmaBuffer) {
    free(dmaBuffer);
    dmaBuffer = nullptr;
  }

  if (drawBuffer) {
    free(drawBuffer);
    drawBuffer = nullptr;
  }
  
  // Calculate required buffer size
  size_t bufferSize = ledCount * 6 * sizeof(int);
  
  // Allocate new buffers
  dmaBuffer = (int*)malloc(bufferSize);
  drawBuffer = (int*)malloc(bufferSize);
  
  // Check for success of buffer allocation
  if (!dmaBuffer || !drawBuffer) {
    Serial.println("Error: Failed to allocate memory for LED buffers");
    if (dmaBuffer) {
      free(dmaBuffer);
      dmaBuffer = nullptr;
    }
    if (drawBuffer) {
      free(drawBuffer);
      drawBuffer = nullptr;
    }
    return false;
  }
  
  // Clear buffers and return success
  memset(dmaBuffer, 0, bufferSize);
  memset(drawBuffer, 0, bufferSize);
  return true;

}

bool createOctoWS2811Instance(uint16_t ledCount, int configFlags) {

  // Delete existing instance
  if (leds) {
    delete leds;
    leds = nullptr;
  }
  
  // Allocate buffers for the new LED count
  if (!allocateBuffers(ledCount)) {
    return false;
  }
  
  // Create new OctoWS2811 instance
  leds = new OctoWS2811(ledCount, dmaBuffer, drawBuffer, configFlags);
  
  if (!leds) {
    Serial.println("Error: Failed to create OctoWS2811 instance");
    return false;
  }
  
  return true;

}

void configure(uint8_t order = WS2811_GRB, uint8_t speed = WS2811_800kHz, uint16_t ledCount = 100) {

  bool is4 = (colorOrderMap[order] >= WS2811_RGBW);

  if (is4) {
    if (ledCount > MAX_RGBW_LEDS_PER_CHANNEL) {
      Serial.print("Invalid LED count: ");
      Serial.print(ledCount);
      Serial.print(". Must be between 1 and ");
      Serial.print(MAX_RGBW_LEDS_PER_CHANNEL);
      Serial.println(".");
    }
  } else {
    if (ledCount > MAX_RGB_LEDS_PER_CHANNEL) {
      Serial.print("Invalid LED count: ");
      Serial.print(ledCount);
      Serial.print(". Must be between 1 and ");
      Serial.print(MAX_RGB_LEDS_PER_CHANNEL);
      Serial.println(".");
    }
  }

  // Create new OctoWS2811 instance with the requested configuration
  if (!createOctoWS2811Instance(ledCount, colorOrderMap[order] | speed)) {
    Serial.println("Failed to update configuration");
    return;
  }

  // Update global state
  ledsPerChannel = ledCount;
  isRGBW = (colorOrderMap[order] >= WS2811_RGBW);
  componentsPerPixel = isRGBW ? 4 : 3;
  
  // Initialize OctoWS2811 object with the requested configuration and tell loop() to update
  leds->begin();
  frameReady = true; 

}

void sendConfigurationOnSerial(uint8_t order, uint8_t speed, uint16_t ledCount) {

  Serial.print("Configuration: ");
  
  // Color order and components
  const char* colorOrderName = "Unknown";

  for (int i = 0; i < COLOR_ORDER_COUNT; i++) {
    if (colorOrderMap[i] == order) {
      colorOrderName = colorOrderNames[i];
      break;
    }
  }
  Serial.print(colorOrderName);
  Serial.print(", ");

  // Speed and protocol
  switch (speed) {
    case WS2811_800kHz: Serial.print("800kHz, WS2811"); break;
    case WS2811_400kHz: Serial.print("400kHz, WS2811"); break;
    case WS2813_800kHz: Serial.print("800kHz, WS2813"); break;
    default:            Serial.print("Unknown"); break;
  }

  // Number of LEDs
  Serial.print(", ");
  Serial.print(ledCount);
  Serial.println(" LEDs/ch");

}

void setup() {

  // On the Teensy, the USB hardware initialization happens before setup() is run and the baud rate 
  // used is always the full native USB speed (either 12 or 480 Mbit/sec). So, there is no need to 
  // call Serial.begin(). Details: https://www.pjrc.com/teensy/td_serial.html

  // Initialize OctoWS2811 with default configuration
  if (!createOctoWS2811Instance(MAX_RGBW_LEDS_PER_CHANNEL, WS2811_GRB | WS2811_800kHz)) {
    Serial.println("Failed to initialize LED system");
    while(1); // Halt execution
  }

}

void loop() {

  // Wait for remote software to open serial connection. When it has, perform configuration and send
  // confirmation.
  if (Serial.dtr()) {
    if (!connected) {
      connected = true;
      configure();
      Serial.print("Connected to ");
      Serial.print(LIB_NAME);
      Serial.print(" v");
      Serial.println(LIB_VERSION);
    } 
  } else {
    if (connected) connected = false;
    delay(20); 
    return;
  }
  
  // Read incoming serial data (if any)
  while (Serial.available()) {
    readSerialByte(Serial.read());
  }
  
  // Performance debugging
  #if DEBUG
    if (leds && leds->busy()) {
      if (!busy) {
        busy = true;
        timer = 0;
      }
    } else {
      if (busy) {
        busy = false;
        Serial.print("Update: ");
        Serial.print(timer);
        Serial.println("ms");
        timer = 0;
      }
    }
  #endif

  // If frame data is ready, update the LEDs. We wait for leds.busy() to be false. However, this may
  // cause dropped frames if too much data is pushed. That's why the client must respect the limits.
  if (frameReady && leds && !leds->busy()) {
    leds->show();
    frameReady = false;
  }

}

void readSerialByte(uint8_t b) {

  switch (state) {

    // Wait for start of frame marker
    case ParseState::WAIT_SOF:
      if (b == SOF_MARKER) state = ParseState::READ_CMD;
      break;
      
    // Read command byte
    case ParseState::READ_CMD:

      cmd = b;
      checksumData = b;

      // For zero-payload commands, we jump ahead to READ_CHK.
      if (cmd == CMD_UPDATE) {
        len = 0;
        state = ParseState::READ_CHK;
      } else {
        state = ParseState::READ_LEN1;
      }

      break;
      
    // Get first byte of payload length
    case ParseState::READ_LEN1:
      len = b;
      checksumData += b;
      state = ParseState::READ_LEN2;
      break;
      
    // Get second byte of payload length, and compute final length
    case ParseState::READ_LEN2:
      len |= (b << 8);
      checksumData += b;
      if (len > MAX_PAYLOAD) {
        state = ParseState::WAIT_SOF;
        Serial.print("Stated payload size is too big: ");
        Serial.print(len);
        Serial.println(" bytes");
      } else {
        payloadIndex = 0;
        state = ParseState::READ_DATA;
      }
      break;
      
    // Read full payload
    case ParseState::READ_DATA:
      payload[payloadIndex++] = b;
      checksumData += b;
      if (payloadIndex >= len) state = ParseState::READ_CHK;
      break;
      
    // Verify checksum
    case ParseState::READ_CHK:
      if (checksumData % 256 == b) {
        processCommand();
      } else {
        Serial.print("Checksum mismatch. Expected: ");
        Serial.print(checksumData % 256);
        Serial.print(", Received: ");
        Serial.println(b);
      }
      state = ParseState::WAIT_SOF;
      break;

  }
  
}

void processCommand() {

  // CMD_CONFIGURE
  if (cmd == CMD_CONFIGURE) {
    handleConfigureCommand();
  
  // CMD_ASSIGN_COLORS
  } else if (cmd == CMD_ASSIGN_COLORS) {
    handleAssignColorsCommand();
    
  // CMD_FILL_COLOR
  } else if (cmd == CMD_FILL_COLOR) {
    handleFillColorCommand();
    
  // CMD_UPDATE
  } else if (cmd == CMD_UPDATE) {
    handleUpdateCommand();
    
  // Invalid command syntax
  } else {
    Serial.print("Invalid command. Cmd: 0x");
    Serial.print(cmd, HEX);
    Serial.print(", Payload length: ");
    Serial.println(len);
  }
}

void handleConfigureCommand() {

  // We leave for the possibility of a larger length to allow future extensions in the configuration
  // options.
  if (len < 4) {
    Serial.println("Invalid configuration payload length.");
    return;
  }
  
  // Read color order and speed
  uint8_t colorOrder = payload[0];
  uint8_t speed = payload[1];

  // Read LED count
  uint16_t count = payload[2] | (payload[3] << 8);

  // Validate if color order, speed and count are valid
  bool validColor = (colorOrder < COLOR_ORDER_COUNT);
  bool validSpeed = (speed == WS2811_800kHz || speed == WS2811_400kHz || speed == WS2813_800kHz);

  // Validate LED count (depends on component count)
  bool validLedCount = (count > 0 && count <= MAX_RGB_LEDS_PER_CHANNEL);
  
  if (!validColor) {
    Serial.print("Invalid color order code: ");
    Serial.println(colorOrder);
  }
  if (!validSpeed) {
    Serial.print("Invalid speed flag: 0x");
    Serial.println(speed, HEX);
  }
  if (!validLedCount) {
    Serial.print("Invalid LED count: ");
    Serial.print(count);
    Serial.print(". Must be between 1 and ");
    Serial.print(MAX_RGBW_LEDS_PER_CHANNEL);
    Serial.println(".");
  }
  
  if (validColor && validSpeed && validLedCount) {
    configure(colorOrderMap[colorOrder], speed, count);
    sendConfigurationOnSerial(colorOrderMap[colorOrder], speed, count);
  } else {
    Serial.println("Configuration rejected.");
  }

}

void handleAssignColorsCommand() {

  if (!leds) {
    Serial.println("Error: system not initialized");
    return;
  }

  // Check payload format (RGB or RGBW)
  if ((len - 1) % componentsPerPixel != 0) {
    Serial.println("Invalid color assignment payload.");
    return;
  }
  
  // First byte of payload is channel
  uint8_t ch = payload[0];
  if (ch >= CHANNEL_COUNT) {
    Serial.print("Invalid channel: ");
    Serial.println(ch);
    return;
  }
  
  // Calculate number of LEDs in this payload
  uint16_t ledsInPayload = (len - 1) / componentsPerPixel;
  
  // Prepare data and index for the loop
  const uint8_t* data = &payload[1];
  uint16_t index = ch * ledsPerChannel;
  
  // Check if we are dealing with 3 or 4 color components
  if (isRGBW) {
    for (uint16_t i = 0; i < ledsInPayload; i++) {
      leds->setPixel(index + i, data[0], data[1], data[2], data[3]);
      data += 4;
    }
  } else {
    for (uint16_t i = 0; i < ledsInPayload; i++) {
      leds->setPixel(index + i, data[0], data[1], data[2]);
      data += 3;
    }
  }

}

void handleFillColorCommand() {

  if (!leds) {
    Serial.println("Error: system not initialized");
    return;
  }
  
  if (len < (isRGBW ? 5 : 4)) {
    Serial.println("Invalid fill color payload.");
    return;
  }
  
  // Channel
  uint8_t ch = payload[0];

  // Color
  uint8_t r = payload[1];
  uint8_t g = payload[2];
  uint8_t b = payload[3];
  uint8_t w = isRGBW ? payload[4] : 0;
  
  // Check if we are dealing with RGB or RGBW, unique channel or broadcast, and update the LEDs.
  if (isRGBW) {
    if (ch == BROADCAST_CHANNEL) {
      for (int i = 0; i < CHANNEL_COUNT * ledsPerChannel; i++) {
        leds->setPixel(i, r, g, b, w);
      }
    } else if (ch < CHANNEL_COUNT) {
      for (int i = 0; i < ledsPerChannel; i++) {
        leds->setPixel(ch * ledsPerChannel + i, r, g, b, w);
      }
    }
  } else {
    if (ch == BROADCAST_CHANNEL) {
      for (int i = 0; i < CHANNEL_COUNT * ledsPerChannel; i++) {
        leds->setPixel(i, r, g, b);
      }
    } else if (ch < CHANNEL_COUNT) {
      for (int i = 0; i < ledsPerChannel; i++) {
        leds->setPixel(ch * ledsPerChannel + i, r, g, b);
      }
    }
  }

}

void handleUpdateCommand() {
  
  if (!leds) {
    Serial.println("Error: LED system not initialized");
    return;
  }

  frameReady = true; // Tells the main loop() to update

}
