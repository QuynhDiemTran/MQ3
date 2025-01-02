#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>  // Thư viện MQTT
#include <NTPClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>  
#include <TinyGPSPlus.h> // Thư viện GPS 

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// cấu hình chân và cảm biến 
#define MQ3_PIN 34
#define RST_PIN 4       // Chân RST của module MFRC522
#define SS_PIN 5        // Chân SS của module MFRC522
#define Threshold 2.3// Ngưỡng điện áp phát hiện nồng độ cồn

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Tạo instance cho MFRC522

const char* ssid = "Phương Thảo";       
const char* password = "123456789";
// Thêm URL của ThingsBoard API và Access Token của thiết bị
const String thingsBoardURL = "http://thingsboard.cloud/api/v1/Hn1iuteL2HZVVHVdMyyT/telemetry";

// NTP Server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600; // GMT+7 (Việt Nam)
const int daylightOffset_sec = 0;    // Không có giờ mùa hè ở Việt Nam

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000); // Cập nhật thời gian mỗi 60 giây

struct Driver {
  byte UID[4];
  String name;
  String licenseID;
  String vehicleInfo;
};

// Dữ liệu tài xế mẫu
Driver drivers[] = {
  { { 0xF3, 0x4B, 0x62, 0x1A }, "Nguyen Van A", "123456", "30A-12345" },
  { { 0x73, 0xF4, 0xED, 0x0D }, "Nguyen Van B", "654321", "29A-67890" }
};

int numDrivers = sizeof(drivers) / sizeof(drivers[0]);  // Số lượng tài xế
bool cardAuthenticated = false;
bool breathTaken = false;  // Trạng thái kiểm tra nồng độ cồn
String driverName;         // Lưu tên tài xế khi quẹt thẻ
String driverLicenseID;
String driverVehicleInfo; 
String status; 

float sensor_volt;
float RS_gas; 
float R0= 9961;  // Giá trị R0 được xác định qua hiệu chuẩn
float ratio;
double BAC; 
int R2 = 4700;  // Giá trị điện trở ngoài (RL)

String getCurrentDateTime() {

  timeClient.update(); // Cập nhật thời gian từ NTP server
 // Lấy thời gian epoch (số giây từ 01/01/1970)
  unsigned long epochTime = timeClient.getEpochTime();
  
  // Chuyển epochTime thành thời gian có định dạng
  setTime(epochTime); // Cập nhật thời gian hệ thống
  
  // Lấy ngày tháng năm và thời gian
  int currentDay = day();   // Lấy ngày
  int currentMonth = month(); // Lấy tháng
  int currentYear = year();  // Lấy năm
  int currentHour = hour();
  int currentMinute = minute();
  //int currentSecond = second();
  String formattedDateTime = String(currentDay) + "-" + String(currentMonth) + "-" + String(currentYear) +"\\n"
                            + String(currentHour) + ":" + String(currentMinute);

  // In ngày tháng năm và thời gian hiện tại lên Serial Monitor
  Serial.print("Ngày giờ hiện tại: ");
  Serial.print(formattedDateTime); // In ngày tháng năm
  return formattedDateTime; 
}

void sendDataToGoogleSheets(String driverName, String licenseID, String vehicleInfo, float ppm, String status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(thingsBoardURL);
    http.addHeader("Content-Type", "application/json");

    String customDateTime = getCurrentDateTime();
    // Tạo payload JSON để gửi
    String payload = "{\"driver_name\":\"" + driverName +
                     "\",\"license_id\":\"" + licenseID +
                     "\",\"vehicle_info\":\"" + vehicleInfo +
                     "\",\"timestamp\":\"" + getCurrentDateTime() +
                     "\",\"alcohol_level\":" + String(ppm) +
                     ",\"status\":\"" + status + "\"}";

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      Serial.println("Dữ liệu đã gửi thành công!");
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Lỗi gửi dữ liệu. Mã lỗi: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi không kết nối!");
  }
}

void displayInfo(String name, String licenseID, String vehicleInfo) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Ten: ");
  display.println(name);
  display.print("GPLX: ");
  display.println(licenseID);
  display.print("Xe: ");
  display.println(vehicleInfo);
  display.display();
}

