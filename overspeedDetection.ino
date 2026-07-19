#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_Mail_Client.h>
#include <vector>
#include <time.h>

const char* WIFI_SSID         = "iPhone";
const char* WIFI_PASS         = "12345678";
const float SENSOR_DISTANCE_M = 0.15;
const float SPEED_LIMIT_KMH   = 2.0;

// ── Email Configuration
#define SMTP_HOST       "smtp.gmail.com"
#define SMTP_PORT       465
#define SENDER_EMAIL    "tietcampustraffic@gmail.com"
#define SENDER_PASSWORD "bujowskkjhqpwfcf"
#define RECIPIENT_EMAIL "8181natasha@gmail.com"

// ── Pin Definitions ──────────────────────────────────────────
#define TRIG1  5
#define ECHO1  18
#define TRIG2  13
#define ECHO2  12

// ── Objects ──────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
HardwareSerial    gpsSerial(1);
TinyGPSPlus       gps;
SMTPSession       smtp;

// ── State Variables ───────────────────────────────────────────
unsigned long t1            = 0;
bool          sensor1Hit    = false;
float         lastLat       = 0.0;
float         lastLng       = 0.0;
bool          wifiConnected = false;
String        CAM_URL       = "";
uint8_t*      imageData     = nullptr;
size_t        imageSize     = 0;

// ─────────────────────────────────────────────────────────────
// SMTP callback
// ─────────────────────────────────────────────────────────────
void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
  if (status.success()) {
    Serial.println("Email sent successfully!");
  } else {
    Serial.println("Email failed.");
  }
}

// ─────────────────────────────────────────────────────────────
// Sync time via NTP
// ─────────────────────────────────────────────────────────────
void syncTime() {
  Serial.println("Syncing time...");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Syncing time..");
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (getLocalTime(&timeinfo)) {
    Serial.println("\nTime synced!");
    Serial.println(&timeinfo, "%A %B %d %Y %H:%M:%S");
    lcd.setCursor(0, 1); lcd.print("Time OK!");
  } else {
    Serial.println("\nTime sync failed.");
    lcd.setCursor(0, 1); lcd.print("Time FAILED");
  }
  delay(1000);
}

// ─────────────────────────────────────────────────────────────
// Read distance from ultrasonic sensor
// ─────────────────────────────────────────────────────────────
long getDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ─────────────────────────────────────────────────────────────
// Auto scan network to find IP Webcam
// ─────────────────────────────────────────────────────────────
bool findCamera() {
  Serial.println("Scanning for IP Webcam on network...");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Finding camera");
  lcd.setCursor(0, 1); lcd.print("Please wait...");

  String baseIP = WiFi.localIP().toString();
  String subnet = baseIP.substring(0, baseIP.lastIndexOf('.') + 1);
  Serial.print("Scanning subnet: "); Serial.println(subnet);

  for (int i = 1; i <= 254; i++) {
    String testIP  = subnet + String(i);
    if (testIP == baseIP) continue;

    String testURL = "http://" + testIP + ":8080/shot.jpg";
    HTTPClient http;
    http.begin(testURL);
    http.setTimeout(300);
    int code = http.GET();
    http.end();

    if (code == 200) {
      CAM_URL = testURL;
      Serial.print("Camera found at: "); Serial.println(testURL);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Camera found!");
      lcd.setCursor(0, 1); lcd.print(testIP);
      delay(2000);
      return true;
    }

    if (i % 10 == 0) {
      Serial.print("Scanning... "); Serial.println(testIP);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Scanning...");
      lcd.setCursor(0, 1); lcd.print(testIP);
    }
  }

  Serial.println("Camera not found!");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Camera not");
  lcd.setCursor(0, 1); lcd.print("found!");
  delay(2000);
  return false;
}

