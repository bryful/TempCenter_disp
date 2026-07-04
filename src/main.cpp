
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
// #include "time.h"

#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#include "LGFX_XIAO_ESP32S3_SPI_ST7789B.hpp"
#include "secrets.h"

#define SERVER_PORT 5000

// 子機データ構造
struct SensorData
{
  int status; // 0 OK
              // 1 異常
  float temp;
  float hum;
};

struct OwnData
{
  String ip;      // IPアドレス（4バイト）
  String timeStr; // 受信した日時（文字列）
  float temp;
  float hum;
  float pres;
};

// クライアントの最大数
#define MAX_CLIENTS 6

// 画面サイズ
#define SCR_WIDTH 240
#define SCR_HEIGHT 320
#define SCR_HEAD_HEIGHT 60
#define SCR_LINE_HEIGHT 42

#define BTN_PIN 43

LGFX_XIAO_ESP32S3_SPI_ST7789B display;

extern const lgfx::U8g2font lgfxJapanGothic_8F;
extern const lgfx::U8g2font lgfxJapanGothic_12F;
extern const lgfx::U8g2font lgfxJapanGothic_16F;
extern const lgfx::U8g2font lgfxJapanGothic_24F;
extern const lgfx::U8g2font lgfxJapanGothic_28F;
extern const lgfx::U8g2font lgfxJapanGothic_36F;
// 画面描画用の裏画面
lgfx::LGFX_Sprite headbuf(&display);
lgfx::LGFX_Sprite scrbuf(&display);

Adafruit_BMP280 bmp;       // I2c 0x77
int bmp280_address = 0x77; // BMP280のI2Cアドレス
Adafruit_AHTX0 aht;        // I2c 0x38
int aht20_address = 0x38;  // AHT20のI2Cアドレス

// secrets.h のマクロから IPAddress を生成
IPAddress local_IP(SECRET_LOCAL_IP);
IPAddress gateway(SECRET_GATEWAY);
IPAddress subnet(SECRET_SUBNET);
IPAddress primaryDNS(SECRET_DNS_1);
IPAddress secondaryDNS(SECRET_DNS_2);

IPAddress tempCenterIP(SECRET_SERVER_IP);

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASSWORD;
const int port = SERVER_PORT;

SensorData children[MAX_CLIENTS]; // クライアントのセンサーデータ
OwnData OwnTemp;

unsigned long nextTime = 0;
// unsigned long blinkTime = 0;
unsigned long screenSaveTime = 0;

bool isScreenSaver = false;

unsigned long scan_timing = 30000;
unsigned long screenSaver_timing = 60000;

int scanIndex = 0;
#define LEDPIN 21

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

// 画面の消去
void DisplayClear()
{
  display.fillScreen(TFT_BLACK);
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setBrightness(80);
}
// 画面へメッセージ
void DisplayPrint(String s)
{
  DisplayClear();
  display.setBrightness(100);
  display.println(s);
  // 10秒表示
}
// ==== ディスプレイ初期化 ====
void setupDisplay()
{
  // 裏画面作成
  headbuf.createSprite(SCR_WIDTH, SCR_HEAD_HEIGHT);
  headbuf.setFont(&lgfxJapanGothic_16F);
  scrbuf.createSprite(SCR_WIDTH, SCR_LINE_HEIGHT);
  scrbuf.setFont(&lgfxJapanGothic_28F);

  display.init();
  display.setRotation(0);
  display.setFont(&lgfxJapanGothic_16F);

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.println("Booting...");
}

