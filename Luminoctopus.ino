/*
  ==================================================================================================
   
   LUMINOCTOPUS v1.0.0-alpha.1
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

// General constants
constexpr uint8_t  CHANNEL_COUNT        =      8; // Number of channels available on device
constexpr uint8_t  BROADCAST_CHANNEL    =    255; // Channel 255 means "broadcast to all channels"
constexpr uint16_t MAX_LEDS_PER_CHANNEL =   1365; // Max number of LEDs per channel (see README.md)
constexpr uint16_t MAX_PAYLOAD          =   8192; // Max payload size (in bytes)
constexpr uint8_t  SOF_MARKER           =   0x00; // Start of frame marker
constexpr uint16_t SYSTEM_ID            = 0x0001; // Luminoctopus system ID (0x00 0x01)

// Command identifier constants
constexpr uint8_t  CMD_CONFIGURE_DEVICE =   0x10;  // Configure device
constexpr uint8_t  CMD_ASSIGN_COLORS    =   0x20;  // Assign colors
constexpr uint8_t  CMD_FILL_COLOR       =   0x21;  // Fill color

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


uint8_t componentsPerPixel = 3;            // 3 for RGB, 4 for RGBW
bool connected = false;                    // Whether serial is connected
uint8_t checksum = 0;                      // Checksum
uint8_t cmd = 0;                           // Identified command
bool frameReady = false;                   // Whether a new frame of data is ready to be displayed
bool isRGBW = false;                       // Whether we are using 4-channel LEDs
uint16_t len = 0;                          // Length of payload
enum class ParseState : uint8_t {          // Serial parser state machine
  WAIT_SOF,
  READ_CMD,
  READ_LEN1,
  READ_LEN2,
  READ_DATA,
  READ_CHK
};
ParseState state = ParseState::WAIT_SOF;   // Current state
uint8_t payload[MAX_PAYLOAD];              // Byte array for the payload
uint16_t payloadIndex = 0;                 // Position of the next byte to write in the payload

// DMA buffer, draw buffer and OctoWS2811 instance
DMAMEM int dmaBuffer[MAX_LEDS_PER_CHANNEL*6];
int drawBuffer[MAX_LEDS_PER_CHANNEL*6];
OctoWS2811 leds(MAX_LEDS_PER_CHANNEL, dmaBuffer, drawBuffer);



void configure(int configFlags = WS2811_GRB | WS2811_800kHz) {

  // Initialize OctoWS2811 object with the requested configuration
  leds.begin(MAX_LEDS_PER_CHANNEL, dmaBuffer, drawBuffer, configFlags);
  frameReady = true; // tells the loop() to update

}

void sendConfigurationOnSerial(int configFlags = WS2811_GRB | WS2811_800kHz) {

  Serial.print("Configuration: ");

  int colorFlag = configFlags & 0x3F;
  const char* colorOrderName = "Unknown";
  for (int i = 0; i < COLOR_ORDER_COUNT; i++) {
    if (colorOrderMap[i] == colorFlag) {
      colorOrderName = colorOrderNames[i];
      break;
    }
  }
  Serial.print(colorOrderName);
  Serial.print(", ");

  int speedBits = configFlags & 0xC0;

  switch (speedBits) {
    case WS2811_800kHz: Serial.println("800kHz, WS2811"); break;
    case WS2811_400kHz: Serial.println("400kHz, WS2811"); break;
    case WS2813_800kHz: Serial.println("800kHz, WS2813"); break;
    default:            Serial.println("Unknown"); break;
  }
  
}

void setup() {
  // On the Teensy, the USB hardware initialization happens before setup() is run and the baud rate 
  // used is always the full native USB speed (either 12 or 480 Mbit/sec). So, there is no need to 
  // call Serial.begin(). Details: https://www.pjrc.com/teensy/td_serial.html
}

void loop() {

  // Wait for remote software to open serial connection. When it has, perform configuration and send
  // confirmation.
  if (Serial.dtr()) {
    if (!connected) {
      connected = true;
      configure();
      Serial.println("Connected to Djip.Co LED controller");
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

  // If frame data is ready, update the LEDs
  if (frameReady && !leds.busy()) {
    leds.show();
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
      checksum = b;
      state = ParseState::READ_LEN1;
      break;

    // Get first byte of payload length
    case ParseState::READ_LEN1:
      len = b;
      checksum += b;
      state = ParseState::READ_LEN2;
      break;

    // Get second byte of payload length, and compute final length
    case ParseState::READ_LEN2:
      len |= (b << 8);
      checksum += b;
      if (len > MAX_PAYLOAD) {
        state = ParseState::WAIT_SOF;
        Serial.print("Declared payload is too large: ");
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
      checksum += b;
      if (payloadIndex >= len) state = ParseState::READ_CHK;
      break;

    // Verify checksum
    case ParseState::READ_CHK:
      if (checksum % 256 == b) {
        processCommand();
      } else {
        Serial.print("Checksum mismatch. Expected: ");
        Serial.print(checksum % 256);
        Serial.print(", Received: ");
        Serial.println(b);
      }
      state = ParseState::WAIT_SOF;
      break;

  }

}

void processCommand() {

  // CMD_CONFIGURE_DEVICE
  if (cmd == CMD_CONFIGURE_DEVICE) {
    handleConfigureCommand();
  
  // CMD_ASSIGN_COLORS
  } else if (cmd == CMD_ASSIGN_COLORS) {
    handleAssignColorsCommand();

  // CMD_FILL_COLOR
  } else if (cmd == CMD_FILL_COLOR) {
    handleFillColorCommand();

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

  // First two bytes: system ID
  uint16_t systemId = payload[0] | (payload[1] << 8);

  if (systemId != SYSTEM_ID) {
    Serial.print("Invalid system ID: 0x");
    Serial.print(systemId, HEX);
    Serial.print(". Expected ");
    Serial.print(SYSTEM_ID, HEX);
    Serial.println(".");
    return;
  }

  // Read color order and speed
  uint8_t colorOrder = payload[2];
  uint8_t speed = payload[3];

  // Validate if color order and speed are valid
  bool validColor = (colorOrder < COLOR_ORDER_COUNT);
  bool validSpeed = (speed == WS2811_800kHz || speed == WS2811_400kHz || speed == WS2813_800kHz);
  
  // Assigned isRGB boolean
  isRGBW = (colorOrderMap[colorOrder] >= WS2811_RGBW);
  componentsPerPixel = isRGBW ? 4 : 3;

  if (!validColor) {
    Serial.print("Invalid color order code: ");
    Serial.println(colorOrder);
  }

  if (!validSpeed) {
    Serial.print("Invalid speed flag: 0x");
    Serial.println(speed, HEX);
  }

  if (validColor && validSpeed) {
    int configFlags = colorOrderMap[colorOrder] | speed;
    configure(configFlags);
    sendConfigurationOnSerial(configFlags);
  } else {
    Serial.println("Configuration rejected.");
  }

}

void handleAssignColorsCommand() {

  // Check payload length
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
  uint16_t index = ch * MAX_LEDS_PER_CHANNEL;

  // Check if we are dealing with 3 or 4 color components
  if (isRGBW) {
    for (uint16_t i = 0; i < ledsInPayload; i++) {
      leds.setPixel(index + i, data[0], data[1], data[2], data[3]);
      data += 4;
    }
  } else {
    for (uint16_t i = 0; i < ledsInPayload; i++) {
      leds.setPixel(index + i, data[0], data[1], data[2]);
      data += 3;
    }
  }

  // Identify that a new frame of data is ready to be displayed
  frameReady = true;

}

void handleFillColorCommand() {

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
      for (int i = 0; i < CHANNEL_COUNT * MAX_LEDS_PER_CHANNEL; i++) {
        leds.setPixel(i, r, g, b, w);
      }
    } else if (ch < CHANNEL_COUNT) {
      for (int i = 0; i < MAX_LEDS_PER_CHANNEL; i++) {
        leds.setPixel(ch * MAX_LEDS_PER_CHANNEL + i, r, g, b, w);
      }
    }

  } else {

    if (ch == BROADCAST_CHANNEL) {
      for (int i = 0; i < CHANNEL_COUNT * MAX_LEDS_PER_CHANNEL; i++) {
        leds.setPixel(i, r, g, b);
      }
    } else if (ch < CHANNEL_COUNT) {
      for (int i = 0; i < MAX_LEDS_PER_CHANNEL; i++) {
        leds.setPixel(ch * MAX_LEDS_PER_CHANNEL + i, r, g, b);
      }
    }
    
  }

  frameReady = true;

}

uint8_t computeChecksum(uint8_t cmd, uint16_t len, uint8_t* data) {
  uint32_t sum = cmd + (len & 0xFF) + (len >> 8);
  for (uint16_t i = 0; i < len; i++) sum += data[i];
  return sum % 256;
}
