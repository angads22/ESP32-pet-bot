/*
  ESP32-CAM Brain Firmware (Everything Except Display)
  ----------------------------------------------------
  Purpose:
  - Runs robot "brain" logic on ESP32-CAM.
  - Handles vision + state machine + (future) motor + audio decisions.
  - Sends face/expression commands to ESP32-C6 display board over UART.

  UART output to display board:
  - FACE,IDLE
  - FACE,HAPPY
  - FACE,SEARCH
  - FACE,CURIOUS
  - FACE,SLEEP
  - BLINK

  NOTE:
  This file includes placeholders for motor/audio control so you can wire
  TB6612FNG + MAX98357A later without touching display firmware.
*/

#include <Arduino.h>

// UART from ESP32-CAM -> ESP32-C6 display board
static constexpr int PIN_UART_TX_TO_DISPLAY = 1;
static constexpr int PIN_UART_RX_FROM_DISPLAY = 3;
static constexpr uint32_t UART_BAUD = 115200;

// TODO: replace with your actual motor pins on ESP32-CAM carrier/wiring.
static constexpr int PIN_MOTOR_AIN1 = 12;
static constexpr int PIN_MOTOR_AIN2 = 13;
static constexpr int PIN_MOTOR_BIN1 = 14;
static constexpr int PIN_MOTOR_BIN2 = 15;
static constexpr int PIN_MOTOR_PWMA = 2;
static constexpr int PIN_MOTOR_PWMB = 4;

enum class RobotState : uint8_t {
  IDLE,
  CURIOUS,
  DRIVE,
  SEARCH,
  HAPPY,
  SLEEP
};

RobotState state = RobotState::IDLE;
uint32_t stateEnteredMs = 0;
uint32_t lastSeenMs = 0;
uint32_t lastFacePushMs = 0;

// Vision data (replace `runVisionStep()` with real detection pipeline).
bool targetSeen = false;
int targetX = 0;      // normalized-ish center offset
int targetSize = 0;   // larger means closer

void sendDisplay(const String& msg) {
  Serial1.println(msg);
}

void pushFaceForState(RobotState s) {
  switch (s) {
    case RobotState::HAPPY:
      sendDisplay("FACE,HAPPY");
      break;
    case RobotState::SEARCH:
      sendDisplay("FACE,SEARCH");
      break;
    case RobotState::CURIOUS:
    case RobotState::DRIVE:
      sendDisplay("FACE,CURIOUS");
      break;
    case RobotState::SLEEP:
      sendDisplay("FACE,SLEEP");
      break;
    case RobotState::IDLE:
    default:
      sendDisplay("FACE,IDLE");
      break;
  }
}

void setMotorRaw(int ain1, int ain2, int bin1, int bin2, int pwmA, int pwmB) {
  // Direction pins
  digitalWrite(PIN_MOTOR_AIN1, ain1);
  digitalWrite(PIN_MOTOR_AIN2, ain2);
  digitalWrite(PIN_MOTOR_BIN1, bin1);
  digitalWrite(PIN_MOTOR_BIN2, bin2);

  // PWM placeholders. For a production build, migrate to LEDC.
  analogWrite(PIN_MOTOR_PWMA, constrain(pwmA, 0, 255));
  analogWrite(PIN_MOTOR_PWMB, constrain(pwmB, 0, 255));
}

void stopMotors() {
  setMotorRaw(LOW, LOW, LOW, LOW, 0, 0);
}

void turnLeft(int speed = 120) {
  setMotorRaw(LOW, HIGH, HIGH, LOW, speed, speed);
}

void turnRight(int speed = 120) {
  setMotorRaw(HIGH, LOW, LOW, HIGH, speed, speed);
}

void driveForward(int speed = 140) {
  setMotorRaw(HIGH, LOW, HIGH, LOW, speed, speed);
}

void playBootSound() {
  // Placeholder for MAX98357A / I2S chirp.
}

void playHappySound() {
  // Placeholder for MAX98357A / I2S chirp.
}

void setState(RobotState nextState) {
  if (state == nextState) return;

  state = nextState;
  stateEnteredMs = millis();
  pushFaceForState(state);

  if (state == RobotState::HAPPY) {
    playHappySound();
    sendDisplay("BLINK");
  }
}

void runVisionStep() {
  // Demo vision simulator so behavior is visible before full CV integration.
  // Replace this with real camera-based detection/tracking.
  const uint32_t now = millis();
  const uint32_t phase = (now / 3000UL) % 4UL;

  if (phase == 0 || phase == 1) {
    targetSeen = true;
    targetX = (phase == 0) ? -20 : 20;
    targetSize = (phase == 1) ? 65 : 40;
    lastSeenMs = now;
  } else {
    targetSeen = false;
    targetX = 0;
    targetSize = 0;
  }
}

void tickStateMachine() {
  const uint32_t now = millis();
  const bool staleTarget = (now - lastSeenMs) > 1200;

  if (targetSeen) {
    if (targetSize > 60) {
      setState(RobotState::HAPPY);
      stopMotors();
      return;
    }

    if (abs(targetX) > 10) {
      setState(RobotState::CURIOUS);
      if (targetX < 0) {
        turnLeft(110);
      } else {
        turnRight(110);
      }
      return;
    }

    setState(RobotState::DRIVE);
    driveForward(135);
    return;
  }

  if (staleTarget) {
    setState(RobotState::SEARCH);
    turnLeft(90);

    // Deep idle fallback after long search.
    if ((now - stateEnteredMs) > 8000 && state == RobotState::SEARCH) {
      setState(RobotState::IDLE);
      stopMotors();
    }
    return;
  }

  setState(RobotState::IDLE);
  stopMotors();
}

void setupMotorPins() {
  pinMode(PIN_MOTOR_AIN1, OUTPUT);
  pinMode(PIN_MOTOR_AIN2, OUTPUT);
  pinMode(PIN_MOTOR_BIN1, OUTPUT);
  pinMode(PIN_MOTOR_BIN2, OUTPUT);
  pinMode(PIN_MOTOR_PWMA, OUTPUT);
  pinMode(PIN_MOTOR_PWMB, OUTPUT);
  stopMotors();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX_FROM_DISPLAY, PIN_UART_TX_TO_DISPLAY);

  setupMotorPins();
  playBootSound();

  stateEnteredMs = millis();
  lastSeenMs = millis();
  pushFaceForState(state);
  sendDisplay("BLINK");
}

void loop() {
  runVisionStep();
  tickStateMachine();

  const uint32_t now = millis();
  if (now - lastFacePushMs > 1000) {
    pushFaceForState(state);
    lastFacePushMs = now;
  }

  delay(20);
}