// -----------------------------------------------------------------------
// == == Wi - Fi と時刻初期化 == ==
bool getNTP()
{
  bool ret = false;
  display.println("get NTP");
  if (WiFi.status() != WL_CONNECTED)
  {
    DisplayPrint("ERROR getNTP WiFi Disconnect");
    return ret;
  }
  configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "time.google.com"); // 2.7.0以降, esp32コンパチ
  delay(1000);                                                                    // NTPサーバーに接続するまで待機
  time_t t;
  struct tm *timeinfo;
  t = time(NULL);
  timeinfo = localtime(&t);
  int retry = 0;
  while (!getLocalTime(timeinfo, 1000))
  {
    if (retry > 30)
    {
      retry = -1;
      break;
    }
    display.print("*");
    delay(1000);
    retry++;
  }
  display.print("\n");
  if (retry < 0)
  {
    DisplayPrint("ERROR NTP/getLocalTime()");
    ret = false;
  }
  else
  {
    display.println("NTP OK!");
    ret = true;
  }
  return ret;
}
void setupWiFiAndTime()
{

  DisplayClear();
  display.println("Connecting WiFi...");
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
  }
  // 固定IP設定

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS))
  {
    Serial.println("STA Configuration Failed");
  }

  WiFi.begin(ssid, password);

  int retry = 0;
  bool done = true;
  while (WiFi.status() != WL_CONNECTED)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      break;
      ;
    }
    display.print("*");
    delay(500);
    if (retry % 10 == 9)
    {
      WiFi.disconnect();
      WiFi.reconnect();
    }
    else if (retry > 30)
    {
      break;
      ;
    }
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    display.println("\nWiFi Connected!");
  }
  else
  {
    display.println("\nWiFi Failed");
  }

  getNTP();
}
// -----------------------------------------------------------------------
bool checkI2CDeviceConnected(uint8_t address)
{
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// ==== 自機センサーの更新 ====
bool setupAHT()
{
  bool re = false;
  if (checkI2CDeviceConnected(aht20_address))
  {
    if (!aht.begin())
    {
      Serial.println("AHT20 が見つかりません。配線チェックして下さい。");
    }
    else
    {
      Serial.println("AHT20  接続確認");
      re = true;
    };
  }
  return re;
}
bool setupBMP280()
{
  bool re = false;
  bmp280_address = 0x77; // BMP280のI2Cアドレス
  if (!checkI2CDeviceConnected(bmp280_address))
  {
    bmp280_address = 0x76; // BMP280のI2Cアドレス
    if (!checkI2CDeviceConnected(bmp280_address))
    {
      Serial.println("BMP280 が見つかりません。配線チェックして下さい。");
      return re;
    }
  }
  unsigned status;
  status = bmp.begin(bmp280_address, BMP280_CHIPID);
  if (!status)
  {
    Serial.println("BMP280が見つかりません。配線チェックして下さい。");
  }
  else
  {
    Serial.println("BMP280 接続確認");
    // Default settings from datasheet.
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
    re = true;
  }
  return re;
}
//---------------------------------------------------
void readOwnTemp()
{
  sensors_event_t humidity, temp;
  humidity.relative_humidity = 0.0f;
  temp.temperature = 0.0f;
  bool b = checkI2CDeviceConnected(aht20_address);
  if (!b)
  {
    b = setupAHT();
  }
  if (b && aht.getEvent(&humidity, &temp))
  {
    Serial.print("AHT20 Temp: ");
    Serial.print(temp.temperature);
    Serial.print(" Humidity: ");
    Serial.println(humidity.relative_humidity);
  }
  else
  {
    Serial.println("AHT20 read error");
  }
  b = checkI2CDeviceConnected(bmp280_address);
  if (!b)
  {
    b = setupBMP280();
  }
  float pres = 0;
  if (b)
  {
    Serial.print("BMP280 Pressure: ");
    pres = bmp.readPressure() / 100.0;
    Serial.println(pres);
  }
  else
  {
    Serial.println("BMP280 read error");
  }
  OwnTemp.temp = temp.temperature;
  OwnTemp.hum = humidity.relative_humidity;
  OwnTemp.pres = pres;

  // 現在時刻取得
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%Y/%m/%d %H:%M:%S", &timeinfo);

  OwnTemp.timeStr = timeStr;
}
//---------------------------------------------------
bool fetchTempCenter()
{
  bool ret = false;
  WiFiClient client;
  int retry = 0;
  bool ok = true;
  Serial.println("connect TempCenter: " + tempCenterIP.toString());
  while (!client.connect(tempCenterIP, SERVER_PORT))
  {
    Serial.print("*");
    delay(350);
    retry++;
    if (retry > 2)
    {
      ok = false;
      break;
    }
  }
  if (!ok)
  {
    Serial.println("connect error! TempCenter");
    return ret;
  }
  if (client.connected())
  {
    client.print("GET_DATA\n");
    Serial.println("GET_DATA");

    String recvData;
    unsigned long start = millis();
    while (client.available() == 0 && millis() - start < 2000)
    {
      delay(10); // 最大2秒待機
    }
    while (client.available())
    {
      recvData += (char)client.read();
    }

    // 応答をJSONとして解析
    if (recvData.length() > 0)
    {
      Serial.println("Received data from TempCenter: " + recvData);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, recvData);
      if (error)
      {
        Serial.println("deserializeJson() failed");
        return false;
      }

      JsonArray sensors = doc["sensors"].as<JsonArray>();
      if (sensors.isNull())
      {
        Serial.println("sensors payload missing");
        return false;
      }

      for (int i = 0; i < MAX_CLIENTS; i++)
      {
        children[i].temp = 0.0f;
        children[i].hum = 0.0f;
        children[i].status = -1;
      }

      int idx = 0;
      for (JsonObject sensor : sensors)
      {
        if (idx >= MAX_CLIENTS)
        {
          break;
        }

        int status = sensor["status"] | 0;
        if (status == 0)
        {
          JsonVariantConst tempVar = sensor["aht_temp"];
          JsonVariantConst humVar = sensor["humidity"];
          if (tempVar.is<float>() && humVar.is<float>())
          {
            children[idx].temp = tempVar.as<float>();
            children[idx].hum = humVar.as<float>();
            children[idx].status = status;
          }
          else
          {
            children[idx].temp = 0.0f;
            children[idx].hum = 0.0f;
            children[idx].status = -1;
          }
        }
        else
        {
          children[idx].temp = 0.0f;
          children[idx].hum = 0.0f;
          children[idx].status = -1;
        }
        idx++;
      }

      ret = true;
    }
  }
  else
  {
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      children[i].status = -1;
    }
    Serial.println("Connect Err");
  }
  client.stop();
  return ret;
}
//---------------------------------------------------