// ─────────────────────────────────────────────────────────────
// Fetch image from IP Webcam into memory
// ─────────────────────────────────────────────────────────────
bool fetchImage() {
  Serial.print("WiFi status: "); Serial.println(wifiConnected);
  Serial.print("CAM_URL: ");     Serial.println(CAM_URL);

  if (!wifiConnected || CAM_URL == "") {
    Serial.println("No WiFi or camera URL - skipping image.");
    if (wifiConnected && CAM_URL == "") {
      Serial.println("CAM_URL empty - rescanning...");
      findCamera();
    }
    return false;
  }

  // Free previous image
  if (imageData != nullptr) {
    free(imageData);
    imageData = nullptr;
    imageSize = 0;
  }

  Serial.println("Fetching image from webcam...");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Car at S1...");
  lcd.setCursor(0, 1); lcd.print("Capturing...");

  HTTPClient http;
  http.begin(CAM_URL);
  http.setTimeout(10000);
  int code = http.GET();
  Serial.print("HTTP code: "); Serial.println(code);

  if (code == 200) {
    int contentLength = http.getSize();
    Serial.print("Content-Length: "); Serial.println(contentLength);
    WiFiClient* stream = http.getStreamPtr();

    if (contentLength <= 0) {
      Serial.println("Unknown size - reading until timeout...");
      std::vector<uint8_t> buffer;
      buffer.reserve(50000);
      unsigned long start = millis();
      while (millis() - start < 8000) {
        if (stream->available()) {
          buffer.push_back(stream->read());
        }
        if (!http.connected()) break;
      }
      imageSize = buffer.size();
      if (imageSize > 0) {
        imageData = (uint8_t*)malloc(imageSize);
        if (imageData) {
          memcpy(imageData, buffer.data(), imageSize);
        }
      }

    } else {
      imageData = (uint8_t*)malloc(contentLength);
      if (!imageData) {
        Serial.println("Memory allocation failed!");
        http.end();
        return false;
      }
      int bytesRead = 0;
      unsigned long start = millis();
      while (bytesRead < contentLength && millis() - start < 8000) {
        if (stream->available()) {
          int chunk = stream->readBytes(
            imageData + bytesRead,
            contentLength - bytesRead
          );
          bytesRead += chunk;
        }
        delay(1);
      }
      imageSize = bytesRead;
      Serial.print("Bytes read: "); Serial.println(bytesRead);
      if (bytesRead < contentLength) {
        Serial.println("Warning: incomplete image.");
      }
    }

    http.end();

    if (imageSize > 0) {
      Serial.print("Image ready. Size: ");
      Serial.print(imageSize); Serial.println(" bytes");
      lcd.setCursor(0, 1); lcd.print("Image OK!");
      delay(500);
      return true;
    }
  }

  Serial.println("Fetch failed - rescanning for camera...");
  http.end();
  findCamera();

  if (imageData != nullptr) {
    free(imageData);
    imageData = nullptr;
    imageSize = 0;
  }

  lcd.setCursor(0, 1); lcd.print("Cam failed!");
  delay(500);
  return false;
}

// ─────────────────────────────────────────────────────────────
// Send email with photo attached
// ─────────────────────────────────────────────────────────────
void sendEmail(float speedKMH) {
  Serial.println("Preparing email...");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Sending email..");

  ESP_Mail_Session session;
  session.server.host_name  = SMTP_HOST;
  session.server.port       = SMTP_PORT;
  session.login.email       = SENDER_EMAIL;
  session.login.password    = SENDER_PASSWORD;
  session.login.user_domain = "";

  SMTP_Message message;
  message.sender.name  = "Campus Speed System";
  message.sender.email = SENDER_EMAIL;
  message.subject      = "OVERSPEED ALERT - Campus Security";
  message.addRecipient("Authority", RECIPIENT_EMAIL);

  String body = "OVERSPEED ALERT!\n\n";
  body += "Speed Detected : " + String(speedKMH, 1)                   + " km/h\n";
  body += "Speed Limit    : " + String(SPEED_LIMIT_KMH, 1)            + " km/h\n";
  body += "Exceeded by    : " + String(speedKMH - SPEED_LIMIT_KMH, 1) + " km/h\n\n";

  if (lastLat != 0.0 && lastLng != 0.0) {
    body += "Location: https://maps.google.com/?q=";
    body += String(lastLat, 6) + "," + String(lastLng, 6) + "\n\n";
  } else {
    body += "Location: GPS fix not available\n\n";
  }

  body += "Photo of vehicle is attached.\n";
  body += "- Campus Speed Monitoring System";

  message.text.content = body.c_str();
  message.text.charSet = "utf-8";

  if (imageData != nullptr && imageSize > 0) {
    Serial.print("Attaching image. Size: "); Serial.println(imageSize);
    SMTP_Attachment attachment;
    attachment.descr.filename          = "overspeed_capture.jpg";
    attachment.descr.mime              = "image/jpeg";
    attachment.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;
    attachment.blob.data               = imageData;
    attachment.blob.size               = imageSize;
    message.addAttachment(attachment);
    Serial.println("Image attached.");
  } else {
    Serial.println("No image - sending email without photo.");
  }

  smtp.debug(1);
  smtp.callback(smtpCallback);

  if (!smtp.connect(&session)) {
    Serial.println("SMTP connect failed.");
    lcd.setCursor(0, 1); lcd.print("Email failed!");
    delay(2000);
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Send failed: " + smtp.errorReason());
    lcd.setCursor(0, 1); lcd.print("Send failed!");
  } else {
    lcd.setCursor(0, 1); lcd.print("Email sent! OK");
  }

  smtp.closeSession();

  if (imageData != nullptr) {
    free(imageData);
    imageData = nullptr;
    imageSize = 0;
  }
  delay(2000);
}

// ─────────────────────────────────────────────────────────────
// Update GPS
// ─────────────────────────────────────────────────────────────
void updateGPS() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  if (gps.location.isValid() && gps.location.isUpdated()) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
  }
}

