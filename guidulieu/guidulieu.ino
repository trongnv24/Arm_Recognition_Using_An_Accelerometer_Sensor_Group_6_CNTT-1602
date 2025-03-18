#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

// === CẤU HÌNH WIFI ===
const char* ssid = "Linh Vinh";
const char* password = "66668888";

// === CẤU HÌNH SERVER ===
const char* serverUrl = "http://192.168.1.101:5000/sensor-data"; // Endpoint chính để gửi dữ liệu cảm biến
const char* statusUrl = "http://192.168.1.101:5000/prediction"; // Endpoint để kiểm tra trạng thái thu thập

// === MPU6050 I2C ADDRESS ===
#define MPU6050_ADDR 0x68

// === TRẠNG THÁI HOẠT ĐỘNG ===
bool isCollecting = false;
unsigned long lastStatusCheckTime = 0;
const unsigned long STATUS_CHECK_INTERVAL = 2000; // Kiểm tra trạng thái mỗi 2 giây

// === CẤU HÌNH TỐC ĐỘ GỬI DỮ LIỆU ===
const unsigned long SAMPLE_INTERVAL = 6; // 6.67ms ≈ 150Hz = 150 mẫu/giây
unsigned long lastSampleTime = 0;

// === BUFFER CHO DỮ LIỆU ===
#define MAX_BUFFER_SIZE 15 // Số lượng mẫu tối đa cần buffer trước khi gửi đi
struct SensorData {
  int16_t accX, accY, accZ;
  int16_t gyroX, gyroY, gyroZ;
};
SensorData sensorBuffer[MAX_BUFFER_SIZE];
int bufferIndex = 0;

// === BIẾN THEO DÕI HIỆU SUẤT ===
unsigned long sampleCount = 0;
unsigned long lastSecondTime = 0;
unsigned long samplesPerSecond = 0;

WiFiClient wifiClient; // Tạo một WiFiClient tái sử dụng
HTTPClient http; // Tạo một HTTPClient tái sử dụng

void setup() {
  Serial.begin(115200);
  Serial.println("Bắt đầu...");
  
  // Khởi tạo I2C
  Wire.begin();
  Wire.setClock(400000); // 400kHz I2C clock
  
  // Khởi tạo MPU6050
  Serial.println("Đang khởi tạo MPU6050...");
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // Set to 0 (wakes up MPU6050)
  Wire.endTransmission(true);
  
  if (testMPU6050()) {
    Serial.println("MPU6050 đã được khởi tạo thành công!");
  } else {
    Serial.println("Không thể tìm thấy MPU6050. Kiểm tra lại kết nối!");
    while (1) {
      delay(500);
    }
  }
  
  // Cấu hình MPU6050 (thang đo)
  // Cấu hình thang đo Gyro ±250°/s
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B);  // Gyro config register
  Wire.write(0x00);  // ±250°/s
  Wire.endTransmission(true);
  
  // Cấu hình thang đo Accelerometer ±2g
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C);  // Accel config register
  Wire.write(0x00);  // ±2g
  Wire.endTransmission(true);
  
  // Cấu hình Sampling Rate (DLPF_CFG = 0, Rate = 8kHz)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1A);  // CONFIG register
  Wire.write(0x00);  // DLPF_CFG = 0 -> Bandwidth = 260Hz
  Wire.endTransmission(true);
  
  // Cấu hình Sample Rate Divider (8kHz / (1 + 0) = 8kHz)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x19);  // SMPLRT_DIV register
  Wire.write(0x00);  // Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV)
  Wire.endTransmission(true);
  
  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  
  int timeout_counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    timeout_counter++;
    if (timeout_counter >= 20) { // Sau 10 giây nếu không kết nối được
      Serial.println("\nKhông thể kết nối WiFi. Đang khởi động lại...");
      ESP.restart();
    }
  }
  
  Serial.println("\nĐã kết nối WiFi!");
  Serial.print("Địa chỉ IP: ");
  Serial.println(WiFi.localIP());
  
  // Kiểm tra kết nối với server
  testServerConnection();
  
  Serial.println("\nESP8266 sẽ tự động kiểm tra trạng thái thu thập dữ liệu từ server");
  Serial.println("Cấu hình để gửi 150 mẫu mỗi giây");
  
  // Khởi tạo thời gian
  lastSampleTime = millis();
  lastSecondTime = millis();
}

