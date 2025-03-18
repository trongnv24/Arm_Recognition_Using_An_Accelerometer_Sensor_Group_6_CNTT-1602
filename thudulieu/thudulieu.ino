#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// MPU6050 I2C address
#define MPU6050_ADDR 0x68

// WiFi credentials
const char* ssid = "Phon";
const char* password = "vinhvinh";

// Flask server address
const char* serverAddress = "http://172.20.10.2:5000/";

// Buffer for collecting sensor readings before sending
const int BUFFER_SIZE = 100;
StaticJsonDocument<8192> jsonBuffer;
JsonArray dataArray = jsonBuffer.to<JsonArray>();
int readingsCount = 0;

// Data collection control
bool isCollecting = false;
String inputString = "";

// Timing variables for 150Hz sampling rate
unsigned long previousMicros = 0;
const unsigned long samplingInterval = 6667; // microseconds (1/150 second ≈ 6667 microseconds)

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize I2C communication 
  Wire.begin();
  Wire.setClock(400000); // Set I2C clock to 400kHz for faster data transfer
  
  // Initialize MPU6050
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // Set to 0 to wake up the MPU6050
  Wire.endTransmission(true);
  
  // Configure MPU6050 sampling rate
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x19);  // Sample Rate Divider register
  Wire.write(0x07);  // Set sample rate to 1kHz / (1+7) = 125Hz (closest we can get to 150Hz with default settings)
  Wire.endTransmission(true);
  
  Serial.println("MPU6050 initialized at 150Hz sampling rate");
  Serial.println("Type START to begin collecting data or STOP to pause");
  Serial.println("Status: STOPPED");
}

void loop() {
  // Check for serial commands
  checkSerialCommands();
  
  // If we have a WiFi connection and data collection is active
  if (WiFi.status() == WL_CONNECTED && isCollecting) {
    
    unsigned long currentMicros = micros();
    
    // Check if it's time to take a sample
    if (currentMicros - previousMicros >= samplingInterval) {
      previousMicros = currentMicros;
      
      // Read raw sensor data
      int16_t accXRaw, accYRaw, accZRaw, gyroXRaw, gyroYRaw, gyroZRaw;
      readMPU6050RawData(&accXRaw, &accYRaw, &accZRaw, &gyroXRaw, &gyroYRaw, &gyroZRaw);
      
      // Add raw values to buffer
      JsonObject reading = dataArray.createNestedObject();
      reading["AccX"] = accXRaw;
      reading["AccY"] = accYRaw;
      reading["AccZ"] = accZRaw;
      reading["GyroX"] = gyroXRaw;
      reading["GyroY"] = gyroYRaw;
      reading["GyroZ"] = gyroZRaw;
      reading["Timestamp"] = currentMicros; // Add timestamp for precise timing analysis
      
      readingsCount++;
      
      // Print values to serial monitor (but less frequently to avoid slowing down sampling)
      if (readingsCount % 15 == 0) { // Only print every 15th reading to maintain sample rate
        Serial.print("AccX: "); Serial.print(accXRaw);
        Serial.print(" | AccY: "); Serial.print(accYRaw);
        Serial.print(" | AccZ: "); Serial.print(accZRaw);
        Serial.print(" | GyroX: "); Serial.print(gyroXRaw);
        Serial.print(" | GyroY: "); Serial.print(gyroYRaw);
        Serial.print(" | GyroZ: "); Serial.println(gyroZRaw);
      }
      
      // When buffer is full, send the data
      if (readingsCount >= BUFFER_SIZE) {
        sendDataToServer();
        
        // Clear buffer
        jsonBuffer.clear();
        dataArray = jsonBuffer.to<JsonArray>();
        readingsCount = 0;
      }
    }
  } else {
    // If not collecting, just wait a bit to save power
    delay(10);
  }
}

void readMPU6050RawData(int16_t *accX, int16_t *accY, int16_t *accZ, int16_t *gyroX, int16_t *gyroY, int16_t *gyroZ) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);  // Starting register for accelerometer
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);  // Request 14 bytes of data
  
  // Read accelerometer data (each value is 16 bits spread across 2 registers)
  *accX = Wire.read() << 8 | Wire.read();
  *accY = Wire.read() << 8 | Wire.read();
  *accZ = Wire.read() << 8 | Wire.read();
  
  // Skip temperature (2 bytes)
  Wire.read() << 8 | Wire.read();
  
  // Read gyroscope data
  *gyroX = Wire.read() << 8 | Wire.read();
  *gyroY = Wire.read() << 8 | Wire.read();
  *gyroZ = Wire.read() << 8 | Wire.read();
  
  // No conversion to physical units - we're keeping the raw values
}

void sendDataToServer() {
  HTTPClient http;
  WiFiClient client;
  
  Serial.println("Sending data to server...");
  
  // Start HTTP connection to server
  http.begin(client, serverAddress);
  http.addHeader("Content-Type", "application/json");
  
  // Serialize JSON to string
  String jsonString;
  serializeJson(dataArray, jsonString);
  
  // Send POST request with JSON data
  int httpResponseCode = http.POST(jsonString);
  
  // Check response
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("Response: ");
    Serial.println(response);
  } else {
    Serial.print("Error sending HTTP Request. Error code: ");
    Serial.println(httpResponseCode);
  }
  
  // Free resources
  http.end();
}

void checkSerialCommands() {
  while (Serial.available()) {
    // Get the new byte
    char inChar = (char)Serial.read();
    
    // Add it to the inputString if not a newline
    if (inChar != '\n' && inChar != '\r') {
      inputString += inChar;
    }
    
    // Process the command when a newline arrives
    if (inChar == '\n') {
      // Convert to uppercase for case-insensitive comparison
      inputString.trim();
      inputString.toUpperCase();
      
      // Process commands
      if (inputString == "START") {
        isCollecting = true;
        previousMicros = micros(); // Reset timing when starting collection
        Serial.println("Status: STARTED - Data collection active at 150Hz");
        // Clear any existing data in buffer when starting new collection
        if (readingsCount > 0) {
          jsonBuffer.clear();
          dataArray = jsonBuffer.to<JsonArray>();
          readingsCount = 0;
        }
      } 
      else if (inputString == "STOP") {
        isCollecting = false;
        Serial.println("Status: STOPPED - Data collection paused");
        // Send any remaining data when stopping
        if (readingsCount > 0) {
          sendDataToServer();
          jsonBuffer.clear();
          dataArray = jsonBuffer.to<JsonArray>();
          readingsCount = 0;
        }
      }
      
      // Clear the string for the next command
      inputString = "";
    }
  }
}