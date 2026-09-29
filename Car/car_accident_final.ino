#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <HTTPClient.h>

Adafruit_MPU6050 mpu;
Preferences preferences; 

// --- NETWORK & TIME ---
const char* ssid = ""; // WIFI Name
const char* password = ""; // WIFI PAssword
const char* mqtt_server = ""; //PC IPv4 address (Run on CMD - ipconfig)

// --- TWILIO API CREDENTIALS ---
const String TWILIO_SID     = "";
const String TWILIO_TOKEN   = "";
const String TWILIO_NUM     = ""; 

// --- VERIFIED EMERGENCY CONTACTS ---
const String CONTACT_1 = "BLANK"; 
const String CONTACT_2 = "BLANK";        
const String CONTACT_3 = "BLANK";        

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // IST UTC + 5:30
const int   daylightOffset_sec = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// --- HARDWARE PINS ---
const int trigPin = 19;
const int echoFront = 34;
const int echoBack = 35;
const int buzzerPin = 18;
const int PIN_ENA = 14; const int PIN_IN1 = 27; const int PIN_IN2 = 26;
const int PIN_IN3 = 25; const int PIN_IN4 = 33; const int PIN_ENB = 32;

// --- DYNAMICS & SENSORS ---
#define SOUND_SPEED 0.034
float distanceFront = 0.0;
float distanceBack = 0.0;
float previousDistanceFront = 100.0; 
float previousDistanceBack = 100.0; 
float distanceDrop = 0.0; 
float distanceDropB = 0.0; 
char currentCommand = 'S'; 

// --- TUNABLE PARAMETERS ---
int baseSpeed = 160;
int turnSpeed = 184;
float Kp = 6.0;  
float Kd = 2.0;  
float crashGForceF = 20.0; 
float crashGForceB = 20.0; 
bool guardianEnabled = true; 
bool sentinelMode = false; 

// --- DASHBOARD CALL ARMING, GPS & TIMER ---
bool autoCallEnabled = false;       
bool crashAlarmActive = false;
bool emergencyPending = false;      
bool emergencyDispatched = false;   
unsigned long crashStartTime = 0;
const unsigned long GRACE_PERIOD_MS = 10000; 
String lastKnownLocation = "Location Unknown"; // GPS Storage

float previousErrorF = 0.0; 
float previousErrorB = 0.0;
float crashDistance = 10.0; 
unsigned long lastTelemetryTime = 0;
float accelYOffset = 0.0;

// --- BLACK BOX DATA STRUCTURES ---
struct BlackBoxLog {
  char timeStr[10];
  float accel;
  float df;
  float db;
  char cmd;
  char action[32]; 
};

enum CrashState { BB_NORMAL, BB_CRASHING, BB_AFTERMATH, BB_SENDING, BB_LOCKED };
CrashState bbState = BB_NORMAL;

BlackBoxLog preBuffer[5];
uint8_t preIdx = 0;
BlackBoxLog crashBuffer[3];
uint8_t crashIdx = 0;
BlackBoxLog postBuffer[5];
uint8_t postIdx = 0;

String getISTTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) { return "00:00:00"; }
  char timeStringBuff[15];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

String serializeLog(BlackBoxLog b, String tag) {
  return "{\"tag\":\"" + tag + "\",\"t\":\"" + String(b.timeStr) + "\",\"accel\":" + String(b.accel,2) + ",\"df\":" + String(b.df,1) + ",\"db\":" + String(b.db,1) + ",\"cmd\":\"" + String(b.cmd) + "\",\"act\":\"" + String(b.action) + "\"}";
}

void debugLog(String message) {
  Serial.println(message); 
  if (client.connected()) client.publish("torquebeast/log", message.c_str()); 
}

void beep(int times, int durationMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(buzzerPin, HIGH); delay(durationMs); digitalWrite(buzzerPin, LOW);
    if (i < times - 1) delay(durationMs);
  }
}

void drive(int left, int right) {
  left = constrain(left, -255, 255);
  right = constrain(right, -255, 255);
  digitalWrite(PIN_IN1, left > 15 ? LOW : (left < -15 ? HIGH : LOW));
  digitalWrite(PIN_IN2, left > 15 ? HIGH : (left < -15 ? LOW : LOW));
  ledcWrite(PIN_ENA, abs(left));
  digitalWrite(PIN_IN3, right > 15 ? LOW : (right < -15 ? HIGH : LOW));
  digitalWrite(PIN_IN4, right > 15 ? HIGH : (right < -15 ? LOW : LOW));
  ledcWrite(PIN_ENB, abs(right));
}