void displayMQ3Info(float ppm, String status) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("PPM: ");
  display.println(ppm);
  display.print("Status: ");
  display.println(status);
  display.display();
}

void displayProgressBar(int duration) {
  int progress = 0;
  unsigned long startTime = millis();
  // Lấy kích thước của văn bản để căn giữa
  String message = "Please Blow";
  int16_t x, y;
  uint16_t width, height;
  display.getTextBounds(message, 0, 0, &x, &y, &width, &height);
  // Tính toán vị trí căn giữa cho thông báo "Please Blow"
  int centerX = (SCREEN_WIDTH - width) / 2;
  int centerY = (SCREEN_HEIGHT - height) / 2;
  display.clearDisplay();
  display.setCursor(centerX, centerY);  // Đặt con trỏ ở vị trí giữa
  display.print(message);
  // Hiển thị thanh tiến trình
  int barHeight = 5;  // Chiều cao thanh tiến trình
  int barY = centerY + height + 5;  // Vị trí thanh tiến trình dưới văn bản
  while (millis() - startTime < duration) {
    progress = map(millis() - startTime, 0, duration, 0, SCREEN_WIDTH);
    display.fillRect(0, barY, progress, barHeight, WHITE);
    display.display();
    delay(50);  // Cập nhật mỗi 50ms
  }
  display.clearDisplay();  // Xóa màn hình sau khi hoàn thành
}

// Hàm hiển thị lại thông báo yêu cầu quét thẻ ở giữa màn hình
void DisplayScanCardMessage() {
  display.clearDisplay();
  // Đặt con trỏ ở giữa màn hình (theo chiều ngang và chiều dọc)
  String message = "Xin moi quet the";
  int16_t x, y;
  uint16_t width, height;
  // Tính toán vị trí hiển thị chữ ở giữa màn hình
  display.getTextBounds(message, 0, 0, &x, &y, &width, &height);
  int centerX = (SCREEN_WIDTH - width) / 2;
  int centerY = (SCREEN_HEIGHT - height) / 2;
  // Vẽ chữ ở vị trí đã tính toán
  display.setCursor(centerX, centerY);
  display.print(message);
  display.display();
  //delay(5000);  // Hiển thị sau 5 giây
}

// Khai báo hàm so sánh UID
bool compareUID(byte *uid1, byte *uid2) {
  for (byte i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) {
      return false;
    }
  }
  return true;
}

void CheckNewCard() {
  if (!mfrc522.PICC_IsNewCardPresent()) {             // // Kiểm tra xem có thẻ mới không, nếu không có thẻ thì kiểm tra lại
    return;  // Không có thẻ mới, thoát hàm
  }
  // Nếu có thẻ mới, đọc UID
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;  // Không đọc được thẻ, thoát hàm
  }

  byte scannedUID[4];
  for (byte i = 0; i < 4; i++) {
    scannedUID[i] = mfrc522.uid.uidByte[i];
  }

  // Kiểm tra UID đã quét với dữ liệu tài xế
  bool validCard = false;
  for (int i = 0; i < numDrivers; i++) {
    if (compareUID(scannedUID, drivers[i].UID)) {
      Serial.println("Tài xế quẹt thẻ hợp lệ: ");
      Serial.print("Tên: ");
      Serial.print(drivers[i].name);
      Serial.print(" - Giấy phép lái xe: ");
      Serial.print(drivers[i].licenseID);
      Serial.print(" - Biển số xe: ");
      Serial.println(drivers[i].vehicleInfo);
      //displayInfo(drivers[i].name, drivers[i].licenseID, drivers[i].vehicleInfo);
      driverName = drivers[i].name;  // Lưu tên tài xế
      driverLicenseID = drivers[i].licenseID;
      driverVehicleInfo = drivers[i].vehicleInfo;
      displayInfo(driverName, driverLicenseID, driverVehicleInfo);
      validCard = true;
      cardAuthenticated = true;  // Đánh dấu thẻ là hợp lệ
      break;
    }
  }
  if (!validCard) {
    Serial.printf("The khong hop le");
    display.clearDisplay();
    display.setCursor(0,16);
    display.println("The khong hop le");
    display.display();
    delay(5000);  // Hiển thị trong 2 giây
  }
  mfrc522.PICC_HaltA();  // Dừng thẻ
  mfrc522.PCD_StopCrypto1();  // Dừng mã hóa
}