// ─────────────────────────────────────────────────────────────
// Display speed on LCD and Serial
// ─────────────────────────────────────────────────────────────
void displaySpeed(float speedKMH) {
  Serial.print("Speed: ");
  Serial.print(speedKMH, 1);
  Serial.println(" km/h");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Speed:");
  lcd.print(speedKMH, 1);
  lcd.print("km/h");

  if (speedKMH > SPEED_LIMIT_KMH) {
    lcd.setCursor(0, 1);
    lcd.print("!! OVERSPEED !!");
    Serial.println("OVERSPEED DETECTED!");
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Speed OK");
    Serial.println("Speed within limit.");
  }
  delay(2000);
}

// ─────────────────────────────────────────────────────────────
// Reset system
// ─────────────────────────────────────────────────────────────
void resetSystem() {
  sensor1Hit = false;
  t1 = 0;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Ready");
  lcd.setCursor(0, 1);
  lcd.print("Limit:");
  lcd.print(SPEED_LIMIT_KMH, 0);
  lcd.print("km/h");
  Serial.println("--- System Ready ---");
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Campus Speed Monitor ===");

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Speed Monitor");
  lcd.setCursor(0, 1); lcd.print("Starting...");
  delay(1500);

  gpsSerial.begin(9600, SERIAL_8N1, 14, 19);
  Serial.println("GPS initialized.");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("GPS init OK");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WiFi connecting");
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    lcd.setCursor(0, 1); lcd.print("WiFi OK!");
    delay(1000);
    syncTime();
    if (findCamera()) {
      Serial.println("Camera URL: " + CAM_URL);
    } else {
      Serial.println("Camera not found - will retry on detection.");
    }
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi failed. Email and camera disabled.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi FAILED");
    lcd.setCursor(0, 1); lcd.print("No email/cam");
    delay(2000);
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Getting GPS...");
  unsigned long gpsWait = millis();
  while (millis() - gpsWait < 10000) {
    updateGPS();
    if (gps.location.isValid()) {
      Serial.print("GPS Fix! Lat: "); Serial.print(lastLat, 6);
      Serial.print(" Lng: ");         Serial.println(lastLng, 6);
      lcd.setCursor(0, 1); lcd.print("GPS Fix OK!");
      delay(1500);
      break;
    }
  }
  if (!gps.location.isValid()) {
    Serial.println("No GPS fix yet - continuing.");
    lcd.setCursor(0, 1); lcd.print("No GPS yet");
    delay(1500);
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SMS: disabled");
  lcd.setCursor(0, 1); lcd.print("No SIM card");
  Serial.println("SIM800L skipped - no SIM card.");
  delay(2000);

  resetSystem();
}

// ─────────────────────────────────────────────────────────────
void loop() {
  updateGPS();

  long dist1 = getDistanceCM(TRIG1, ECHO1);

  // ── Sensor 1 triggered ───────────────────────────────────
 if (dist1 < 15 && !sensor1Hit) {
  sensor1Hit = true;
  Serial.println("Sensor 1 triggered! Capturing image now...");

  // Try capturing image
  if (!fetchImage()) {
    Serial.println("Image capture failed!");
  }

  // Start timing AFTER capture
  t1 = millis();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Car at S1...");
  lcd.setCursor(0, 1); lcd.print("Timing start");
}

  // ── Wait for Sensor 2 ────────────────────────────────────
  if (sensor1Hit) {

    if (millis() - t1 > 3000) {
      Serial.println("Timeout - car didn't reach S2.");
      // Free image memory on timeout
      if (imageData != nullptr) {
        free(imageData);
        imageData = nullptr;
        imageSize = 0;
      }
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Timeout!");
      lcd.setCursor(0, 1); lcd.print("Try again...");
      delay(1500);
      resetSystem();
      return;
    }

    long dist2 = getDistanceCM(TRIG2, ECHO2);

    if (dist2 < 15) {
      unsigned long t2 = millis();
      float timeSec    = (t2 - t1) / 1000.0;

      if (timeSec < 0.01) {
        Serial.println("False trigger - ignoring.");
        if (imageData != nullptr) {
          free(imageData);
          imageData = nullptr;
          imageSize = 0;
        }
        resetSystem();
        return;
      }

      float speedMS  = SENSOR_DISTANCE_M / timeSec;
      float speedKMH = speedMS * 3.6;

      Serial.print("Time: "); Serial.print(timeSec); Serial.println("s");
      Serial.print("GPS: ");
      if (gps.location.isValid()) {
        Serial.print(lastLat, 6);
        Serial.print(", ");
        Serial.println(lastLng, 6);
      } else {
        Serial.println("No fix yet");
      }

      // Image already captured at Sensor 1
      displaySpeed(speedKMH);

      if (speedKMH > SPEED_LIMIT_KMH) {
        sendEmail(speedKMH); // image already in memory
      } else {
        // Speed OK - free image memory
        if (imageData != nullptr) {
          free(imageData);
          imageData = nullptr;
          imageSize = 0;
        }
      }

      delay(3000);
      resetSystem();
    }
  }
}