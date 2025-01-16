#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>  
#include <TinyGPSPlus.h> 

#define SCREEN_WIDTH 128  
#define SCREEN_HEIGHT 32 
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// cấu hình chân và cảm biến 
#define MQ3_PIN 34
#define RST_PIN 4       
#define SS_PIN 5        
#define Threshold 1.99

MFRC522 mfrc522(SS_PIN, RST_PIN); 

const char* ssid = "TOTOLINK_N200RE";       
const char* password = "20012002";
const char* thingsBoardURL = "https://script.google.com/macros/s/AKfycbyPTghARm49jOMi4KkkwKuGOKvaPYLsBfEEX3X1GDjaALEjDeG7qJbZ3CX_SE8RkmT_/exec" ;

// NTP Server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600; 
const int daylightOffset_sec = 0;    

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer,gmtOffset_sec , 60000); // UTC+7

struct Driver {
  byte UID[4];
  String name;
  String DriverID;
};

Driver drivers[] = {
  { { 0xF3, 0x4B, 0x62, 0x1A }, "Nguyen Van A", "1234" },
  { { 0x73, 0xF4, 0xED, 0x0D }, "Nguyen Van B", "5678" }
};

int numDrivers = sizeof(drivers) / sizeof(drivers[0]);  // Số lượng tài xế
bool cardAuthenticated = false;
bool breathTaken = false;  
String driverName;         

float  sensor_volt;
double Rs; 
float  R0 = 73.365;
double ratio;
double ppm;
int    R2 = 4700;  
String status; 

String getCurrentDateTime() {
  timeClient.update(); 
  unsigned long epochTime = timeClient.getEpochTime();
  setTime(epochTime);
  
  int currentDay = day();   
  int currentMonth = month(); 
  int currentYear = year();  
  int currentHour = hour();
  int currentMinute = minute();
  //int currentSecond = second();
  String formattedDateTime = String(currentYear) + "-" + String(currentMonth) + "-" + String(currentDay);
  //String formattedDateTime1 = String(currentHour) + ":" + String(currentMinute);
  Serial.print("Ngày giờ hiện tại: ");
  Serial.print(formattedDateTime); 
  return formattedDateTime; 
}

void sendDataToGoogleSheets(String driverName, String driverID, float ppm, String status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(thingsBoardURL);
    http.addHeader("Content-Type", "application/json");

    String customDateTime = getCurrentDateTime();
    // Tạo payload JSON để gửi
    String payload = "{\"driver_name\":\"" + driverName +
                     "\",\"driver_id\":\"" + driverID +
                     "\",\"timestamp1\":\"" + getCurrentDateTime() +
                     "\",\"bac_level\":" + String(ppm,2) + 
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

void displayInfo(String name, String driverID) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Ten: ");
  display.println(name);
  display.print("ID: ");
  display.println(driverID);
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
  String message = "Please Blow";
  int16_t x, y;
  uint16_t width, height;
  display.getTextBounds(message, 0, 0, &x, &y, &width, &height);
  int centerX = (SCREEN_WIDTH - width) / 2;
  int centerY = (SCREEN_HEIGHT - height) / 2;
  display.clearDisplay();
  display.setCursor(centerX, centerY);  
  display.print(message);
  int barHeight = 5;  
  int barY = centerY + height + 5;  
  while (millis() - startTime < duration) {
    progress = map(millis() - startTime, 0, duration, 0, SCREEN_WIDTH);
    display.fillRect(0, barY, progress, barHeight, WHITE);
    display.display();
    delay(50);  // Cập nhật mỗi 50ms
  }
  display.clearDisplay();  
}

void DisplayScanCardMessage() {
  display.clearDisplay();
  String message = "Xin moi quet the";
  int16_t x, y;
  uint16_t width, height;
  display.getTextBounds(message, 0, 0, &x, &y, &width, &height);
  int centerX = (SCREEN_WIDTH - width) / 2;
  int centerY = (SCREEN_HEIGHT - height) / 2;
  display.setCursor(centerX, centerY);
  display.print(message);
  display.display();
}

bool compareUID(byte *uid1, byte *uid2) {
  for (byte i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) {
      return false;
    }
  }
  return true;
}

void CheckNewCard() {
  if (!mfrc522.PICC_IsNewCardPresent()) {            
    return;  
  }
 
  if (!mfrc522.PICC_ReadCardSerial()) {
    return; 
  }

  byte scannedUID[4];
  for (byte i = 0; i < 4; i++) {
    scannedUID[i] = mfrc522.uid.uidByte[i];
  }

  bool validCard = false;
  for (int i = 0; i < numDrivers; i++) {
    if (compareUID(scannedUID, drivers[i].UID)) {
      Serial.println("Tài xế quẹt thẻ hợp lệ: ");
      Serial.print("Tên: ");
      Serial.print(drivers[i].name);
      Serial.print(" - ID : ");
      Serial.print(drivers[i].DriverID);
      driverName = drivers[i].name;  
      driverID = drivers[i].DriverID;
      displayInfo(driverName, driverID);
      validCard = true;
      cardAuthenticated = true;  
      break;
    }
  }
  if (!validCard) {
    Serial.printf("The khong hop le");
    display.clearDisplay();
    display.setCursor(0,16);
    display.println("The khong hop le");
    display.display();
    delay(5000);  
  mfrc522.PICC_HaltA();  
  mfrc522.PCD_StopCrypto1();  
}

void MQ3() {
    int sensorValue = 0;
    for (int i = 0; i < 10; i++) {
      sensorValue += analogRead(MQ3_PIN);
    }
    sensorValue /= 10;  
    sensor_volt = (float)sensorValue / 4095 * 3.3; 
    // Tính toán Rs từ điện áp cảm biến
    if (sensor_volt > 0) {
      Rs = (3.3 * R2) / sensor_volt - R2;
    } else {
      Rs = R2; 
    }
    ratio = Rs / R0;  
    double x = 0.4 * ratio;   
    ppm  = pow(x, -1.431)-0.04;  
    if (ppm < 0) {
      ppm = 0.00;
    }

  status = (ppm > 0.00) ? "Khong dat" : "Dat";
  displayMQ3Info(ppm, status);
  sendDataToGoogleSheets(driverName, driverID,  ppm, status);
}

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
    delay(5000);
  } else {
    cardAuthenticated = false;  
    breathTaken = false;       
    DisplayScanCardMessage();  
  }
}
