#include <Adafruit_NeoPixel.h>

/**
 * Compile time configuration.
 */

// #define DEBUG 1
#define SIMPLE 0

static uint32_t kOutputPIN = 2;
static uint32_t kNumberOfPixels = 7;

#if defined(DEBUG)
  #define DEBUG_PRINT(str)                      \
    Serial.print(millis());                     \
    Serial.print(": ");                         \
    Serial.print(__PRETTY_FUNCTION__);          \
    Serial.print(' ');                          \
    Serial.print(__FILE__);                     \
    Serial.print(':');                          \
    Serial.print(__LINE__);                     \
    Serial.print(' ');                          \
    Serial.println(str);
#else
 #define DEBUG_PRINT(x)
#endif

static uint32_t kWipeDelay = 5;

static uint32_t kSlowCylonTime = 1000;
static uint32_t kFastCylonTime = 500;

static float   kCylonWidth = 3.5;
static int32_t kInvertedCylonWidth = 4;

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_RGB     Pixels are wired for RGB bitstream
//   NEO_GRB     Pixels are wired for GRB bitstream
//   NEO_KHZ400  400 KHz bitstream (e.g. FLORA pixels)
//   NEO_KHZ800  800 KHz bitstream (e.g. High Density LED strip)
Adafruit_NeoPixel strip =
    Adafruit_NeoPixel(kNumberOfPixels, kOutputPIN, NEO_GRB + NEO_KHZ800);

/**
 * Modes
 *
 * The sets of actions that can happen, and a routine to dispatch according to
 * the current mode.
 */

typedef enum Mode {
  kWipe = 0,
  kCylonColor,
  kCylonColorInvert,
  kCylonStaticRainbow,
  kCylonDynamicRainbow,
  kCylonShiftingRainbow,
  kTwinkle,
  kTwinkleWhite,

  kMaxMode // Must always be last.
} Mode;


/**
 * Color Targets
 *
 * The set of colors that many modes will use. Not all modes use these (e.g.,
 * the rainbow modes will ignore them).
 */

typedef enum Color {
  kRed = 0,
  kOrange,
  kYellow,
  kGreen,
  kAqua,
  kBlue,
  kIndigo,
  kViolet,
  kWhite,

  kMaxColor // Must always be last.
} Color;

uint32_t colorToValue(Color color);

/**
 * Speeds
 *
 * For modes that have multiple possible speeds.
 */

typedef enum Speed {
  kVerySlow,
  kSlow,
  kNormal,
  kFast,
  kVeryFast,

  kMaxSpeed // Must always be last.
} Speed;

uint32_t speedToValue(Speed speed);

typedef enum Direction {
  kForward,
  kBackward,

  kMaxDirection // Must always be last.
} Direction;

/**
 * State
 *
 * Encapsulates the union of interesting state across all modes.
 */

typedef struct {
  Mode mode;
  Color targetColor;
  Speed speed;
  Direction direction;
  uint32_t dispatchTimeMillis;
  uint32_t loopStartTimeMillis;
  uint32_t durationMillis;
} State;

State currentState;

/**
 * Forward declarations.
 */

/**
 * Central dispatch routine, based on values in the state. Sets
 * dispatchTimeMillis.
 */
void dispatch(State state);

/**
 * Should the mode main loop continue, or is the mode time over? Should be
 * polled reasonably frequently.
 */
boolean continueLoop(State state);

/** Wait out the rest of the mode time before returning. */
void modeDelay(State state);

/**
 * The loop must set state.loopStartTimeMillis at the start of the loop in
 * order to use this function at the end.
 */
void loopDelay(State state);

/** Mode routines */
void colorWipe(State state);
void cylon(State state);
void twinkle(State state);

/**
 * Arduino Programming API
 */

void setup() {
  Serial.begin(9600);
  while (!Serial)
    ;
  Serial.println("====================");
  Serial.println("7 NeoPixel Jewel for fiber optic tree. " __DATE__ " " __TIME__);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

#if DEBUG
  randomSeed(0);
#else
  randomSeed(analogRead(0));
#endif
}

