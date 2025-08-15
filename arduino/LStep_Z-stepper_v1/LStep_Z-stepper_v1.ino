// Modified Arduino firmware for A498x stepper and Micro-Manager compatibility
// Non-blocking stepper control with proper status reporting using laurb9/StepperDriver
// version 0.7 - Uses startMove()/nextAction() non-blocking API
// Modified for stepper motor 2025

#include <Arduino.h>
#include "A4988.h"  // Use A4988 class from laurb9/StepperDriver

// Motor configuration
#define MOTOR_STEPS 200     // Full steps per revolution
#define MICROSTEPS 1        // Microstepping (set per your hardware jumpers)

// Pin definitions for A4988-like drivers
#define DIR_PIN 4
#define STEP_PIN 7
#define ENABLE_PIN 8
// Optional microstep pins (not required if jumpers fixed); pass -1 if unused
#define MS1_PIN -1
#define MS2_PIN -1
#define MS3_PIN -1

// Create stepper instance (without MS pins if unused)
#if (MS1_PIN < 0 || MS2_PIN < 0 || MS3_PIN < 0)
A4988 stepper(MOTOR_STEPS, DIR_PIN, STEP_PIN);
#else
A4988 stepper(MOTOR_STEPS, DIR_PIN, STEP_PIN, MS1_PIN, MS2_PIN, MS3_PIN);
#endif

String cmd = "";
float z = 0.0;
long stepPosition = 0;        // Current position in steps (software tracking)
float stepsPerUnit = 100.0;   // Steps per micrometer (adjust to your mechanics)

// Non-blocking movement state (library-driven)
bool isMoving = false;
// Library returns time (micros) until next action; we store it to schedule nextAction()
unsigned long nextActionDueMicros = 0;
// Cached speed settings
float currentRPM = 60.0;      // RPM for constant speed
int currentMicrosteps = MICROSTEPS;

void setup() {
  Serial.begin(9600);

  // Initialize driver
  stepper.begin(currentRPM, currentMicrosteps);  // sets speed and microstepping [7]
  // Enable outputs
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW); // LOW=enable on common A4988 boards

  // Ensure idle state
  delay(1000);
  reply("Vers:LS");
}

void loop() {
  // Handle serial commands first
  if (Serial.available()) {
    cmd = Serial.readStringUntil('\r');
    processCommand(cmd);
    cmd = "";
  }

  // Non-blocking stepping via nextAction()
  handleStepperMovement();
}

void processCommand(String s) {
  if (s.startsWith("?ver")) {
    reply("Vers:LS");
  } else if (s.startsWith("!autostatus 0")) {
    // Acknowledge
  } else if (s.startsWith("?det")) {
    reply("60");
  } else if (s.startsWith("?pitch z")) {
    reply("50.0");
  } else if (s.startsWith("?vel z")) {
    reply("100.0");
  } else if (s.startsWith("?accel z")) {
    reply("1.0");
  } else if (s.startsWith("!dim z 1")) {
    // Acknowledge
  } else if (s.startsWith("!dim z 2")) {
    // Acknowledge
  } else if (s.startsWith("?statusaxis")) {
    reply(isMoving ? "M" : "@");
  } else if (s.startsWith("!vel z")) {
    // Set velocity (map to RPM)
    String speedStr = s.substring(s.indexOf('z') + 1);
    float speed = speedStr.toFloat();
    setMotorSpeed(speed);
  } else if (s.startsWith("!accel z")) {
    // Not implemented (library supports acceleration in other modes)
  } else if (s.startsWith("?pos z")) {
    String zs = String(z, 1);
    reply(zs);
  } else if (s.startsWith("?lim z")) {
    reply("0.0 100.0");
  } else if (s.startsWith("!pos z")) {
    // Set origin (sync software position)
    String posStr = s.substring(s.indexOf('z') + 1);
    if (posStr.length() > 0) {
      z = posStr.toFloat();
      stepPosition = (long)(z * stepsPerUnit);
      // Align library's internal pos by stopping and not relying on its pos store
      stepper.stop(); // ensure no residual move queued [7]
      isMoving = false;
    }
  } else if (s.startsWith("?status")) {
    reply("OK...");
  } else if (s.startsWith("!dim z 0")) {
    // Acknowledge steps mode if used
  } else if (s.startsWith("!speed z")) {
    // Alias/acknowledge
  } else if (s.startsWith("!mor z")) {
    // Relative move - NON-BLOCKING
    String delta = s.substring(s.indexOf('z') + 1);
    float deltaZ = delta.toFloat();
    z += deltaZ;
    startRelativeMove(deltaZ);
  } else if (s.startsWith("!moa z")) {
    // Absolute move - NON-BLOCKING
    String apos = s.substring(s.indexOf('z') + 1);
    float targetZ = apos.toFloat();
    float deltaZ = targetZ - z;
    z = targetZ;
    startRelativeMove(deltaZ);
  } else if (s.startsWith("?err")) {
    reply("0");
  }
}

void reply(String s) {
  Serial.print(s);
  Serial.print("\r");
  Serial.flush();
}

// Map requested velocity (steps/sec) to RPM for the library
void setMotorSpeed(float speed) {
  // Constrain requested speed in steps/sec
  float stepsPerSecond = constrain(speed, 10.0, 4000.0);
  // Convert to RPM considering microsteps
  // RPM = (steps/sec) / (full_steps_per_rev * microsteps) * 60
  float rpm = (stepsPerSecond / (MOTOR_STEPS * currentMicrosteps)) * 60.0;
  rpm = constrain(rpm, 1.0, 1200.0); // safe range per MCU load and driver limits
  currentRPM = rpm;
  stepper.setRPM(currentRPM); // updates internal timing [7]
}

// Queue a relative move using non-blocking API
void startRelativeMove(float deltaZ) {
  long deltaSteps = (long)(deltaZ * stepsPerUnit);
  if (deltaSteps == 0) {
    return;
  }

  // Enable outputs
  digitalWrite(ENABLE_PIN, LOW);

  // Start a non-blocking move (in steps); nextAction() must be called in loop [1]
  stepper.startMove(deltaSteps);  // positive or negative allowed [1]
  isMoving = true;

  // Prime scheduler so we call nextAction() immediately
  nextActionDueMicros = micros();
}

// Drive the motor in non-blocking fashion; schedule nextAction() precisely
void handleStepperMovement() {
  if (!isMoving) return;

  unsigned long now = micros();
  if ((long)(now - nextActionDueMicros) >= 0) {
    // nextAction() performs one scheduled action and returns time until next (in micros) [1]
    long wait = stepper.nextAction();  // <=0 indicates move complete [1]
    if (wait <= 0) {
      // Movement finished
      isMoving = false;
      // Update software position
      // stepper.startMove() consumed exactly deltaSteps; reflect z already updated on command
      stepPosition = (long)(z * stepsPerUnit);
      // Optionally disable to reduce heat:
      // digitalWrite(ENABLE_PIN, HIGH);
    } else {
      nextActionDueMicros = now + (unsigned long)wait;
      // Update software position incrementally if desired (optional):
      // We keep it simple and update at the end, to match Micro-Manager polling
    }
  }
}