bool testMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  byte error = Wire.endTransmission();
  return (error == 0);
}

// Hàm kiểm tra kết nối với server
void testServerConnection() {
  Serial.println("Kiểm tra kết nối với server...");
  
  // Thử kết nối đến trang chủ server
  http.begin(wifiClient, "http://172.20.10.2:5000/");
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.print("Kết nối thành công! HTTP code: ");
    Serial.println(httpCode);
    String payload = http.getString();
    Serial.println("Payload từ server:");
    Serial.println(payload.substring(0, 200) + "..."); // Chỉ hiển thị 200 ký tự đầu
  } else {
    Serial.print("Không thể kết nối đến server. Error: ");
    Serial.println(http.errorToString(httpCode));
  }
  
  http.end();
}

// Kiểm tra trạng thái thu thập dữ liệu từ server
bool checkCollectionStatus() {
  http.begin(wifiClient, statusUrl);
  int httpCode = http.GET();
  bool collecting = false;
  
  if (httpCode > 0) {
    String response = http.getString();
    
    // Phân tích JSON để lấy trạng thái thu thập
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      // Kiểm tra trạng thái thu thập
      if (doc.containsKey("is_collecting")) {
        collecting = doc["is_collecting"];
        
        // Nếu trạng thái thay đổi, thông báo
        if (collecting != isCollecting) {
          if (collecting) {
            Serial.println("\n===== SERVER ĐÃ BẮT ĐẦU THU THẬP DỮ LIỆU =====");
            // Reset bộ đếm khi bắt đầu thu thập mới
            sampleCount = 0;
            lastSecondTime = millis();
          } else {
            Serial.println("\n===== SERVER ĐÃ DỪNG THU THẬP DỮ LIỆU =====");
            
            // Hiển thị kết quả dự đoán nếu có
            if (doc.containsKey("gesture") && doc.containsKey("confidence")) {
              String gesture = doc["gesture"];
              float confidence = doc["confidence"];
              Serial.print("\n===== KẾT QUẢ DỰ ĐOÁN =====\n");
              Serial.print("Cử chỉ: ");
              Serial.println(gesture);
              Serial.print("Độ tin cậy: ");
              Serial.print(confidence);
              Serial.println("%\n");
            }
          }
        }
      }
    }
  } else {
    Serial.print("Lỗi HTTP khi kiểm tra trạng thái: ");
    Serial.println(httpCode);
  }
  
  http.end();
  return collecting;
}

// Đọc dữ liệu thô từ MPU6050
void readRawData(int16_t *accX, int16_t *accY, int16_t *accZ, int16_t *gyroX, int16_t *gyroY, int16_t *gyroZ) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);  // Starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);  // Request 14 registers
  
  *accX = Wire.read() << 8 | Wire.read();   // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
  *accY = Wire.read() << 8 | Wire.read();   // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
  *accZ = Wire.read() << 8 | Wire.read();   // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
  
  // Skip temperature - 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
  Wire.read() << 8 | Wire.read();
  
  *gyroX = Wire.read() << 8 | Wire.read();  // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
  *gyroY = Wire.read() << 8 | Wire.read();  // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
  *gyroZ = Wire.read() << 8 | Wire.read();  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)
}

// Gửi nhiều mẫu dữ liệu cảm biến đến server
void sendSensorDataBatch() {
  if (bufferIndex == 0) return; // Không có dữ liệu để gửi
  
  // Tạo JSON document với mảng các mẫu
  DynamicJsonDocument doc(2048); // Tăng kích thước buffer JSON
  JsonArray samples = doc.createNestedArray("samples");
  
  for (int i = 0; i < bufferIndex; i++) {
    JsonObject sample = samples.createNestedObject();
    sample["AccX"] = sensorBuffer[i].accX;
    sample["AccY"] = sensorBuffer[i].accY;
    sample["AccZ"] = sensorBuffer[i].accZ;
    sample["GyroX"] = sensorBuffer[i].gyroX;
    sample["GyroY"] = sensorBuffer[i].gyroY;
    sample["GyroZ"] = sensorBuffer[i].gyroZ;
  }
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  // Gửi HTTP request
  http.begin(wifiClient, serverUrl);
  http.addHeader("Content-Type", "application/json");
  
  unsigned long startTime = micros();
  int httpResponseCode = http.POST(requestBody);
  unsigned long endTime = micros();
  
  // Chỉ hiện thị thông báo mỗi 5 lần gửi
  static int counter = 0;
  if (counter % 5 == 0) {
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Đã gửi ");
      Serial.print(bufferIndex);
      Serial.print(" mẫu, mã: ");
      Serial.print(httpResponseCode);
      Serial.print(", thời gian: ");
      Serial.print((endTime - startTime) / 1000.0);
      Serial.println("ms");
    } else {
      Serial.print("Lỗi gửi dữ liệu: ");
      Serial.println(httpResponseCode);
    }
  }
  counter++;
  
  http.end();
  
  // Reset buffer
  bufferIndex = 0;
}

