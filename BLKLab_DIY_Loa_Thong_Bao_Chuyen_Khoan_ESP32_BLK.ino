// ----------------BANLINHKIEN.COM--------------
// BLKLab : Loa Thông Báo Chuyển Khoản ESP32 BLK 
// Ngày : 17/02/2025
// Hướng dẫn sử dụng:
// Màn hình chính mặc định hiển thị 3 giao dịch tiền vào gần nhất
// Chức năng nút nhấn BOOT:
//   - Nhấn nhanh 1 lần để tăng âm lượng:
//   - Nhấn giữ 3s để vào chế độ cài đặt: Giữ nút BOOT trên kit esp32 để vào chế độ APMODE, esp32 sẽ phát ra wifi tên Tingbox, 
//     Kết nối vào wifi đó, truy cập 192.168.4.1, cấu hình các thông số:
//       + tên WIFI
//       + Mật khẩu WIFI
//       + API Token của Sepay

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index_html.h"
#include "data_config.h"
#include <EEPROM.h>
#include <Arduino_JSON.h>
#include "icon.h"
#include <HTTPClient.h>
#include "Audio.h"


// Khai báo I2S
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26
Audio audio;
bool isPlaying = false;
HTTPClient http;

struct Transaction {
    String id;
    String bank_brand_name;
    String account_number;
    String transaction_date;
    int amount_out;
    int amount_in;
    String transaction_content;
    String reference_number;
    String sub_account;
    String bank_account_id;
};

Transaction transactions[3];
int soLuongGD = 0;

const char* apiUrl = "https://my.sepay.vn/userapi/transactions/list?limit=3";
String url_ting_ting = "https://tiengdong.com/wp-content/uploads/Tieng-tinh-tinh-www_tiengdong_com.mp3";
String reference_number_now = "";  // Biến lưu mã tham chiếu từ lần đọc trước 

uint8_t trigAudio = 0;
int amount_inGiaoDich = 0;
uint8_t firstTrans = 0;
int countTaskReadSepay = 0;
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);


// Một số Macro
#define ENABLE    1
#define DISABLE   0
// ---------------------- Khai báo cho OLED 1.3 --------------------------
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3C //initialize with the I2C addr 0x3C Typically eBay OLED's
//#define i2c_Address 0x3d //initialize with the I2C addr 0x3D Typically Adafruit OLED's
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO
Adafruit_SH1106G oled = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define NUMFLAKES 10
#define XPOS 0
#define YPOS 1
#define DELTAY 2

#define OLED_SDA      21
#define OLED_SCL      22

typedef enum {
  SCREEN0,
  SCREEN1,
  SCREEN2,
  SCREEN3,
  SCREEN4,
  SCREEN5,
  SCREEN6,
  SCREEN7,
  SCREEN8,
  SCREEN9,
  SCREEN10,
  SCREEN11,
  SCREEN12,
  SCREEN13
}SCREEN;
int screenOLED = SCREEN0;
int countSCREEN9 = 0;
bool enableShow = DISABLE;
int Volume = 10;
// Khai bao LED
#define LED           2
// Khai báo BUZZER
#define BUZZER        4
#define BUZZER_ON     0
#define BUZZER_OFF    1
//-------------------- Khai báo Button-----------------------
#include "mybutton.h"

#define BUTTON_SET_PIN    0

#define BUTTON1_ID  1

Button buttonSET;
Button buttonDOWN;
Button buttonUP;
void button_press_short_callback(uint8_t button_id);
void button_press_long_callback(uint8_t button_id);
//------------------------------------------------------------
TaskHandle_t TaskButton_handle      = NULL;
TaskHandle_t TaskOLEDDisplay_handle = NULL;
TaskHandle_t TaskDHT11_handle = NULL;
TaskHandle_t TaskDustSensor_handle = NULL;
TaskHandle_t TaskAutoWarning_handle = NULL;