void publishCurrentConfig() {
  String configJson = "{\"baseSpd\":" + String(baseSpeed) + 
                      ",\"turnSpd\":" + String(turnSpeed) + 
                      ",\"kp\":" + String(Kp, 1) + 
                      ",\"kd\":" + String(Kd, 1) + 
                      ",\"gF\":" + String(crashGForceF, 1) + 
                      ",\"gB\":" + String(crashGForceB, 1) + 
                      ",\"guardian\":" + (guardianEnabled ? "true" : "false") + 
                      ",\"autoCall\":" + (autoCallEnabled ? "true" : "false") + "}";
  client.publish("torquebeast/config", configJson.c_str());
}

// --- TWILIO REST API DISPATCHERS ---
void makeTwilioSMS(String targetNumber) {
  if (targetNumber == "" || targetNumber == "BLANK") return;
  HTTPClient http;
  String url = "https://api.twilio.com/2010-04-01/Accounts/" + TWILIO_SID + "/Messages.json";
  http.begin(url);
  http.setAuthorization(TWILIO_SID.c_str(), TWILIO_TOKEN.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String encTo = targetNumber; encTo.replace("+", "%2B");
  String encFrom = TWILIO_NUM; encFrom.replace("+", "%2B");
  
  // URL Encoded payload with Google Maps link injection
  String bodyText = "🚨 CRITICAL ALERT: Your Car has been crash confirmed. Emergency assistance requested.%0A%0ATrack live location here:%0A" + lastKnownLocation;
  
  // URL Encoding Fixes (Spaces must be %20)
  bodyText.replace(" ", "%20"); 
  bodyText.replace(":", "%3A"); 
  bodyText.replace("/", "%2F"); 
  bodyText.replace("?", "%3F"); 
  bodyText.replace("=", "%3D"); 
  bodyText.replace(",", "%2C"); 
  
  String payload = "To=" + encTo + "&From=" + encFrom + "&Body=" + bodyText;

  int httpResponseCode = http.POST(payload);
  if (httpResponseCode == 201) {
    debugLog("SYS: Twilio SMS sent to " + targetNumber);
  } else {
    debugLog("SYS: Twilio SMS Error (" + targetNumber + "): " + String(httpResponseCode));
  }
  http.end();
}

void makeTwilioCall(String targetNumber) {
  if (targetNumber == "" || targetNumber == "BLANK") return;
  HTTPClient http;
  String url = "https://api.twilio.com/2010-04-01/Accounts/" + TWILIO_SID + "/Calls.json";
  http.begin(url);
  http.setAuthorization(TWILIO_SID.c_str(), TWILIO_TOKEN.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String encTo = targetNumber; encTo.replace("+", "%2B");
  String encFrom = TWILIO_NUM; encFrom.replace("+", "%2B");
  String twiml = "%3CResponse%3E%3CSay%20voice%3D%22alice%22%3ECRITICAL%20ALERT.%20A%20crash%20involving%20your%20car%20has%20been%20confirmed.%20A%20live%20GPS%20map%20link%20has%20been%20sent%20to%20your%20phone%20via%20text%20message.%20Emergency%20assistance%20requested.%3C%2FSay%3E%3C%2FResponse%3E";
  String payload = "To=" + encTo + "&From=" + encFrom + "&Twiml=" + twiml;

  int httpResponseCode = http.POST(payload);
  if (httpResponseCode == 201) {
    debugLog("SYS: Twilio Voice Call placed to " + targetNumber);
  } else {
    debugLog("SYS: Twilio Call Error (" + targetNumber + "): " + String(httpResponseCode));
  }
  http.end();
}

void dispatchAllEmergencyAlerts() {
  debugLog("SYS: Grace period expired! Dispatching Twilio Alerts...");

  // Send texts with a breather and an MQTT heartbeat to prevent disconnects
  makeTwilioSMS(CONTACT_1);
  client.loop(); yield(); delay(500); 
  
  makeTwilioSMS(CONTACT_2);
  client.loop(); yield(); delay(500);
  
  makeTwilioSMS(CONTACT_3);
  client.loop(); yield(); delay(500);

  // Send Voice Calls with a 2-second gap to bypass Twilio's limits
  makeTwilioCall(CONTACT_1);
  client.loop(); yield(); delay(2000); 
  
  makeTwilioCall(CONTACT_2);
  client.loop(); yield(); delay(2000);
  
  makeTwilioCall(CONTACT_3);
  client.loop(); yield(); delay(2000);

  debugLog("SYS: All telecom alerts successfully dispatched!");
}

void resetAlarm() {
  crashAlarmActive = false; 
  emergencyPending = false;
  emergencyDispatched = false;
  digitalWrite(buzzerPin, LOW); 
  bbState = BB_NORMAL;
  preIdx = 0; crashIdx = 0; postIdx = 0;
  
  previousDistanceFront = 100.0;
  previousDistanceBack = 100.0;
  
  debugLog("SYS: Alarm Cleared & 10s Timer Aborted.");
  if (client.connected()) {
    client.publish("torquebeast/alert", "{\"status\":\"STANDBY\"}");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }

  if (String(topic) == "torquebeast/command") {
    if (message == "X") { 
      currentCommand = 'S'; 
      resetAlarm(); 
    } 
    else if (message == "C") {
      distanceFront = 100.0;
      previousDistanceFront = 100.0;
      distanceBack = 100.0;
      previousDistanceBack = 100.0;
      pinMode(trigPin, OUTPUT); 
      pinMode(echoFront, INPUT); 
      pinMode(echoBack, INPUT);
      digitalWrite(trigPin, LOW);
      debugLog("SYS: Radar sensors software reboot triggered.");
    }
    else { 
      char newCmd = message.charAt(0);
      if (crashAlarmActive) {
        if (newCmd == 'B' && distanceFront <= crashDistance) { 
          previousDistanceFront = 100.0; 
          resetAlarm(); 
        } else if (newCmd == 'F' && distanceBack <= crashDistance) { 
          previousDistanceBack = 100.0; 
          resetAlarm(); 
        }
      }
      if (!crashAlarmActive) currentCommand = newCmd;
    }
  } 
  else if (String(topic) == "torquebeast/location") {
    lastKnownLocation = message; 
  }
  else if (String(topic) == "torquebeast/tune") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) return;

    baseSpeed = doc["baseSpd"] | baseSpeed;
    turnSpeed = doc["turnSpd"] | turnSpeed;
    Kp = doc["kp"] | Kp;
    Kd = doc["kd"] | Kd;
    crashGForceF = doc["gF"] | crashGForceF;
    crashGForceB = doc["gB"] | crashGForceB;
    if (doc.containsKey("guardian")) guardianEnabled = doc["guardian"];
    if (doc.containsKey("sentinel")) sentinelMode = doc["sentinel"];
    
    if (doc.containsKey("autoCall")) {
      autoCallEnabled = doc["autoCall"];
      preferences.putBool("autoCall", autoCallEnabled);
      debugLog("SYS: Emergency Calling is now " + String(autoCallEnabled ? "ARMED" : "DISARMED"));
    }

    preferences.putInt("baseSpeed", baseSpeed);
    preferences.putInt("turnSpeed", turnSpeed);
    preferences.putFloat("Kp", Kp);
    preferences.putFloat("Kd", Kd);
    preferences.putFloat("gF", crashGForceF);
    preferences.putFloat("gB", crashGForceB);
    preferences.putBool("guardian", guardianEnabled);
    
    publishCurrentConfig(); 
  }
  else if (String(topic) == "torquebeast/req_config") { 
    publishCurrentConfig(); 
  }
}

