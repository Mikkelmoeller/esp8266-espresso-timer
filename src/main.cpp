// Low-power code mostly taken over from the excellent example at
// https://github.com/esp8266/Arduino/blob/e920564b8d6e8436f42ae6e48f40f67b42ab0d25/libraries/esp8266/examples/LowPowerDemo/LowPowerDemo.ino

#include <Arduino.h>
#include <Wire.h>
#include "SH1106Wire.h"
#include "OLEDDisplayUi.h"
#include <ESP8266WiFi.h>
#include <EspProwl.h>

// Copy over "_constants.h.example" to "_constants.h" and update it with values suitable
//	for your network
#include "_constants.h"
#include "font.h"

#define I2C_SDA 4
#define I2C_SCL 5
#define SHOT_BUTTON_PIN 12
#define HEATING_BUTTON_PIN 13

#define DEBUG 0

SH1106Wire display(0x3c, I2C_SDA, I2C_SCL);
OLEDDisplayUi ui ( &display );

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
WiFiClient espClient;

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.persistent(false); // don't store the connection each time to save wear on the flash
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(10); // reduce RF output power, increase if it won't connect

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    yield();
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  WiFi.setAutoReconnect(true);

  EspProwl.begin();
  EspProwl.setApiKey((char *)PROWL_KEY);
  EspProwl.setApplicationName((char *)PROWL_APP);
}

int timerMode = 0; // 1 = shot timer; 2 = heating timer
int slowAnimationState = 0;
int fastAnimationState = 0;
bool done = false;
bool pushHeatingComplete = false;

const int heatingTime = 900000; // Up to 15 min
const int shotTimer = 30000; // Up to 30 sec
const int slowAnimationDuration = 1000;
const int fastAnimationDuration = 120;

unsigned long timerStart = 0;
unsigned long lastSlowAnimation = 0;
unsigned long lastFastAnimation = 0;
unsigned long doneSince = 0;
unsigned long doneTillStandby = 0;

void timeFrame(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display->setFont(Roboto_Mono_Light_14);

  unsigned long now = millis();
  unsigned long elapsedMs = now - timerStart;
  if (now - lastFastAnimation > fastAnimationDuration) {
    fastAnimationState++;
    if (fastAnimationState > 1) {
      fastAnimationState = 0;
    }
    
    lastFastAnimation = now;

    if (now - lastSlowAnimation > slowAnimationDuration) {
      slowAnimationState++;
      if (slowAnimationState > 3) {
        slowAnimationState = 0;
      }
      
      lastSlowAnimation = now;
    }
  }

  int elapsed = elapsedMs / 10;
  float elapsedSec = (float)elapsed / 100;
  int elapsedMin;
  String zeroIfNeeded = "";
  String textDecoration = "";
  switch (slowAnimationState) {
    case 0:
      textDecoration = "";
      break;
    case 1:
      textDecoration = "~";
      break;
    case 2:
      textDecoration = "~~";
      break;
    case 3:
      textDecoration = "~~~";
      break;
  }

  switch (timerMode) {
    case 1:
      display->drawString(64, 12, textDecoration + " Shot " + textDecoration);
      display->setFont(Roboto_Thin_24);

      if (elapsedMs >= shotTimer) {
        if (!done) {
          done = true;
          doneSince = now;
          doneTillStandby = 10000;
        }
        // Time's up: Flash!
        if (fastAnimationState == 0) {
          break;
        }
        elapsedSec = shotTimer/ 1000;
      }
      display->drawString(64, 40, String(elapsedSec));
      break;
    case 2:
      display->drawString(64, 12, textDecoration + " Heating " + textDecoration);
      display->setFont(Roboto_Thin_24);

      if (elapsedMs > heatingTime) {
        if (!done) {
          done = true;
          doneSince = now;
          doneTillStandby = heatingTime;
          pushHeatingComplete = true;
          display->drawString(64, 40, "Complete");
          break;
        }
        // Time's up: Flash!
        if (fastAnimationState == 0) {
          break;
        }
        display->drawString(64, 40, "Complete");
        break;
      }

      elapsedMin = (int)elapsedSec / 60;
      elapsedSec = elapsedSec - elapsedMin * 60;
      if (elapsedSec < 10) {
        zeroIfNeeded = "0";
      }
      display->drawString(64, 40, String(elapsedMin) + ":" + zeroIfNeeded + String(elapsedSec));
      break;
    default:
      return;
  }
}

unsigned long volatile timeOfLastInterrupt = 0;
int volatile targedTimerMode = -1;
bool volatile reset = false;

const int minTimeBetweenInterrupts = 200;

void handleButtonPush(int targetTimerMode) {
  noInterrupts();
  unsigned long currentTime = millis();
  if ((currentTime - timeOfLastInterrupt) > minTimeBetweenInterrupts) {
    timeOfLastInterrupt = currentTime;
    reset = true;
    targedTimerMode = targetTimerMode;
  }
  interrupts();
}

void ICACHE_RAM_ATTR shotButtonPushed() {
  handleButtonPush(1);
}

void ICACHE_RAM_ATTR heatingButtonPushed() {
  handleButtonPush(2);
}

FrameCallback frames[] = { timeFrame };
const int frameCount = 1;

void setup() {
  Serial.begin(9600);
  Serial.println("wow");

  pinMode(SHOT_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(SHOT_BUTTON_PIN, shotButtonPushed, FALLING);
  pinMode(HEATING_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(HEATING_BUTTON_PIN, heatingButtonPushed, FALLING);

  ui.setTargetFPS(60);
  ui.setFrames(frames, frameCount);
  ui.disableAllIndicators();
  ui.init();
  display.flipScreenVertically();

  setup_wifi();
}

void loop() {
  if (timerMode != targedTimerMode) {
    Serial.print("Changing timer mode to ");
    Serial.println(targedTimerMode);
    timerMode = targedTimerMode;
    if (timerMode == 0) {
      display.displayOff();
    } else {
      display.displayOn();
    }
  }

  if (reset) {
    Serial.println("Reset");
    timerStart = millis();
    reset = false;
    done = false;
    doneSince = 0;
    doneTillStandby = 0;
  }

  if (done) {
    const unsigned long now = millis();
    if (now - doneSince > doneTillStandby) {
      // Standby
      targedTimerMode = 0;
      done = false;
      doneSince = 0;
      doneTillStandby = 0;
      return; // To force immediate targetTimerMode evaluation
    }
  }

  if (pushHeatingComplete) {
    pushHeatingComplete = false;
    int returnCode = EspProwl.push((char *)"", (char *)"Heating Complete", 0);
    if (returnCode == 200) {
      if (DEBUG) Serial.println("OK.");
    } else {
      if (DEBUG) Serial.print("Error. Server returned: ");
      if (DEBUG) Serial.print(returnCode);
    }
  }

  int remainingTimeBudget = ui.update();
  if (DEBUG) Serial.println(remainingTimeBudget);
  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }
}