void loop() {
  static State state;
#if 1
  state.mode = (Mode)random(kMaxMode);
  state.targetColor = (Color)random(kMaxColor);
  state.speed = (Speed)random(kMaxSpeed);
  state.direction = (Direction)random(kMaxDirection);
  state.durationMillis = random(2, 10) * 1000;
  dispatch(state);
#else
  for (uint32_t i = kTwinkle ; i < kMaxMode ; ++i) {
    state.mode = kWipe;
    state.targetColor = (Color)random(kMaxColor);
    state.speed = kSlow;
    state.direction = kForward;
    state.durationMillis = 1000;
    dispatch(state);

    Serial.println(i);

    state.mode = (Mode)i;
    state.targetColor = kRed;
    state.speed = kFast;
    state.durationMillis = 10000;
    dispatch(state);
  }
#endif
}

/**
 * Core functionality.
 *
 * Take a state and dispatch according to the current mode in the state.
 */

void dispatch(State state) {
  state.dispatchTimeMillis = millis();
  state.loopStartTimeMillis = 0;

  DEBUG_PRINT(String("now: ") + String(state.dispatchTimeMillis) +
              String(" duration: ") + String(state.durationMillis));

  switch (state.mode) {
    case kWipe:
      colorWipe(state);
      break;
    case kCylonColor:
    case kCylonColorInvert:
    case kCylonStaticRainbow:
    case kCylonDynamicRainbow:
    case kCylonShiftingRainbow:
      cylon(state);
      break;
    case kTwinkle:
    case kTwinkleWhite:
      twinkle(state);
      break;
    default:
      DEBUG_PRINT("Unknown mode.");
      break;
  }
}

/**
 * Move a key spot backwards or forwards across the pixels, wrapping at the
 * end. Target a specific color or method of generating a rainbow. |loopspeed|
 * sets how long each loop takes, |numberofpasses| sets how many loops are
 * done.
 */
void cylon(State state) {
  uint16_t rainbowBase = 0; // arbitrary counters
  uint16_t rainbowIncrement = 32 / kNumberOfPixels;
  rainbowIncrement = max(rainbowIncrement, 1);
  int16_t leadpos = 0;

  int32_t cylonWidth = (state.mode == kCylonColorInvert) ? kInvertedCylonWidth : kCylonWidth;
  boolean affects_whole_string = (state.mode != kCylonColor);

  uint32_t targetColor = colorToValue(state.targetColor);

  if (state.mode == kCylonColorInvert) {
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, targetColor);
    }
    strip.show();
  }

  while (continueLoop(state)) {
    state.loopStartTimeMillis = millis();

    // counter for dynamic rainbows
    rainbowBase = (rainbowBase + rainbowIncrement) % 256;

    for (int16_t i = 0; i < strip.numPixels() ; ++i) {
      uint32_t c = targetColor;
      uint32_t rainbowShift = i;

      switch (state.mode) {
        // color modes that change accross strip
        case kCylonStaticRainbow:
          c = Wheel(((i * 256 / strip.numPixels())) & 255);
          break;
        case kCylonDynamicRainbow:
          rainbowShift = 256 / kNumberOfPixels * i;
          c = Wheel((rainbowShift + rainbowBase) & 255);
          break;
        case kCylonShiftingRainbow:
          c = Wheel(rainbowBase);
          break;
        default:
          break;
      }

      int32_t distance1 = leadpos - i;
      distance1 = abs(distance1);

      int32_t distance2 = leadpos + kNumberOfPixels - i;
      distance2 = abs(distance2);

      int32_t distance3 = i + kNumberOfPixels - leadpos;
      distance3 = abs(distance3);

      int32_t distance = min(distance1, distance2);
      distance = min(distance, distance3);

      if (state.mode == kCylonColorInvert) {
        distance = max(0, cylonWidth - distance);
      }
      if (distance > cylonWidth) {
        c = 0;
      } else if (distance) {
        int32_t percent = 100 * (cylonWidth - distance) / cylonWidth;
        c = fadeColor(c, percent);
      }

      strip.setPixelColor(i, c);
    } // end for loop
    strip.show();

    leadpos = (state.direction == kForward)
        ? (leadpos + 1) % kNumberOfPixels
        : (leadpos - 1 + kNumberOfPixels) % kNumberOfPixels;

    loopDelay(state);
  } // end of while loop
}