void checkSensor()
{
  Serial.println("checkSensor");
  display.println("checkSensor");
  fetchTempCenter();
}
//---------------------------------------------------

String toSuji(float v)
{
  if (v < 0)
    v *= -1;
  int v1 = (int)v;
  int v2 = (int)((v - v1) * 10);
  String ret = String(v1) + "." + String(v2);
  if (v < 10)
  {
    ret = " " + ret;
  }
  return ret;
}
// ==== 上部表示の更新 ====
void headbudPrint(int col)
{

  headbuf.fillScreen(TFT_BLACK);
  headbuf.setTextSize(1);
  headbuf.setTextColor(col);
  headbuf.setCursor(10, 2);
  headbuf.println("Temp-Hum Sensor by bry-ful");
  headbuf.setCursor(30, 20);
  headbuf.printf("%s-%.1f", OwnTemp.timeStr.c_str(), ((float)scan_timing / 1000));
  headbuf.setCursor(30, 38);
  headbuf.setTextSize(1);
  headbuf.printf("%.1f℃ %.1f%% %.1fpHa", OwnTemp.temp, OwnTemp.hum, OwnTemp.pres);
  headbuf.drawRoundRect(0, 0, SCR_WIDTH - 1, SCR_HEAD_HEIGHT - 1, 6, col);
  headbuf.setTextSize(1);
}
// ==== クライアント表示の更新 ====
void scrbudPrint(int index)
{
  if (index >= MAX_CLIENTS)
    return;
  String id = String(index + 1);
  float t = children[index].temp;
  float h = children[index].hum;
  int mode = 0;
  //
  //  0 normal
  //  1 Count err /Disconnect
  //
  int col = TFT_WHITE;
  if (children[index].status != 0)
  {
    mode = 1;
    col = TFT_RED;
  }
  bool b = false;
  b = (((OwnTemp.hum - h < 10) || (h > 30)) && (mode == 0));
  scrbuf.fillScreen(TFT_BLACK);

  // ＩＤの描画
  scrbuf.setTextSize(1);
  scrbuf.setTextColor(col);
  scrbuf.setCursor(15, 1);
  scrbuf.setFont(&lgfxJapanGothic_16F);
  scrbuf.print(id);
  // 温度
  scrbuf.setTextSize(1);
  scrbuf.setCursor(15, 17);
  scrbuf.setFont(&lgfxJapanGothic_24F);
  if (mode == 1)
  {
    scrbuf.print("--.-");
  }
  else
  {
    scrbuf.print(toSuji(t));
  }
  if (t < 0)
  {
    scrbuf.drawLine(5, 12, 12, 12);
  }
  scrbuf.setFont(&lgfxJapanGothic_16F);
  scrbuf.setCursor(65, 25);
  scrbuf.print("℃");

  // 下線
  scrbuf.drawLine(10, SCR_LINE_HEIGHT - 1, SCR_WIDTH - 10, SCR_LINE_HEIGHT - 1);

  // メーター
  int xp = 110;
  int yp = 25;
  int st = 90 + 45;
  int en = 360 + 45;
  int ea = st + (en - st) * h / 100;
  scrbuf.fillArc(xp, yp, 14, 10, st, en, TFT_DARKGRAY);
  scrbuf.fillArc(xp, yp, 14, 10, st, ea, col);
  int l = 18;
  double hr = st + (en - st) * (double)OwnTemp.hum / 100;

  double x = (cos(hr * PI / 180) * l);
  double y = (sin(hr * PI / 180) * l);
  scrbuf.drawArc(xp, yp, 18, 18, st, (int)hr, col);
  scrbuf.drawLine((int)(xp + x * 0.1), (int)(yp + y * 0.1), (int)(xp + x), (int)(yp + y), col);
  scrbuf.drawCircle(xp, yp, 6, col);
  // 湿度
  scrbuf.setCursor(130, 6);
  scrbuf.setFont(&lgfxJapanGothic_36F);
  if ((b) && (mode == 0))
  {
    scrbuf.setTextColor(TFT_YELLOW);
  }
  if (mode == 1)
  {
    scrbuf.print("--.-");
  }
  else
  {
    scrbuf.print(toSuji(h));
  }
  scrbuf.setFont(&lgfxJapanGothic_24F);
  scrbuf.setCursor(205, 16);
  scrbuf.print("%");
}
// ==== フッター表示の更新 ====
void footorPrint(int col)
{

  scrbuf.fillScreen(TFT_BLACK);

  int bitV = (millis() >> 3) & 0b11111;
  for (int i = 0; i < 5; i++)
  {
    if (bitV & 0x01 == 0x01)
    {
      scrbuf.fillRect(20 + i * 30, 2, 24, 6, col);
    }
    bitV = bitV >> 1;
    scrbuf.drawRect(20 + i * 30, 2, 24, 6, col);
  }
  scrbuf.setCursor(180, 0);
  scrbuf.setFont(&lgfxJapanGothic_8F);
  scrbuf.setTextColor(TFT_RED);
  scrbuf.setTextSize(1);
  scrbuf.println(WiFi.localIP().toString());
}
// ==== 画面表示の更新 ====
void PrintScrren()
{
  if (isScreenSaver)
  {
    return;
  }
  int col = TFT_WHITE;
  if (WiFi.status() != WL_CONNECTED)
  {
    col = TFT_RED;
  }
  headbudPrint(col);
  headbuf.pushSprite(0, 0);

  int idx = 0;
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    scrbudPrint(i);
    scrbuf.pushSprite(0, SCR_HEAD_HEIGHT + SCR_LINE_HEIGHT * i);
  }

  footorPrint(TFT_WHITE);
  scrbuf.pushSprite(0, SCR_HEAD_HEIGHT + SCR_LINE_HEIGHT * 6);
}
//---------------------------------------------------

