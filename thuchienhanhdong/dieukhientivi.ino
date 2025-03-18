#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// === CẤU HÌNH WIFI ===
const char* ssid = "VINH DUY";     // Thay bằng tên WiFi của bạn
const char* password = "12345678"; // Thay bằng mật khẩu WiFi

// === CẤU HÌNH API ===
const char* host = "192.168.1.44"; // Thay bằng IP của máy chạy Flask server
const int port = 5000;              // Port của Flask server
const String predictionEndpoint = "/prediction"; // Endpoint lấy kết quả dự đoán

// === CẤU HÌNH CHÂN GPIO ===
const uint16_t kIrLed = 4;      // GPIO4 (D2) - LED hồng ngoại
const uint16_t kStatusLed = 2;  // GPIO2 (D4) - LED thông thường

// === CẤU HÌNH SONY IR CODES ===
const uint64_t SONY_VOL_UP = 0x490;    // Tăng âm lượng
const uint64_t SONY_VOL_DOWN = 0xC90;  // Giảm âm lượng
const uint64_t SONY_CH_UP = 0x90;      // Tăng kênh
const uint64_t SONY_CH_DOWN = 0x890;   // Giảm kênh

// Khởi tạo đối tượng IR
IRsend irsend(kIrLed);

// Biến để theo dõi trạng thái LED
unsigned long lastLedToggle = 0;
bool ledState = false;

// Biến để theo dõi cử chỉ
String lastGesture = "";
unsigned long lastGestureTime = 0;
const unsigned long gestureCooldown = 2000; // 2 giây "nghỉ" giữa các cử chỉ

void setup() {
  // Khởi tạo Serial
  Serial.begin(115200);
  Serial.println("\n==== Hệ thống điều khiển TV Sony bằng cử chỉ ====");
  
  // Khởi tạo LED trạng thái
  pinMode(kStatusLed, OUTPUT);
  digitalWrite(kStatusLed, HIGH);
  
  // Khởi tạo LED IR
  irsend.begin();
  
  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(kStatusLed, !digitalRead(kStatusLed)); // Nhấp nháy LED trong khi kết nối
  }
  
  Serial.println("");
  Serial.println("WiFi đã kết nối!");
  Serial.print("Địa chỉ IP: ");
  Serial.println(WiFi.localIP());
  
  // Nhấp nháy LED để báo hiệu đã kết nối WiFi thành công
  for (int i = 0; i < 5; i++) {
    digitalWrite(kStatusLed, HIGH);
    delay(100);
    digitalWrite(kStatusLed, LOW);
    delay(100);
  }
}

void blinkLed() {
  if (millis() - lastLedToggle > 500) {
    ledState = !ledState;
    digitalWrite(kStatusLed, ledState);
    lastLedToggle = millis();
  }
}

void flashLed(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(kStatusLed, HIGH);
    delay(delayMs);
    digitalWrite(kStatusLed, LOW);
    delay(delayMs);
  }
}

void loop() {
  // Nhấp nháy LED để biết hệ thống đang chạy
  blinkLed();
  
  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Mất kết nối WiFi! Đang kết nối lại...");
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }
  
  // Chỉ lấy dự đoán mỗi 500ms để tránh gửi quá nhiều yêu cầu
  static unsigned long lastPredictionTime = 0;
  if (millis() - lastPredictionTime > 500) {
    lastPredictionTime = millis();
    
    // Tạo HTTP client
    WiFiClient client;
    HTTPClient http;
    
    // Tạo URL đầy đủ
    String url = "http://" + String(host) + ":" + String(port) + predictionEndpoint;
    
    // Bắt đầu kết nối
    http.begin(client, url);
    
    // Gửi yêu cầu GET
    int httpCode = http.GET();
    
    // Kiểm tra kết quả
    if (httpCode > 0) {
      // Nhận được phản hồi
      if (httpCode == HTTP_CODE_OK) {
        // Đọc dữ liệu
        String payload = http.getString();
        
        // Parse JSON
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        // Kiểm tra lỗi parsing
        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
        } else {
          // Lấy gesture từ JSON
          String gesture = doc["gesture"];
          float confidence = doc["confidence"];
          
          // Kiểm tra nếu có cử chỉ mới và đã qua thời gian "nghỉ"
          if (gesture != lastGesture && gesture != "Chưa có dự đoán" && 
              millis() - lastGestureTime > gestureCooldown && confidence > 50.0) {
            
            // Lưu cử chỉ và thời gian
            lastGesture = gesture;
            lastGestureTime = millis();
            
            // In thông tin
            Serial.print("Cử chỉ mới: ");
            Serial.print(gesture);
            Serial.print(" (");
            Serial.print(confidence);
            Serial.println("%)");
            
            // Gửi lệnh IR tương ứng
            sendGestureCommand(gesture);
          }
        }
      }
    } else {
      Serial.print("Lỗi kết nối HTTP: ");
      Serial.println(http.errorToString(httpCode).c_str());
    }
    
    // Kết thúc kết nối
    http.end();
  }
  
  // Delay ngắn 
  delay(50);
}

void sendGestureCommand(String gesture) {
  uint64_t irCode = 0;
  String gestureName = "";
  
  // Ánh xạ cử chỉ với mã IR
  // Điều chỉnh tên cử chỉ để phù hợp với tên dự đoán từ model của bạn
  if (gesture == "2") {  // Vuốt trái
    irCode = SONY_CH_DOWN;
    gestureName = "Giảm kênh";
  } 
  else if (gesture == "1") {  // Vuốt phải
    irCode = SONY_CH_UP;
    gestureName = "Tăng kênh";
  }
  else if (gesture == "3") {  // Vuốt lên
    irCode = SONY_VOL_UP;
    gestureName = "Tăng âm lượng";
  }
  else if (gesture == "4") {  // Vuốt xuống
    irCode = SONY_VOL_DOWN;
    gestureName = "Giảm âm lượng";
  }
  
  // Nếu có mã IR tương ứng
  if (irCode != 0) {
    Serial.println("Gửi lệnh: " + gestureName);
    
    // Bật LED khi gửi tín hiệu
    digitalWrite(kStatusLed, HIGH);
    
    // Gửi mã Sony IR - lặp lại 3 lần
    for (int i = 0; i < 3; i++) {
      irsend.sendSony(irCode, 12);   // Sony TV thường dùng mã 12-bit
      delay(25);
    }
    
    // Nhấp nháy để xác nhận đã gửi xong
    flashLed(2, 100);
  }
}