/**
 * Wipe the LEDs one after another to a target color.
 */
void colorWipe(State state) {
  uint32_t wipeDelay = state.durationMillis / strip.numPixels();
  uint32_t color = colorToValue(state.targetColor);
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    uint16_t pixelNum = (state.direction == kForward)
        ? i
        : strip.numPixels() - i - 1;
    strip.setPixelColor(pixelNum, color);
    strip.show();
    if (wipeDelay) {
      delay(wipeDelay);
    }
  }
}

/**
 * Twinkle the lights, either with full on of the specific color, or in white.
 */
void twinkle(State state) {
  uint32_t targetColor = colorToValue(state.targetColor);

  while (continueLoop(state)) {
    state.loopStartTimeMillis = millis();

    for (int16_t i = 0; i < strip.numPixels() ; ++i) {
      uint32_t c = targetColor;

      uint32_t percent = random(1, 30);
      c = fadeColor(c, percent);
      strip.setPixelColor(i, c);
    }
    strip.show();

    loopDelay(state);

    if (random(100) > 70) {
      uint32_t c = (state.mode == kTwinkleWhite)
          ? colorToValue(kWhite)
          : targetColor;

      uint32_t pixel = random(0, strip.numPixels());
      strip.setPixelColor(pixel, c);
      strip.show();
    }
  }

}


/**
 * Utility methods.
 */

/** Return a packed RGB value for a specified Color. */
uint32_t colorToValue(Color color) {
  switch (color) {
    case kRed:
      return 0xFF0000;
    case kOrange:
      return 0xFF5500;
    case kYellow:
      return 0xFFFF00;
    case kGreen:
      return 0x00FF00;
    case kAqua:
      return 0x00FFFF;
    case kBlue:
      return 0x0000FF;
    case kIndigo:
      return 0x3300FF;
    case kViolet:
      return 0xFF00FF;
    case kWhite:
      return 0xFFFFFF;
    default:
      DEBUG_PRINT("Unknown color target");
      break;
  }
  return 0;
}

/**
 * Return the number of millis() that a loop should be according to the
 * specified speed.
 */
uint32_t speedToValue(Speed speed) {
  switch (speed) {
    default:
      //    case kGlacial:
      //      return 1000;
    case kVerySlow:
      return 500;
    case kSlow:
      return 200;
    case kNormal:
      return 100;
    case kFast:
      return 66;
    case kVeryFast:
      return 33;
      //    case kHyper:
      //      return 16;
  }
}

boolean continueLoop(State state) {
  uint32_t currentDuration = millis() - state.dispatchTimeMillis;
  DEBUG_PRINT(currentDuration);
  return currentDuration < state.durationMillis;
}

void modeDelay(State state) {
  delay(state.durationMillis - (millis() - state.dispatchTimeMillis));
}

void loopDelay(State state) {
  if (state.loopStartTimeMillis) {
    uint32_t desiredLoopTime = speedToValue(state.speed);

    // In case the mode is near the end of it's time, clip it to the desired
    // overall mode time.
    uint32_t loopTime = millis() - state.loopStartTimeMillis;
    uint32_t modeDelayTime = state.durationMillis - (millis() - state.dispatchTimeMillis);
    uint32_t delayTime = desiredLoopTime < loopTime ?
        0 :
        min(desiredLoopTime - loopTime, modeDelayTime);
    if (delayTime) {
      DEBUG_PRINT(delayTime);
      delay(delayTime);
    }
  }
}

uint32_t fadeColor(uint32_t c, uint8_t percent) {
  percent = max(0, min(100, percent));
  uint8_t r, g, b;
  r = (uint8_t)(c >> 16);
  g = (uint8_t)(c >> 8);
  b = (uint8_t)c;

  r = (r * percent / 100);
  g = (g * percent / 100);
  b = (b * percent / 100);

  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void setAll(uint32_t c) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

void rainbow(uint8_t wait, uint32_t number_of_cycles) {
  uint16_t i, j;

  for (j = 0; j < 256 * number_of_cycles; j++) {
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait, uint32_t number_of_cycles) {
  uint16_t i, j;

  for (j = 0; j < 256 * number_of_cycles;
       j++) { // 5 cycles of all colors on wheel
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if (WheelPos < 85) {
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}