// --- ROBUST WIFI & MQTT RECONNECT LOGIC ---
void reconnect() {
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("SYS: Wi-Fi dropped! Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
      delay(500);
      Serial.print(".");
    }
  }

  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) return; // Break out if Wi-Fi dropped again
    Serial.println("Attempting MQTT connection...");
    if (client.connect("TorqueBeastESP32")) {
      client.subscribe("torquebeast/command");
      client.subscribe("torquebeast/tune");
      client.subscribe("torquebeast/location"); 
      client.subscribe("torquebeast/req_config"); 
      publishCurrentConfig(); 
      debugLog("SYS: MQTT Reconnected!");
      beep(2, 100);
    } else { 
      delay(2000); 
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); 

  preferences.begin("car-setup", false);
  baseSpeed = preferences.getInt("baseSpeed", 160);
  turnSpeed = preferences.getInt("turnSpeed", 184);
  Kp = preferences.getFloat("Kp", 6.0); 
  Kd = preferences.getFloat("Kd", 2.0);
  crashGForceF = preferences.getFloat("gF", 20.0); 
  crashGForceB = preferences.getFloat("gB", 20.0); 
  guardianEnabled = preferences.getBool("guardian", true);
  autoCallEnabled = preferences.getBool("autoCall", false);

  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);
  ledcAttach(PIN_ENA, 1000, 8); ledcAttach(PIN_ENB, 1000, 8);
  drive(0, 0); 

  pinMode(trigPin, OUTPUT); pinMode(echoFront, INPUT); pinMode(echoBack, INPUT);
  pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW); 

  Wire.begin();
  Wire.setClock(400000); 
  if (!mpu.begin()) { while (1) delay(10); }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 5000)) { delay(500); }

  client.setServer(mqtt_server, 1883); 
  client.setCallback(mqttCallback);
  client.setBufferSize(2048); 

  if (client.connect("TorqueBeastESP32")) {
    client.subscribe("torquebeast/command");
    client.subscribe("torquebeast/tune");
    client.subscribe("torquebeast/location"); 
    client.subscribe("torquebeast/req_config"); 
    publishCurrentConfig();
  }

  float sumY = 0;
  for (int i = 0; i < 100; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumY += a.acceleration.y;
    delay(10);
  }
  accelYOffset = sumY / 100.0;
  beep(3, 80); 
}