void MQ3() {
  int sensorValue = 0;
  for (int i = 0; i < 10; i++) {
    sensorValue += analogRead(MQ3_PIN);
  }
  sensorValue /= 10;  // Tính giá trị trung bình

  float sensorVolt = (float)sensorValue / 4095 * 3.3;  // Chuyển giá trị analog thành điện áp

  // Tính toán Rs từ điện áp cảm biến
  float Rs;
  if (sensorVolt > 0) {
    Rs = (3.3 * R2) / sensorVolt - R2;
  } else {
    Rs = R2;  // Nếu cảm biến không hoạt động đúng, gán Rs một giá trị an toàn
  }

  float ratio = Rs / R0;  // Tính tỷ lệ Rs/R0
  double x = 0.4 * ratio;
  float BAC = pow(x, -1.431) - 0.04;  // Tính BAC theo công thức

  if (BAC < 0) {
    BAC = 0.0;
  }

  // In giá trị điện áp và nồng độ cồn ra Serial Monitor
  Serial.print("Vout = ");
  Serial.println(sensorVolt);  // In giá trị điện áp của cảm biến
  Serial.print("Nồng độ cồn: ");
  Serial.print(BAC);
  Serial.println(" mg/L");

  // Cập nhật và hiển thị thông tin trên màn hình OLED
  String status = (BAC > Threshold) ? "Không an toàn" : "An toàn";
  displayMQ3Info(BAC, status);  // Hiển thị nồng độ cồn và trạng thái lên màn hình OLED

  // Gửi dữ liệu lên Google Sheets (hoặc server khác)
  sendDataToGoogleSheets(driverName, driverLicenseID, driverVehicleInfo, BAC, status);
}

// Hàm hiển thị thông tin nồng độ cồn và trạng thái lên màn hình OLED
void displayMQ3Info(float ppm, String status) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Nồng độ cồn: ");
  display.print(ppm);
  display.println(" mg/L");
  display.print("Trạng thái: ");
  display.println(status);
  display.display();
}


// bool DetectBreath() {
//   int sensorValue = analogRead(MQ3_PIN);  // Đọc giá trị từ cảm biến
//   sensor_volt = (float)sensorValue / 4095 * 3.3;  // Chuyển giá trị analog thành điện áp
//   RS_gas = ((3.3 * R2) / sensor_volt) - R2;  // Tính toán Rs từ điện áp
//   ratio = RS_gas / R0;  // Tính tỷ lệ Rs/R0
//   double x = 0.4 * ratio;   
//   BAC = pow(x, -1.433);  // 
//   return BAC > 0.01;  // 
// }
void setup() {
  Serial.begin(112500);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Dang ket noi WiFi...");
  }
  Serial.println("WiFi da ket noi!");
   // Khởi tạo thời gian từ NTP
  timeClient.begin();
 
  // Khởi tạo màn hình OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
    ;
  }
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.display();

  // Khởi tạo SPI và RFID
  SPI.begin(18, 19, 23, SS_PIN);
  mfrc522.PCD_Init();
  DisplayScanCardMessage();  

}
void loop() {

  CheckNewCard(); 

  if (cardAuthenticated && !breathTaken) {
    breathTaken = true;  
    delay(5000);  
    displayProgressBar(10000);  
    MQ3();  
    delay(3000);
  
    }

  } else {
    // Đặt lại trạng thái sau khi đã hoàn thành việc đo
    cardAuthenticated = false;  // Đặt lại trạng thái xác thực
    breathTaken = false;        // Đặt lại trạng thái kiểm tra nồng độ cồn
    DisplayScanCardMessage();   // Hiển thị thông báo quẹt thẻ
  }
}