//---------------------------------------------------
void setup()
{
  Serial.begin(115200);
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW); // Turn off the LED

  pinMode(BTN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  setupDisplay();
  display.println("Starting Sensor Control...");
  setupWiFiAndTime();

  //---------------------------------------------------
  setupAHT();
  setupBMP280();
  checkSensor();
  readOwnTemp();
  PrintScrren();
  nextTime = millis();
  screenSaveTime = millis();
}

void loop()
{
  if (digitalRead(BTN_PIN) == LOW)
  {
    if (isScreenSaver)
    {
      isScreenSaver = false;
      display.setBrightness(80); // 画面を明るくする
      screenSaveTime = millis(); // スクリーンセーバーのタイマーをリセット
      nextTime = 0;              // タイマーをリセットしてすぐに画面を更新
    }
    else
    {
      isScreenSaver = true;
      display.setBrightness(0); // 画面を暗くする
    }
    delay(500); // ボタンのチャタリング防止
  }
  if (!isScreenSaver)
  {
    if (millis() - screenSaveTime > screenSaver_timing) //
    {
      // スクリーンセーバーの処理
      isScreenSaver = true;
      // screenSaveTime = millis();
      display.setBrightness(0); // 画面を暗くする
    }
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    if (isScreenSaver)
    {
      isScreenSaver = false;
      display.setBrightness(80); // 画面を明るくする
      screenSaveTime = millis(); // スクリーンセーバーのタイマーをリセット
    }
    setupWiFiAndTime();
    nextTime = 0; // タイマーをリセットしてすぐに画面を更新
  }
  if (millis() - nextTime > scan_timing) //
  {
    readOwnTemp();
    checkSensor();
    // 正常か確認
    bool err = false;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (children[i].status == 0)
        continue;
      if (children[i].hum >= 35.0f)
      {
        // 湿度が高いクライアント
        err = true;
        break;
      }
    }
    if (err)
    {
      display.setBrightness(100); // 画面を明るくする
      screenSaveTime = millis();  // スクリーンセーバーのタイマーをリセット
    }
    if (!isScreenSaver)
    {
      PrintScrren();
    }
    nextTime = millis();
  }
}
