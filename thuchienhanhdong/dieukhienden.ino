#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Make sure to add this line to define D1 and D2 pin mappings
#define D1 5
#define D2 4

// === CẤU HÌNH KẾT NỐI WIFI ===
const char* ssid = "Phon";         // Thay đổi thành SSID WiFi của bạn
const char* password = "vinhvinh"; // Thay đổi thành mật khẩu WiFi của bạn

// === CẤU HÌNH KẾT NỐI SERVER ===
const char* serverUrl = "http://172.20.10.2:5000/"; // Thay đổi thành IP của máy tính chạy server Flask

// === CẤU HÌNH PIN ===
const int LED_PIN = D1;     // Pin điều khiển đèn chính
const int LED_PIN2 = D2;    // Pin điều khiển đèn phụ

// === CẤU HÌNH THỜI GIAN ===
unsigned long lastPredictionTime = 0;
const long predictionInterval = 2000; // Thời gian giữa các lần lấy dự đoán (2 giây)

// === BIẾN LƯU TRẠNG THÁI ===
String lastGesture = "";
bool isLedOn = false;
bool isLed2On = false;

void setup() {
  // Khởi tạo Serial để debug
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 Gesture Control Client ===");
  
  // Cấu hình pin
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  
  // Tắt tất cả các thiết bị khi khởi động
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN2, LOW);
  
  // Kết nối WiFi
  connectToWiFi();
}

void loop() {
  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Mất kết nối WiFi. Đang kết nối lại...");
    connectToWiFi();
  }
  
  // Kiểm tra thời gian để lấy dự đoán mới
  unsigned long currentMillis = millis();
  if (currentMillis - lastPredictionTime >= predictionInterval) {
    lastPredictionTime = currentMillis;
    
    // Lấy kết quả dự đoán từ server
    getPrediction();
  }
}

void connectToWiFi() {
  Serial.print("Đang kết nối đến WiFi ");
  Serial.print(ssid);
  Serial.println("...");
  
  WiFi.begin(ssid, password);
  
  // Chờ kết nối WiFi
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nĐã kết nối WiFi thành công!");
    Serial.print("Địa chỉ IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nKhông thể kết nối WiFi. Vui lòng kiểm tra lại SSID và mật khẩu.");
  }
}

void getPrediction() {
  WiFiClient client;
  HTTPClient http;
  
  // Tạo URL cho API lấy dự đoán
  String url = String(serverUrl) + "prediction";
  
  Serial.print("Đang lấy dự đoán từ: ");
  Serial.println(url);
  
  // Khởi tạo kết nối HTTP
  http.begin(client, url);
  
  // Gửi request GET
  int httpCode = http.GET();
  
  // Kiểm tra kết quả
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      // Đọc phản hồi
      String payload = http.getString();
      Serial.println("Phản hồi từ server: " + payload);
      
      // Phân tích JSON
      processPrediction(payload);
    } else {
      Serial.print("HTTP GET error: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("HTTP connection failed: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }
  
  // Giải phóng tài nguyên
  http.end();
}

void processPrediction(String jsonString) {
  // Phân bổ bộ nhớ cho document JSON
  DynamicJsonDocument doc(1024);
  
  // Phân tích JSON
  DeserializationError error = deserializeJson(doc, jsonString);
  
  // Kiểm tra lỗi
  if (error) {
    Serial.print("Lỗi phân tích JSON: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Lấy kết quả dự đoán
  String gesture = doc["gesture"].as<String>();
  float confidence = doc["confidence"].as<float>();
  
  // Kiểm tra confidence (độ tin cậy)
  if (confidence < 50.0) {
    Serial.println("Độ tin cậy thấp, bỏ qua dự đoán này: " + String(confidence) + "%");
    return;
  }
  
  // Kiểm tra xem gesture có thay đổi không
  if (gesture != lastGesture) {
    Serial.print("Cử chỉ được nhận diện: ");
    Serial.print(gesture);
    Serial.print(" (Độ tin cậy: ");
    Serial.print(confidence);
    Serial.println("%)");
    
    lastGesture = gesture;
    
    // Thực hiện hành động dựa trên kết quả dự đoán
    executeAction(gesture);
  }
}

void executeAction(String gesture) {
  // Thực hiện hành động dựa trên nhãn dự đoán
  if (gesture == "1") {
    // Nhãn 1: Bật đèn chính
    Serial.println("Thực hiện hành động: BẬT ĐÈN CHÍNH");
    digitalWrite(LED_PIN, HIGH);
    isLedOn = true;
  } 
  else if (gesture == "2") {
    // Nhãn 2: Tắt đèn chính
    Serial.println("Thực hiện hành động: TẮT ĐÈN CHÍNH");
    digitalWrite(LED_PIN, LOW);
    isLedOn = false;
  } 
  else if (gesture == "3") {
    // Nhãn 3: Bật đèn phụ
    Serial.println("Thực hiện hành động: BẬT ĐÈN PHỤ");
    digitalWrite(LED_PIN2, HIGH);
    isLed2On = true;
  } 
  else if (gesture == "4") {
    // Nhãn 4: Tắt đèn phụ
    Serial.println("Thực hiện hành động: TẮT ĐÈN PHỤ");
    digitalWrite(LED_PIN2, LOW);
    isLed2On = false;
  }
  
  // In trạng thái hiện tại
  printCurrentState();
}

void printCurrentState() {
  Serial.println("=== TRẠNG THÁI HIỆN TẠI ===");
  Serial.println("Đèn Chính: " + String(isLedOn ? "BẬT" : "TẮT"));
  Serial.println("Đèn Phụ: " + String(isLed2On ? "BẬT" : "TẮT"));
  Serial.println("==========================");
}