void setup(){
  Serial.begin(115200);
  // Đọc data setup từ eeprom
  EEPROM.begin(512);
  readEEPROM();
    // Khởi tạo LED
  pinMode(LED, OUTPUT);
  // Khởi tạo BUZZER
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, BUZZER_OFF);
  // Khởi tạo OLED
  oled.begin(i2c_Address, true);
  oled.setTextSize(2);
  oled.setTextColor(SH110X_WHITE);

  // Khởi tạo nút nhấn
  pinMode(BUTTON_SET_PIN, INPUT_PULLUP);

  button_init(&buttonSET, BUTTON_SET_PIN, BUTTON1_ID);
  button_pressshort_set_callback((void *)button_press_short_callback);
  button_presslong_set_callback((void *)button_press_long_callback);
  xTaskCreatePinnedToCore(TaskReadSepay, "TaskReadSepay" ,      1024*20 ,  NULL,  24 ,  NULL ,  1);
  xTaskCreatePinnedToCore(TaskButton,          "TaskButton" ,          1024*10 ,  NULL,  10 ,  &TaskButton_handle       , 1);
  Evolume = EEPROM.read(220);
  if(Evolume == 255) Evolume = 10;
  Serial.print("Volume:");
  Serial.println(Evolume);
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(Evolume);
  
  // Kết nối wifi
  connectSTA();
}

