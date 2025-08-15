// Modified Arduino firmware for A4988 stepper and Micro-Manager compatibility
// Non-blocking stepper control with proper status reporting
// Stand-alone stepper control

// Motor configuration
#define MOTOR_STEPS 200     // Steps per revolution
#define MICROSTEPS 1        // Microstepping

// Pin definitions for A4988
#define DIR_PIN 4
#define STEP_PIN 7
#define ENABLE_PIN 8

String cmd = "";
float z = 0.0;
long stepPosition = 0;        // Current position in steps
float stepsPerUnit = 100.0;   // Steps per micrometer (adjust for your setup)

// Non-blocking movement variables
bool isMoving = false;
long moveStepsRemaining = 0;
int moveDirection = 1;
unsigned long lastStepTime = 0;
unsigned long stepIntervalMicros = 5000; // Default: 200 steps/sec

void setup() {
  Serial.begin(9600);
  
  // Configure stepper pins
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW); // Enable motor (LOW = enabled for A4988)
  
  delay(1000);
  
  reply("Vers:LS");
}

void loop() {
  // Handle serial commands - HIGHEST PRIORITY
  if (Serial.available()) {
    cmd = Serial.readStringUntil('\r');
    processCommand(cmd);
    cmd = "";
  }

  // Non-blocking stepper movement
  handleStepperMovement();
}

void processCommand(String s) {
  if (s.startsWith("?ver")) {
    reply("Vers:LS");
  } else if (s.startsWith("!autostatus 0")) {
    // Just acknowledge
  } else if (s.startsWith("?det")) {
    reply("60");
  } else if (s.startsWith("?pitch z")) {
    reply("50.0");
  } else if (s.startsWith("?vel z")) {
    reply("100.0");
  } else if (s.startsWith("?accel z")) {
    reply("1.0");
  } else if (s.startsWith("!dim z 1")) {
    // Acknowledge dimension command
  } else if (s.startsWith("!dim z 2")) {
    // Acknowledge dimension command
  } else if (s.startsWith("?statusaxis")) {
    // CRITICAL: Return 'M' if moving, '@' if ready
    reply(isMoving ? "M" : "@");
  } else if (s.startsWith("!vel z")) {
    // Set velocity
    String speedStr = s.substring(s.indexOf('z') + 1);
    float speed = speedStr.toFloat();
    setMotorSpeed(speed);
  } else if (s.startsWith("!accel z")) {
    // Acknowledge acceleration command
  } else if (s.startsWith("?pos z")) {
    // Return current position
    String zs = String(z, 1);
    reply(zs);
  } else if (s.startsWith("?lim z")) {
    reply("0.0 100.0");
  } else if (s.startsWith("!pos z")) {
    // Set origin
    String posStr = s.substring(s.indexOf('z') + 1);
    if (posStr.length() > 0) {
      z = posStr.toFloat();
      stepPosition = (long)(z * stepsPerUnit);
    }
  } else if (s.startsWith("?status")) {
    reply("OK...");
  } else if (s.startsWith("!dim z 0")) {
    // Acknowledge dimension command
  } else if (s.startsWith("!speed z")) {
    // Acknowledge speed command
  } else if (s.startsWith("!mor z")) {
    // Relative move - NON-BLOCKING
    String delta = s.substring(s.indexOf('z') + 1);
    float deltaZ = delta.toFloat();
    z += deltaZ;
    startMove(deltaZ);
    // Reply immediately - don't wait for movement to complete
  } else if (s.startsWith("!moa z")) {
    // Absolute move - NON-BLOCKING
    String apos = s.substring(s.indexOf('z') + 1);
    float targetZ = apos.toFloat();
    float deltaZ = targetZ - z;
    z = targetZ;
    startMove(deltaZ);
    // Reply immediately - don't wait for movement to complete
  } else if (s.startsWith("?err")) {
    reply("0");
  }
}

void reply(String s) {
  Serial.print(s);
  Serial.print("\r");
  Serial.flush(); // Ensure immediate transmission
}

void setMotorSpeed(float speed) {
  // Convert speed to step interval (microseconds)
  // Adjust this mapping based on your requirements
  float stepsPerSecond = constrain(speed, 10, 1000);
  stepIntervalMicros = (unsigned long)(1000000.0 / stepsPerSecond);
}

void startMove(float deltaZ) {
  long deltaSteps = (long)(deltaZ * stepsPerUnit);
  if (deltaSteps == 0) {
    return;
  }
  
  // Stop any current movement
  isMoving = true;
  moveDirection = (deltaSteps > 0) ? 1 : -1;
  moveStepsRemaining = abs(deltaSteps);
  lastStepTime = micros();
}

void handleStepperMovement() {
  if (!isMoving) return;
  
  unsigned long currentTime = micros();
  if (currentTime - lastStepTime >= stepIntervalMicros) {
    lastStepTime = currentTime;
    
    // Set direction
    digitalWrite(DIR_PIN, (moveDirection > 0) ? HIGH : LOW);
    
    // Step pulse
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(2); // Minimum pulse width for A4988
    digitalWrite(STEP_PIN, LOW);
    
    // Update position
    stepPosition += moveDirection;
    moveStepsRemaining--;
    
    // Check if movement complete
    if (moveStepsRemaining <= 0) {
      isMoving = false;
    }
  }
}

/*
Command reference:
"?ver"   ->   Vers:LS
"?det"   ->   60  (device type)
"?statusaxis" -> @ if ready, M if busy
"!autostatus 0"
"?pitch z" -> 50.0
"!dim z 2" set to microns
"!dim z 1"
"?vel z"  -> 100.0
"!vel z 100" set velocity
"?accel z" -> 1.0
"!accel z 1" set acceleration
"!moa z 10"  move z to absolute position 10
"?err"  return 0
"!mor z 5" move z by relative amount 5
"?pos z" query current z position
"?lim z" query limits -> 0.0 100.0
"!pos z 0" set origin
"?status"  -> OK...
"!dim z 0" set as steps
"!speed z 10" set speed
*/
