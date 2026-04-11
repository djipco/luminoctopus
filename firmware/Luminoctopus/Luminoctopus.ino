/*
  ==================================================================================================
   
   LUMINOCTOPUS
   © 2025-2026, Jean-Philippe Côté (djip.co)

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
constexpr const char* LIB_NAME                  = "Luminoctopus";
constexpr const char* LIB_VERSION               = "1.0.0-alpha.10";

// Protocol version
constexpr uint8_t PROTOCOL_VERSION              = 1;

// General constants
constexpr uint8_t  BROADCAST_CHANNEL            =    255; // 255 => "broadcast to all channels"
constexpr uint8_t  BUFFER_MULTIPLIER            =      6; // Multiplier for buffer
constexpr uint8_t  CHANNEL_COUNT                =      8; // Number of channels available on device
constexpr uint16_t CHECKSUM_MODULO              =    256; // Modulo for checksum
constexpr uint16_t DEFAULT_CHANNEL_COUNT        =    300; // Default number of LEDs per channel
constexpr uint16_t MAX_PAYLOAD                  =   8192; // Max payload size (in bytes)
constexpr uint16_t MAX_RGB_LEDS_PER_CHANNEL     =   1365; // Hardware limit due to DMA transfer speed
constexpr uint16_t MAX_RGBW_LEDS_PER_CHANNEL    =   1023; // Hardware limit due to DMA transfer speed
constexpr uint8_t  SOF_MARKER                   =   0x00; // Start of frame marker
constexpr uint16_t SYSTEM_ID                    = 0x0001; // Luminoctopus system ID (0x00 0x01)

// Commands (0x00 is reserved for SOF)
constexpr uint8_t CMD_GET_INFO                  = 0x01; // Query commands (0x01–0x1F)
constexpr uint8_t CMD_GET_CONFIG                = 0x02;
constexpr uint8_t CMD_CONFIGURE                 = 0x20; // Configuration commands (0x20–0x3F)
constexpr uint8_t CMD_ASSIGN_COLORS             = 0x40; // LED data commands (0x40–0x5F)
constexpr uint8_t CMD_FILL_COLOR                = 0x41;
constexpr uint8_t CMD_UPDATE                    = 0x60; // Control commands (0x60–0x7F)
// 0x80–0x9F (reserverd)
// 0xA0–0xBF (reserverd)
// 0xC0–0xDF (reserverd)
constexpr uint8_t CMD_SYSTEM_EXCLUSIVE          = 0xE0; // System-exclusive commands (0xE0–0xFF)

// Error codes
constexpr uint8_t ERR_UNKNOWN_COMMAND           = 0x01; // Query errors (0x01–0x1F)
constexpr uint8_t ERR_PAYLOAD_TOO_SHORT         = 0x20; // Configuration errors (0x20–0x3F)
constexpr uint8_t ERR_INVALID_COLOR_ORDER       = 0x21;
constexpr uint8_t ERR_INVALID_SPEED             = 0x22;
constexpr uint8_t ERR_INVALID_LED_COUNT         = 0x23;
constexpr uint8_t ERR_INVALID_CHANNEL           = 0x40; // LED data errors (0x40–0x5F)
constexpr uint8_t ERR_COLOR_COMPONENT_MISMATCH  = 0x41;
constexpr uint8_t ERR_SYSTEM_NOT_INITIALIZED    = 0xE0; // System errors (0xE0–0xFF)
constexpr uint8_t ERR_PAYLOAD_TOO_LARGE         = 0xE1;
constexpr uint8_t ERR_CHECKSUM_MISMATCH         = 0xE2;


// Map of allowed color orders using OctoWS2811's constants 
const int colorOrderMap[] = {
  WS2811_RGB,   //  0
  WS2811_RBG,   //  1
  WS2811_GRB,   //  2 (default)
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

// State
uint8_t componentsPerPixel = 3;                       // 3 for RGB, 4 for RGBW
bool connected = false;                               // Whether serial is connected
uint8_t checksumState = 0;                            // Data used to calculate checksum
uint8_t cmd = 0;                                      // Identified command
bool frameReady = false;                              // Whether a new frame is ready to display
bool isRGBW = false;                                  // Whether we are using 4-channel LEDs
uint16_t ledsPerChannel = MAX_RGBW_LEDS_PER_CHANNEL;  // Number of LEDs per channel
uint16_t len = 0;                                     // Length of payload
uint8_t chkL = 0;

uint8_t currentColorOrder = 2;                        // Currently configured color order (GRB by default)
uint8_t currentSpeed = WS2811_800kHz;                 // Currently configured speed (WS2811_800kHz by default)

enum class ParseState : uint8_t {                     // Serial parser state machine
  WAIT_SOF,
  READ_CMD,
  READ_LEN1,
  READ_LEN2,
  READ_DATA,
  READ_CHK,
  READ_CHK2
};

ParseState state = ParseState::WAIT_SOF;              // Current state
uint8_t payload[MAX_PAYLOAD];                         // Byte array for the payload
uint16_t payloadIndex = 0;                            // Position of next byte to write in payload

constexpr uint16_t MAX_LEDS = MAX_RGB_LEDS_PER_CHANNEL;
constexpr size_t MAX_BUFFER_SIZE = MAX_LEDS * BUFFER_MULTIPLIER * sizeof(int);

// Buffers are statically allocated for the maximum supported LED count to guarantee deterministic 
// behavior and avoid heap fragmentation.
int dmaBufferStatic[MAX_BUFFER_SIZE / sizeof(int)];
int drawBufferStatic[MAX_BUFFER_SIZE / sizeof(int)];

// Debugging
#if DEBUG
elapsedMillis timer;
bool busy = false;
#endif

// Storage for placement-new OctoWS2811 instance (properly aligned)
alignas(OctoWS2811) static uint8_t octoStorage[sizeof(OctoWS2811)];
OctoWS2811* leds = nullptr;

void createOctoWS2811Instance(uint16_t ledCount, int configFlags) {

  // Destroy previous instance if it exists
  if (leds) {
    leds->~OctoWS2811();
    leds = nullptr;
  }
  
  // Clear buffers to avoid stale LED data after reconfiguration
  memset(dmaBufferStatic, 0, MAX_BUFFER_SIZE);
  memset(drawBufferStatic, 0, MAX_BUFFER_SIZE);

  // Construct in preallocated storage
  leds = new (octoStorage) OctoWS2811(
    ledCount,
    dmaBufferStatic,
    drawBufferStatic,
    configFlags
  );

}

void configure(
  uint8_t order = 2,
  uint8_t speed = WS2811_800kHz, 
  uint16_t ledCount = DEFAULT_CHANNEL_COUNT
) {

  if (!isValidColorOrder(order)) {
    sendError(ERR_INVALID_COLOR_ORDER, "COLOR_ORDER_NOT_SUPPORTED");
    return;
  }

  if (!isValidSpeed(speed)) {
      sendError(ERR_INVALID_SPEED, "PROTOCOL_SPEED_NOT_SUPPORTED");
      return;
  }

  bool is4 = (colorOrderMap[order] >= WS2811_RGBW);
  uint16_t maxLeds = is4 ? MAX_RGBW_LEDS_PER_CHANNEL : MAX_RGB_LEDS_PER_CHANNEL;
    
  if (ledCount == 0 || ledCount > maxLeds) {
    sendError(ERR_INVALID_LED_COUNT, "LEDS_PER_CHANNEL_OUT_OF_RANGE");
    return;
  }

  // Create new OctoWS2811 instance with the requested configuration
  createOctoWS2811Instance(ledCount, colorOrderMap[order] | speed);

  // Update global state
  currentColorOrder  = order;
  currentSpeed       = speed;
  ledsPerChannel = ledCount;
  isRGBW = (colorOrderMap[order] >= WS2811_RGBW);
  componentsPerPixel = isRGBW ? 4 : 3;
  
  // Initialize OctoWS2811 object with the requested configuration and tell loop() to update
  leds->begin();
  frameReady = true; 

}

void setup() {

  // On the Teensy, the USB hardware initialization happens before setup() is run and the baud rate 
  // used is always the full native USB speed (either 12 or 480 Mbit/sec). So, there is no need to 
  // call Serial.begin(). Details: https://www.pjrc.com/teensy/td_serial.html

  createOctoWS2811Instance(DEFAULT_CHANNEL_COUNT, WS2811_GRB | WS2811_800kHz);

}

void loop() {

  // Wait for remote software to open serial connection. When it has, perform configuration and send
  // confirmation.
  if (Serial.dtr()) {
    if (!connected) {
      connected = true;
      configure();
      handleGetInfoCommand();
    } 
  } else {

    if (connected) {
      connected = false;
      resetParser();
      frameReady = false;
    }
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
      checksumState = b;

      // For zero-payload commands, we jump ahead to READ_CHK.
      if (cmd == CMD_UPDATE || cmd == CMD_GET_INFO || cmd == CMD_GET_CONFIG) {
        len = 0;
        state = ParseState::READ_CHK;
      } else {
        state = ParseState::READ_LEN1;
      }

      break;
      
    // Get first byte of payload length
    case ParseState::READ_LEN1:
      len = b;
      checksumState += b;
      state = ParseState::READ_LEN2;
      break;
      
    // Get second byte of payload length, and compute final length
    case ParseState::READ_LEN2:
      len |= (b << 8);
      checksumState += b;
      if (len > MAX_PAYLOAD) {
        state = ParseState::WAIT_SOF;
        checksumState = 0;
        sendError(ERR_PAYLOAD_TOO_LARGE, "MAX_PAYLOAD_EXCEEDED");
      } else {
        payloadIndex = 0;
        state = ParseState::READ_DATA;
      }
      break;
      
    // Read full payload
    case ParseState::READ_DATA:
      payload[payloadIndex++] = b;
      checksumState += b;
      if (payloadIndex >= len) state = ParseState::READ_CHK;
      break;
      
    // Verify checksum
    case ParseState::READ_CHK:
      // if (checksumState % CHECKSUM_MODULO == b) {
      //   processCommand();
      // } else {
      //   checksumState = 0;
      //   sendError(ERR_CHECKSUM_MISMATCH, "CHECKSUM_VERIFICATION_FAILED");
      // }
      // state = ParseState::WAIT_SOF;
      // break;
      chkL = b;              // stocker le premier octet, vérifier au prochain
      state = ParseState::READ_CHK2;
      break;

    case ParseState::READ_CHK2: {
      uint16_t received = chkL | (b << 8);
      uint16_t expected = checksumState % CHECKSUM_MODULO;  // MSB = 0x00 pour l'instant

      if (received == expected) {
        processCommand();
      } else {
        checksumState = 0;
        sendError(ERR_CHECKSUM_MISMATCH, "CHECKSUM_VERIFICATION_FAILED");
      }
      state = ParseState::WAIT_SOF;
      break;
    }

  }
  
}

void processCommand() {

    switch (cmd) {
      
      case CMD_GET_INFO:      handleGetInfoCommand();      break;
      case CMD_CONFIGURE:     handleConfigureCommand();    break;
      case CMD_GET_CONFIG:    handleGetConfigCommand();    break;
      case CMD_ASSIGN_COLORS: handleAssignColorsCommand(); break;
      case CMD_FILL_COLOR:    handleFillColorCommand();    break;
      case CMD_UPDATE:        handleUpdateCommand();       break;
      default:
        sendError(ERR_UNKNOWN_COMMAND, "COMMAND_NOT_SUPPORTED");
        break;
    }

}

void handleGetInfoCommand() {

  Serial.print("INFO: ");

  Serial.print("DEVICE=");
  Serial.print(LIB_NAME);

  Serial.print(" PROTOCOL=");
  Serial.print(PROTOCOL_VERSION);

  Serial.print(" FIRMWARE=");
  Serial.print(LIB_VERSION);

  Serial.print(" CHANNELS=");
  Serial.print(CHANNEL_COUNT);

  Serial.println(" TRANSPORT=USB");

}

void handleGetConfigCommand() {

  Serial.print("CONFIG: ");

  Serial.print("COLOR_ORDER=");
  Serial.print(colorOrderNames[currentColorOrder]);

  Serial.print(" SPEED=");
  switch (currentSpeed) {
    case WS2811_800kHz: Serial.print("800kHz"); break;
    case WS2811_400kHz: Serial.print("400kHz"); break;
    case WS2813_800kHz: Serial.print("800kHz_WS2813"); break;
    default:            Serial.print("UNKNOWN"); break;
  }

  Serial.print(" LEDS_PER_CHANNEL=");
  Serial.print(ledsPerChannel);

  Serial.println();

}

void sendError(uint8_t errCode, const char* message) {
  Serial.print("ERROR: CODE=0x");
  Serial.print(errCode, HEX);
  Serial.print(" MESSAGE=");
  Serial.println(message);
}

void handleConfigureCommand() {

  // We leave for the possibility of a larger length to allow future extensions in the configuration
  // options.
  if (len < 4) {
    sendError(ERR_PAYLOAD_TOO_SHORT, "PAYLOAD_TOO_SHORT");
    return;
  }
  
  // Read color order and speed
  uint8_t colorOrder = payload[0];
  uint8_t speed = payload[1];

  // Read LED count
  uint16_t count = payload[2] | (payload[3] << 8);

  // Validate if color order, speed and count are valid
  bool validColor = isValidColorOrder(colorOrder);
  bool validSpeed = isValidSpeed(speed);

  // Validate LED count (depends on component count)
  bool validLedCount;

  if (validColor && colorOrderMap[colorOrder] >= WS2811_RGBW) {
    validLedCount = (count > 0 && count <= MAX_RGBW_LEDS_PER_CHANNEL);
  } else {
    validLedCount = (count > 0 && count <= MAX_RGB_LEDS_PER_CHANNEL);
  }
  
  if (!validColor) {
    sendError(ERR_INVALID_COLOR_ORDER, "COLOR_ORDER_NOT_SUPPORTED");
  }

  if (!validSpeed) {
    sendError(ERR_INVALID_SPEED, "PROTOCOL_SPEED_NOT_SUPPORTED");
  }

  if (!validLedCount) {
    sendError(ERR_INVALID_LED_COUNT, "LEDS_PER_CHANNEL_OUT_OF_RANGE");
  }
  
  if (validColor && validSpeed && validLedCount) {
    configure(colorOrder, speed, count);
    handleGetConfigCommand();
  }

}

void handleAssignColorsCommand() {

  if (!leds) {
    sendError(ERR_SYSTEM_NOT_INITIALIZED, "LED_SYSTEM_NOT_READY");
    return;
  }

  // Check payload format (RGB or RGBW)
  if ((len - 1) % componentsPerPixel != 0) {
    sendError(ERR_COLOR_COMPONENT_MISMATCH, "COLOR_COMPONENT_MISMATCH");
    return;
  }
  
  // First byte of payload is channel
  uint8_t ch = payload[0];
  if (ch >= CHANNEL_COUNT) {
    sendError(ERR_INVALID_CHANNEL, "CHANNEL_INDEX_OUT_OF_RANGE");
    return;
  }
  
  // Calculate number of LEDs in this payload
  uint16_t ledsInPayload = (len - 1) / componentsPerPixel;

  // Check if we're trying to write beyond the allocated LEDs
  if (ledsInPayload > ledsPerChannel) {
    #if DEBUG
    Serial.println("DEBUG: PAYLOAD_TOO_LARGE_IGNORED");
    #endif
    return;
  }
  
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
    sendError(ERR_SYSTEM_NOT_INITIALIZED, "LED_SYSTEM_NOT_READY");
    return;
  }
  
  if (len < (isRGBW ? 5 : 4)) {
    sendError(ERR_PAYLOAD_TOO_SHORT, "FILL_COLOR_PAYLOAD_TOO_SHORT");
    return;
  }
  
  // Channel
  uint8_t ch = payload[0];

  if (!isValidChannel(ch)) {
    sendError(ERR_INVALID_CHANNEL, "CHANNEL_INDEX_OUT_OF_RANGE");
    return;
  }

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
    sendError(ERR_SYSTEM_NOT_INITIALIZED, "LED_SYSTEM_NOT_READY");
    return;
  }

  frameReady = true; // Tells the main loop() to update

}

bool isValidChannel(uint8_t ch) {
  return ch < CHANNEL_COUNT || ch == BROADCAST_CHANNEL;
}

bool isValidColorOrder(uint8_t order) {
  return order < COLOR_ORDER_COUNT;
}

bool isValidSpeed(uint8_t speed) {
  return speed == WS2811_800kHz || speed == WS2811_400kHz || speed == WS2813_800kHz;
}

void resetParser() {
  state = ParseState::WAIT_SOF;
  payloadIndex = 0;
  len = 0;
  checksumState = 0;
  chkL = 0;
}