void loop() {
  if (!client.connected() || WiFi.status() != WL_CONNECTED) { 
    drive(0, 0); 
    currentCommand = 'S'; 
    reconnect(); 
  }
  client.loop(); 

  // --- 10-SECOND SAFETY GRACE PERIOD RUNTIME ---
  if (emergencyPending && !emergencyDispatched) {
    unsigned long elapsed = millis() - crashStartTime;
    
    digitalWrite(buzzerPin, (millis() % 200 < 100) ? HIGH : LOW);

    static unsigned long lastTick = 0;
    if (millis() - lastTick >= 1000) {
      lastTick = millis();
      int secLeft = (GRACE_PERIOD_MS - elapsed) / 1000;
      if (secLeft < 0) secLeft = 0;
      String alertJson = "{\"status\":\"COUNTDOWN\",\"sec\":" + String(secLeft) + "}";
      client.publish("torquebeast/alert", alertJson.c_str());
    }

    if (elapsed >= GRACE_PERIOD_MS) {
      emergencyDispatched = true;
      digitalWrite(buzzerPin, HIGH); 
      client.publish("torquebeast/alert", "{\"status\":\"DISPATCHED\"}");

      if (autoCallEnabled && WiFi.status() == WL_CONNECTED) {
        dispatchAllEmergencyAlerts();
      } else {
        debugLog("SYS: Auto-Calling disabled in dashboard. Telecom dispatch skipped.");
      }
    }
  }

  // --- ALARM LOCKDOWN DRIVE OVERRIDE ---
  if (crashAlarmActive) { 
    currentCommand = 'S';
    if (!emergencyPending && !sentinelMode) {
      digitalWrite(buzzerPin, HIGH);
    }
    drive(0, 0); 
  }

  // --- 20ms ALTERNATING ULTRASONIC RADAR ---
  static unsigned long lastSonarTime = 0;
  static bool pingFrontNext = true;
  
  if (millis() - lastSonarTime >= 20) {
    lastSonarTime = millis();
    long duration;
    
    if (pingFrontNext) {
      digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW);
      duration = pulseIn(echoFront, HIGH, 20000); 
      distanceFront = (duration == 0) ? 100.0 : (duration * SOUND_SPEED / 2);
      if (distanceFront > 100.0) distanceFront = 100.0; 
      distanceDrop = (previousDistanceFront < 100.0 && distanceFront < 100.0) ? (previousDistanceFront - distanceFront) : 0.0;
      previousDistanceFront = distanceFront;
    } else {
      digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW);
      duration = pulseIn(echoBack, HIGH, 20000); 
      distanceBack = (duration == 0) ? 100.0 : (duration * SOUND_SPEED / 2);
      if (distanceBack > 100.0) distanceBack = 100.0; 
      distanceDropB = (previousDistanceBack < 100.0 && distanceBack < 100.0) ? (previousDistanceBack - distanceBack) : 0.0;
      previousDistanceBack = distanceBack;
    }
    pingFrontNext = !pingFrontNext; 
  }

  // --- 500Hz MPU6050 EVALUATION ---
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float forwardAccel = a.acceleration.y - accelYOffset; 

  bool isCrashing = false;
  float activeThreshF = sentinelMode ? 1.5 : crashGForceF;
  float activeThreshB = sentinelMode ? 1.5 : crashGForceB;

  if (currentCommand == 'F' && distanceFront <= crashDistance && forwardAccel < -activeThreshF) {
    isCrashing = true; 
  } else if (currentCommand == 'B' && distanceBack <= crashDistance && forwardAccel > activeThreshB) {
    isCrashing = true;  
  }

  if (!crashAlarmActive && isCrashing) {
    crashAlarmActive = true; 
    emergencyPending = true;
    emergencyDispatched = false;
    crashStartTime = millis();
    currentCommand = 'S'; 
    debugLog("!!! IMPACT DETECTED !!! 10-Second Countdown Initiated...");

    strcpy(crashBuffer[0].timeStr, getISTTime().c_str());
    crashBuffer[0].accel = forwardAccel; crashBuffer[0].df = distanceFront; crashBuffer[0].db = distanceBack; crashBuffer[0].cmd = currentCommand;
    strncpy(crashBuffer[0].action, "IMPACT_COUNTDOWN", 31); crashBuffer[0].action[31] = '\0';
    bbState = BB_CRASHING; crashIdx = 1;
  }

  // --- BLACK BOX BUFFER MANAGEMENT ---
  static unsigned long lastBBTime = 0;
  if (millis() - lastBBTime >= 100) {
    lastBBTime = millis();
    BlackBoxLog currentSnapshot;
    strcpy(currentSnapshot.timeStr, getISTTime().c_str());
    currentSnapshot.accel = forwardAccel; currentSnapshot.df = distanceFront; currentSnapshot.db = distanceBack; currentSnapshot.cmd = currentCommand;
    strncpy(currentSnapshot.action, crashAlarmActive ? "ALARM_HOLD" : "NORMAL_RUN", 31);
    currentSnapshot.action[31] = '\0';

    if (bbState == BB_NORMAL) {
      preBuffer[preIdx] = currentSnapshot; preIdx = (preIdx + 1) % 5;
    } else if (bbState == BB_CRASHING) {
      if (crashIdx < 3) crashBuffer[crashIdx++] = currentSnapshot; else bbState = BB_AFTERMATH;
    } else if (bbState == BB_AFTERMATH) {
      if (postIdx < 5) postBuffer[postIdx++] = currentSnapshot; else bbState = BB_SENDING;
    } else if (bbState == BB_SENDING) {
      if (client.connected()) {
        String report = "{\"crash\":[";
        for (int i = 0; i < 5; i++) report += serializeLog(preBuffer[(preIdx + i) % 5], "B") + ",";
        for (int i = 0; i < 3; i++) report += serializeLog(crashBuffer[i], "C") + ",";
        for (int i = 0; i < 5; i++) { report += serializeLog(postBuffer[i], "A"); if (i < 4) report += ","; }
        report += "]}";
        client.publish("torquebeast/crashreport", report.c_str());
      }
      bbState = BB_LOCKED;
    }
  }

  // --- TELEMETRY BROADCAST ---
  if (millis() - lastTelemetryTime > 400) {
    String telemetry = "{\"accel\":" + String(forwardAccel, 2) + ", \"df\":" + String(distanceFront, 1) + ", \"db\":" + String(distanceBack, 1) + ", \"cmd\":\"" + String(currentCommand) + "\"}";
    client.publish("torquebeast/telemetry", telemetry.c_str());
    lastTelemetryTime = millis();
  }

  // --- STANDARD DRIVING LOGIC ---
  if (!crashAlarmActive) {
    if (currentCommand == 'F') {
      if (distanceFront <= 8.0 || (distanceDrop > 15.0 && distanceFront < 35.0)) {
        drive(0, 0); currentCommand = 'S'; previousErrorF = 0; beep(1, 300);         
      } else if (distanceFront <= 35.0) {
        float error = distanceFront - 8.0; 
        float derivative = error - previousErrorF; 
        float output = (Kp * error) + (Kd * derivative);
        previousErrorF = error; 
        int pSpeed = constrain((int)output, 0, baseSpeed); 
        if (pSpeed < 50) pSpeed = 0; 
        drive(pSpeed, pSpeed);
      } else { 
        drive(baseSpeed, baseSpeed); 
        previousErrorF = distanceFront - 8.0; 
      }
    } else if (currentCommand == 'B') {
      if (distanceBack <= 8.0 || (distanceDropB > 15.0 && distanceBack < 35.0)) {
        drive(0, 0); currentCommand = 'S'; previousErrorB = 0; beep(1, 300);         
      } else if (distanceBack <= 35.0) {
        float error = distanceBack - 8.0; 
        float derivative = error - previousErrorB; 
        float output = (Kp * error) + (Kd * derivative);
        previousErrorB = error; 
        int pSpeed = constrain((int)output, 0, baseSpeed); 
        if (pSpeed < 50) pSpeed = 0; 
        drive(-pSpeed, -pSpeed); 
      } else { 
        drive(-baseSpeed, -baseSpeed); 
        previousErrorB = distanceBack - 8.0; 
      }
    }
    else if (currentCommand == 'L') drive(-turnSpeed, turnSpeed);
    else if (currentCommand == 'R') drive(turnSpeed, -turnSpeed);
    else if (currentCommand == 'S') drive(0, 0);
  }
}