// Thêm một mẫu dữ liệu cảm biến vào buffer
void addSampleToBuffer(int16_t accX, int16_t accY, int16_t accZ, int16_t gyroX, int16_t gyroY, int16_t gyroZ) {
  // Thêm mẫu vào buffer
  sensorBuffer[bufferIndex].accX = accX;
  sensorBuffer[bufferIndex].accY = accY;
  sensorBuffer[bufferIndex].accZ = accZ;
  sensorBuffer[bufferIndex].gyroX = gyroX;
  sensorBuffer[bufferIndex].gyroY = gyroY;
  sensorBuffer[bufferIndex].gyroZ = gyroZ;
  
  bufferIndex++;
  
  // Nếu buffer đầy, gửi dữ liệu đi
  if (bufferIndex >= MAX_BUFFER_SIZE) {
    sendSensorDataBatch();
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Mất kết nối WiFi. Đang kết nối lại...");
    WiFi.reconnect();
    delay(1000);
    return;
  }
  
  // Kiểm tra trạng thái thu thập từ server định kỳ
  if (currentMillis - lastStatusCheckTime >= STATUS_CHECK_INTERVAL) {
    lastStatusCheckTime = currentMillis;
    isCollecting = checkCollectionStatus();
  }
  
  // Đếm số mẫu mỗi giây
  if (currentMillis - lastSecondTime >= 1000) {
    samplesPerSecond = sampleCount;
    Serial.print("Tốc độ lấy mẫu: ");
    Serial.print(samplesPerSecond);
    Serial.println(" mẫu/giây");
    sampleCount = 0;
    lastSecondTime = currentMillis;
  }
  
  // Nếu đang thu thập dữ liệu và đã đến lúc lấy mẫu
  if (isCollecting && (currentMillis - lastSampleTime >= SAMPLE_INTERVAL)) {
    lastSampleTime = currentMillis;
    
    // Đọc dữ liệu cảm biến
    int16_t accX, accY, accZ, gyroX, gyroY, gyroZ;
    readRawData(&accX, &accY, &accZ, &gyroX, &gyroY, &gyroZ);
    
    // Hiển thị dữ liệu (chỉ hiển thị mỗi 50 mẫu để giảm spam)
    static int displayCounter = 0;
    if (displayCounter % 75 == 0) {
      Serial.print("Mẫu: AccX=");
      Serial.print(accX);
      Serial.print(" AccY=");
      Serial.print(accY);
      Serial.print(" AccZ=");
      Serial.print(accZ);
      Serial.print(" GyroX=");
      Serial.print(gyroX);
      Serial.print(" GyroY=");
      Serial.print(gyroY);
      Serial.print(" GyroZ=");
      Serial.println(gyroZ);
    }
    displayCounter++;
    
    // Thêm dữ liệu vào buffer
    addSampleToBuffer(accX, accY, accZ, gyroX, gyroY, gyroZ);
    
    // Tăng số lượng mẫu
    sampleCount++;
  }
  
  // Nếu đang thu thập dữ liệu và có dữ liệu trong buffer nhưng chưa đầy,
  // gửi dữ liệu nếu không có mẫu mới trong 50ms
  if (isCollecting && bufferIndex > 0 && (currentMillis - lastSampleTime >= 50)) {
    sendSensorDataBatch();
  }
  
  // Nếu dừng thu thập và còn dữ liệu trong buffer, gửi nốt
  if (!isCollecting && bufferIndex > 0) {
    sendSensorDataBatch();
  }
  
  // Tạm dừng ngắn để ESP có thể xử lý các tác vụ khác
  yield();
}