void loop() {
  vTaskDelete(NULL);
}
int tienNhan[3] = {0,0,0};
void themTien(int i) {
    // Dịch các phần tử sang trái
    tienNhan[0] = tienNhan[1];
    tienNhan[1] = tienNhan[2];
    tienNhan[2] = i; // Gán giá trị mới vào phần tử cuối
}
bool enableReadSepay = 0;
void TaskReadSepay(void *pvParameters) {
    while(1) {
        countTaskReadSepay ++;
        Serial.println(countTaskReadSepay);
        if(trigAudio == 0 && countTaskReadSepay >= 6 && enableReadSepay == 1) {
          countTaskReadSepay = 0;
          if (WiFi.status() == WL_CONNECTED && trigAudio == 0) {
              // Lấy thông tin 3 giao dịch gần nhất
              HTTPClient http;
              http.begin(apiUrl);
              http.addHeader("Authorization", String("Bearer ") + EApiKey.c_str());
              http.addHeader("Content-Type", "application/json");
              
              int httpResponseCode = http.GET();
              if (httpResponseCode > 0) {
                  String response = http.getString();
                  Serial.println("Response:");
                  Serial.println(response);
      
                  JSONVar jsonObj = JSON.parse(response);
                  if (JSON.typeof(jsonObj) == "undefined") {
                      Serial.println("Failed to parse JSON");
                      return;
                  }
                  
                  JSONVar transArray = jsonObj["transactions"];
                  int i = 0;
                  for (int j = 0; j < 3; j++) {
                      transactions[i].id = (const char*)transArray[j]["id"];
                      transactions[i].bank_brand_name = (const char*)transArray[2-j]["bank_brand_name"];
                      transactions[i].account_number = (const char*)transArray[2-j]["account_number"];
                      transactions[i].transaction_date = (const char*)transArray[2-j]["transaction_date"];
                      transactions[i].amount_out = String((const char*)transArray[2-j]["amount_out"]).toInt();
                      transactions[i].amount_in = String((const char*)transArray[2-j]["amount_in"]).toInt();
                      transactions[i].transaction_content = (const char*)transArray[2-j]["transaction_content"];
                      transactions[i].reference_number = (const char*)transArray[2-j]["reference_number"];
                      transactions[i].sub_account = (const char*)transArray[2-j]["sub_account"];
                      transactions[i].bank_account_id = (const char*)transArray[2-j]["bank_account_id"];
                      i++;
                      if (i >= 3) break;
                  }
              } else {
                  Serial.print("Error on HTTP request: ");
                  Serial.println(httpResponseCode);
              }
              http.end();
            // Kiểm tra nếu code == 200 là đọc thành công
            if (httpResponseCode == 200) {
              // ledBlink(1);
              if(firstTrans == 0) {
                firstTrans = 1;
                reference_number_now = transactions[2].reference_number;
                Serial.println("reference_number_now");
                Serial.println(reference_number_now);
                Serial.println("transactions[2].reference_number");
                Serial.println(transactions[2].reference_number);
                tienNhan[0] = transactions[0].amount_in;
                tienNhan[1] = transactions[1].amount_in;
                tienNhan[2] = transactions[2].amount_in;
              } else {
                  // Kiểm tra có giao dịch mới hay không.
                  if(trigAudio == 0 ) {
                    Serial.println(transactions[2].reference_number);
                    int viTri = kiemTrareference_number(reference_number_now);
                    if (viTri != -1) {
                      if(viTri == 0) {
                        reference_number_now = transactions[1].reference_number;
                        amount_inGiaoDich = transactions[1].amount_in;
                        
                        if(amount_inGiaoDich > 0  && String(EVAacount.c_str()) == transactions[1].sub_account) {
                          themTien(amount_inGiaoDich);
                          trigAudio = 1;
                          vTaskDelete(TaskButton_handle);
                          printTransactions(1);
                        }
                        
                      } else if(viTri == 1 ) {
                        reference_number_now = transactions[2].reference_number;
                        amount_inGiaoDich = transactions[2].amount_in;
                        if(amount_inGiaoDich > 0 && String(EVAacount.c_str()) == transactions[2].sub_account) {
                          trigAudio = 1;
                          themTien(amount_inGiaoDich);
                          vTaskDelete(TaskButton_handle);
                          printTransactions(2);
                        }
                        
                      } else {
                        Serial.println("Không có giao dịch mới");
                      }
                    } else {
                          Serial.println("Mã tham chiếu không tồn tại trong danh sách.");
                          Serial.println(reference_number_now);
                          reference_number_now = transactions[0].reference_number;
                          amount_inGiaoDich = transactions[0].amount_in;
                          if(amount_inGiaoDich > 0 && String(EVAacount.c_str()) == transactions[0].sub_account) {
                            trigAudio = 1;
                            themTien(amount_inGiaoDich);
                            vTaskDelete(TaskButton_handle); 
                            printTransactions(0);  
                          }
                          
                    }
                  }
              }
            } else {
              Serial.println("Lỗi lấy dữ liệu. Vui lòng đợi lần lấy dữ liệu tiếp theo !");
            }
          }
        }
       // phát âm thanh khi có giao dịch mới
        if (!isPlaying && trigAudio == 1) {  
            Serial.println("Playing audio...");
            isPlaying = 1;
            enableShow = DISABLE;
            audio.connecttohost(url_ting_ting.c_str());  // Phát tiếng ting ting trước
            audio.loop();
        }
        if (isPlaying) {
          if (audio.isRunning() && trigAudio > 0) {
            audio.loop();
          } else {
            if(trigAudio == 1) {
                Serial.println("Audio finished 1");
                trigAudio = 2;
                String message = "Thanh toán thành công " + String(amount_inGiaoDich) + " đồng";
                const char* messageChar = message.c_str();
                audio.connecttospeech(messageChar, "vi");   // Phát âm thanh thanh toán thành công
            } else if (trigAudio == 2) {
                isPlaying = 0;  // Reset để lần sau có thể phát tiếp
                trigAudio = 0;
                enableShow = ENABLE;
                xTaskCreatePinnedToCore(TaskButton,          "TaskButton" ,          1024*10 ,  NULL,  5 ,   &TaskButton_handle       , 1);
                Serial.println("Audio finished 2");
            } 
          }
        }

        switch(screenOLED) {
          case SCREEN0: // Hiệu ứng khởi động
            for(int j = 0; j < 3; j++) {
              for(int i = 0; i < FRAME_COUNT_loadingOLED; i++) {
                oled.clearDisplay();
                oled.drawBitmap(32, 0, loadingOLED[i], FRAME_WIDTH_64, FRAME_HEIGHT_64, 1);
                oled.display();
                delay(FRAME_DELAY/4);
              }
            }
            screenOLED = SCREEN4;
            break;
          case SCREEN1:  
            for(int i = 0; i < FRAME_COUNT_Money && enableShow == ENABLE; i++) {
              oled.clearDisplay();
              oled.setTextSize(1.5);
              oled.setCursor(60, 5);
              oled.print(String(tienNhan[0]) + " vnd");
              oled.setCursor(60, 25);
              oled.print(String(tienNhan[1]) + " vnd");
              oled.setCursor(60, 45);
              oled.print(String(tienNhan[2]) + " vnd");
              clearRectangle(0, 0, 32, 64);
              oled.drawBitmap(16, 16, moneyOLED[i], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
              oled.display();
              delay(FRAME_DELAY);
            }
            oled.display();

            break;
          case SCREEN2:   
            oled.clearDisplay();
            oled.setTextSize(2);
            oled.setCursor(60, 25);
            oled.print(String(Evolume) + "%");
            if(Evolume == 0) {  
              oled.drawBitmap(16, 16, speakerMuteOLED[FRAME_COUNT_speakerMuteOLED - 1], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
            }
            else {
              oled.drawBitmap(16, 16, speakerOLED[FRAME_COUNT_speakerOLED - 1], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
            }
            oled.display();
            delay(FRAME_DELAY);
            oled.display();

            delay(300);
            enableShow = ENABLE;
            if( enableShow == ENABLE)
              screenOLED = SCREEN3;
            break;
          case SCREEN3:  

            delay(1000);
            if( enableShow == ENABLE)
              screenOLED = SCREEN1;
            break; 
          case SCREEN4:    // Đang kết nối Wifi
            oled.clearDisplay();
            oled.setTextSize(1);
            oled.setCursor(40, 5);
            oled.print("WIFI");
            oled.setTextSize(1.5);
            oled.setCursor(40, 17);
            oled.print("Dang ket noi..");
        
            for(int i = 0; i < FRAME_COUNT_wifiOLED; i++) {
              clearRectangle(0, 0, 32, 32);
              oled.drawBitmap(0, 0, wifiOLED[i], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
              oled.display();
              delay(FRAME_DELAY);
            }
            break;
          case SCREEN5:    // Kết nối wifi thất bại
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(40, 5);
              oled.print("WIFI");
              oled.setTextSize(1.5);
              oled.setCursor(40, 17);
              oled.print("Mat ket noi.");
              oled.drawBitmap(0, 0, wifiOLED[FRAME_COUNT_wifiOLED - 1 ], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
              oled.drawLine(31, 0 , 0, 31 , 1);
              oled.drawLine(32, 0 , 0, 32 , 1);
              oled.display();
              delay(2000);
              screenOLED = SCREEN9;
            break;
          case SCREEN6:   // Đã kết nối Wifi, đang kết nối Blynk

            break;
          case SCREEN7:   // Đã kết nối Wifi
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(40, 5);
              oled.print("WIFI");
              oled.setTextSize(1.5);
              oled.setCursor(40, 17);
              oled.print("Da ket noi.");
              oled.drawBitmap(0, 0, wifiOLED[FRAME_COUNT_wifiOLED - 1 ], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
              delay(3000);
              screenOLED = SCREEN1;
              enableShow = ENABLE;
            break;
          case SCREEN8:   
              oled.clearDisplay();
              delay(2000);
              screenOLED = SCREEN9;
            break;
          case SCREEN9:   // Cai đặt 192.168.4.1
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(40, 5);
              oled.setTextSize(1);
              oled.print("Ket noi Wifi:");
              oled.setCursor(40, 17);
              oled.setTextSize(1);
              oled.print("TingBox");

              oled.setCursor(40, 38);
              oled.print("Dia chi IP:");
      
              oled.setCursor(40, 50);
              oled.print("192.168.4.1");

              for(int i = 0; i < FRAME_COUNT_settingOLED; i++) {
                clearRectangle(0, 0, 32, 64);
                oled.drawBitmap(0, 16, settingOLED[i], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
                oled.display();
                delay(FRAME_DELAY*4);
              }
              countSCREEN9++;
              if(countSCREEN9 > 10) {
                countSCREEN9 = 0;
                screenOLED = SCREEN1;
                enableShow = ENABLE;
              }

              break;
            case SCREEN10:    // auto : on
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(40, 20);
              oled.print("Canh bao:");
              oled.setTextSize(2);
              oled.setCursor(40, 32);
              oled.print("DISABLE"); 
              for(int i = 0; i < FRAME_COUNT_autoOnOLED; i++) {
                clearRectangle(0, 0, 32, 64);
                oled.drawBitmap(0, 16, autoOnOLED[i], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
                oled.display();
                delay(FRAME_DELAY);
              }
              clearRectangle(40, 32, 128, 64);
              oled.setCursor(40, 32);
              oled.print("ENABLE"); 
              oled.display();   
              delay(2000);
              screenOLED = SCREEN1;
              enableShow = ENABLE;
              break;
            case SCREEN11:     // auto : off
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(40, 20);
              oled.print("Canh bao:");
              oled.setTextSize(2);
              oled.setCursor(40, 32);
              oled.print("ENABLE");
              for(int i = 0; i < FRAME_COUNT_autoOffOLED; i++) {
                clearRectangle(0, 0, 32, 64);
                oled.drawBitmap(0, 16, autoOffOLED[i], FRAME_WIDTH_32, FRAME_HEIGHT_32, 1);
                oled.display();
                delay(FRAME_DELAY);
              }
              clearRectangle(40, 32, 128, 64);
              oled.setCursor(40, 32);
              oled.print("DISABLE"); 
              oled.display();    
              delay(2000);
              screenOLED = SCREEN1;  
              enableShow = ENABLE;
              break;
            case SCREEN12:  // gui du lieu len blynk

              delay(1000);
              screenOLED = SCREEN1; 
              enableShow = ENABLE;
              break;
            case SCREEN13:   // khoi dong lai
              oled.clearDisplay();
              oled.setTextSize(1);
              oled.setCursor(0, 20);
              oled.print("Khoi dong lai");
              oled.setCursor(0, 32);
              oled.print("Vui long doi ..."); 
              oled.display();
              break;
            default : 
              delay(10);
              break;
        } 
           
    }
}

// In giao dịch
void printTransactions(int i) {
    Serial.println("\nTransactions:");
    Serial.println("--------------------");
    Serial.println("ID: " + transactions[i].id);
    Serial.println("Bank: " + transactions[i].bank_brand_name);
    Serial.println("Account: " + transactions[i].account_number);
    Serial.println("Date: " + transactions[i].transaction_date);
    Serial.print("Amount In: "); Serial.println(transactions[i].amount_in);
    Serial.print("Amount Out: "); Serial.println(transactions[i].amount_out);
    Serial.println("Content: " + transactions[i].transaction_content);
    Serial.println("Reference: " + transactions[i].reference_number);
    Serial.println("Sub Account: " + transactions[i].sub_account);
    Serial.println("Bank Account ID: " + transactions[i].bank_account_id);
    Serial.println("--------------------");
}



// Hàm kiểm tra mã tham chiếu có trong danh sách không
int kiemTrareference_number(String reference_number) {
    for (int i = 0; i < 3; i++) {
        if (transactions[i].reference_number == reference_number) {
            return i; // Trả về vị trí nếu tìm thấy
        }
    }
    return -1; // Không tìm thấy
}
// Xóa 1 ô hình chữ nhật từ tọa độ (x1,y1) đến (x2,y2)
void clearRectangle(int x1, int y1, int x2, int y2) {
   for(int i = y1; i < y2; i++) {
     oled.drawLine(x1, i, x2, i, 0);
   }
}

void clearOLED(){
  oled.clearDisplay();
  oled.display();
}


//-----------------Kết nối STA wifi, chuyển sang wifi AP nếu kết nối thất bại ----------------------- 
void connectSTA() {
      delay(5000);
      enableShow = DISABLE;
      if ( Essid.length() > 1 ) {  
      Serial.println(Essid);        //Print SSID
      Serial.println(Epass);        //Print Password

      WiFi.begin(Essid.c_str(), Epass.c_str());   //c_str()
      int countConnect = 0;
      while (WiFi.status() != WL_CONNECTED) {
          delay(500);   
          if(countConnect++  == 15) {
            Serial.println("Ket noi Wifi that bai");
            Serial.println("Kiem tra SSID & PASS");
            Serial.println("Ket noi Wifi: ESP32 de cau hinh");
            Serial.println("IP: 192.168.4.1");
            screenOLED = SCREEN5;
            digitalWrite(BUZZER, BUZZER_ON);
            delay(2000);
            digitalWrite(BUZZER, BUZZER_OFF);
            delay(3000);
            break;
          }
          // MODE đang kết nối wifi
          screenOLED = SCREEN4;
          delay(2000);
      }
      Serial.println("");
      if(WiFi.status() == WL_CONNECTED) {
          Serial.println("Da ket noi Wifi: ");
          Serial.println("IP address: ");
          Serial.println(WiFi.localIP()); 
          Serial.println((char*)Essid.c_str());
        buzzerBeep(3);
        // MODE wifi đã kết nối, đang kết nối blynk
        enableShow = ENABLE;
        screenOLED = SCREEN7;
        enableReadSepay = 1;
        delay(2000);
        
        
      }
      else {
        digitalWrite(BUZZER, BUZZER_ON);
        delay(2000);
        digitalWrite(BUZZER, BUZZER_OFF);
        // MODE truy cập vào 192.168.4.1
        screenOLED = SCREEN9;
        connectAPMode(); 
      }
        
    }
}

//--------------------------- switch AP Mode --------------------------- 
void connectAPMode() {

  // Khởi tạo Wifi AP Mode, vui lòng kết nối wifi ESP32, truy cập 192.168.4.1
  WiFi.softAP(ssidAP, passwordAP);  

  // Gửi trang HTML khi client truy cập 192.168.4.1
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Gửi data ban đầu đến clientgetDataFromClient
  server.on("/data_before", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = getJsonData();
    request->send(200, "application/json", json);
  });

  // Get data từ client
  server.on("/post_data", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "SUCCESS");
    enableShow = DISABLE;
    screenOLED = SCREEN13;
    delay(5000);
    ESP.restart();
  }, NULL, getDataFromClient);

  // Start server
  server.begin();
}

//------------------- Hàm đọc data từ client gửi từ HTTP_POST "/post_data" -------------------
void getDataFromClient(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  Serial.print("get data : ");
  Serial.println((char *)data);
  JSONVar myObject = JSON.parse((char *)data);
  if(myObject.hasOwnProperty("ssid"))
    Essid = (const char*) myObject["ssid"];
  if(myObject.hasOwnProperty("pass"))
    Epass = (const char*)myObject["pass"] ;
  if(myObject.hasOwnProperty("api_key"))
    EApiKey = (const char*)myObject["api_key"] ;
  if(myObject.hasOwnProperty("va_acount"))
    EVAacount = (const char*)myObject["va_acount"] ;
  writeEEPROM();
  

}

// ------------ Hàm in các giá trị cài đặt ------------
void printValueSetup() {
    Serial.print("ssid = ");
    Serial.println(Essid);
    Serial.print("pass = ");
    Serial.println(Epass);
    Serial.print("api_key = ");
    Serial.println(EApiKey);
    Serial.print("va_acount = ");
    Serial.println(EVAacount);
}

//-------- Hàm tạo biến JSON để gửi đi khi có request HTTP_GET "/" --------
String getJsonData() {
  JSONVar myObject;
  myObject["ssid"]  = Essid;
  myObject["pass"]  = Epass;
  myObject["api_key"]  = EApiKey;
  myObject["va_acount"]  = EVAacount;


  String jsonData = JSON.stringify(myObject);
  return jsonData;
}

/*
 * Các hàm liên quan đến lưu dữ liệu cài đặt vào EEPROM
*/
//--------------------------- Read Eeprom  --------------------------------
void readEEPROM() {
    for (int i = 0; i < 32; ++i)       //Reading SSID
        Essid += char(EEPROM.read(i)); 
    for (int i = 32; i < 64; ++i)      //Reading Password
        Epass += char(EEPROM.read(i)); 
    for (int i = 64; i < 200; ++i)      //Reading API Key
        EApiKey += char(EEPROM.read(i)); 
    for (int i = 200; i < 230; ++i)      //Reading EVAacount
        EVAacount += char(EEPROM.read(i)); 
    if(Essid.length() == 0) Essid = "BLK";

    printValueSetup();
}

// ------------------------ Clear Eeprom ------------------------
void clearEeprom() {
    Serial.println("Clearing Eeprom");
    for (int i = 0; i < 250; ++i) 
      EEPROM.write(i, 0);
}

// -------------------- Hàm ghi data vào EEPROM ------------------
void writeEEPROM() {
    clearEeprom();
    for (int i = 0; i < Essid.length(); ++i)
          EEPROM.write(i, Essid[i]);  
    for (int i = 0; i < Epass.length(); ++i)
          EEPROM.write(32+i, Epass[i]);
    for (int i = 0; i < EApiKey.length(); ++i)
          EEPROM.write(64+i, EApiKey[i]);
    for (int i = 0; i < EVAacount.length(); ++i)
          EEPROM.write(200+i, EVAacount[i]);
    EEPROM.commit();
    Serial.println("write eeprom");
    delay(500);
}


//-----------------------Task Task Button ----------
void TaskButton(void *pvParameters) {
    while(1) {
      handle_button(&buttonSET);
      delay(10);
    }
}
//-----------------Hàm xử lí nút nhấn nhả ----------------------
void button_press_short_callback(uint8_t button_id) {
    switch(button_id) {
      case BUTTON1_ID :  
        buzzerBeep(1);
        Serial.println("btSET press short");
        screenOLED = SCREEN2;
        enableShow = DISABLE;
        Evolume = Evolume + 10;

        if(Evolume > 100) Evolume = 0;
        audio.setVolume(Evolume);
        EEPROM.write(220, Evolume);
        EEPROM.commit();
        break;

    } 
} 
//-----------------Hàm xử lí nút nhấn giữ ----------------------
void button_press_long_callback(uint8_t button_id) {
  switch(button_id) {
    case BUTTON1_ID :
      buzzerBeep(2);  
      enableShow = DISABLE;
      Serial.println("btSET press long");
      screenOLED = SCREEN9;
      clearOLED();
      connectAPMode(); 
      break;
 
  } 
} 
// ---------------------- Hàm điều khiển còi -----------------------------
void buzzerBeep(int numberBeep) {
  for(int i = 0; i < numberBeep; ++i) {
    digitalWrite(BUZZER, BUZZER_ON);
    delay(100);
    digitalWrite(BUZZER, BUZZER_OFF);
    delay(100);
  }  
}
// ---------------------- Hàm điều khiển còi -----------------------------
void ledBlink(int numberBeep) {
  for(int i = 0; i < numberBeep; ++i) {
    digitalWrite(LED, ENABLE);
    delay(200);
    digitalWrite(LED, ENABLE);
    delay(100);
  }  
}
// ---------------------- Hàm điều khiển LED -----------------------------
void blinkLED(int numberBlink) {
  for(int i = 0; i < numberBlink; ++i) {
    digitalWrite(LED, DISABLE);
    delay(300);
    digitalWrite(LED, ENABLE);
    delay(300);
  }  
}
