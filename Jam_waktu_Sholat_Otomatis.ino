#include <DMDESP.h>
#include <Wire.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <fonts/SystemFont5x7.h>
#include "PrayerTimes.h"

#define FontDefault SystemFont5x7
#define DISPLAYS_WIDE 2
#define DISPLAYS_HIGH 1
DMDESP Disp(DISPLAYS_WIDE, DISPLAYS_HIGH);

#define EditPin 2
#define UpPin 10
#define DownPin 9
#define BuzzerPin 3

const char* ssid = "ayobisa";
const char* password = "ayoharusbisa";

RTC_DS3231 rtc;
int iYear, iMonth, iDay, iHour, iMinute, iSecond;
int iPage = 1, iState = 1;

unsigned long lastNtpSync = 0;
const unsigned long ntpInterval = 5UL * 60UL * 1000UL;
bool lastWiFiConnected = false;

boolean LastUp = LOW, CurrentUp = LOW;
boolean LastDown = LOW, CurrentDown = LOW;
boolean LastEdit = LOW, CurrentEdit = LOW;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 1000;

String scrollText = "";
int scrollX = 0;
unsigned long lastScrollUpdate = 0;
unsigned long scrollInterval = 50;

int previousDay = -1;

// display alternating time/date control
unsigned long lastAlternateTime = 0;
bool showClock = true;
const unsigned long alternateInterval = 5000;

// Lokasi kota Balikpapan
double latitude = -1.265386;
double longitude = 116.831200;
double timeZone = 8.0;

PrayerTimes prayer(latitude, longitude, timeZone);

// Waktu sholat hasil kalkulasi
int fajrH, fajrM, sunriseH, sunriseM, dhuhrH, dhuhrM;
int asrH, asrM, maghribH, maghribM, ishaH, ishaM;

// Offset waktu sholat dalam menit (sesuaikan dengan hasil resmi yang kamu punya)
int offsetFajr = -5;     
int offsetDhuhr = 5;     
int offsetAsr = 4;       
int offsetMaghrib = 5;   
int offsetIsha = 8;      

const int buzzerDuration = 10000;
bool buzzerActive = false;
unsigned long buzzerStart = 0;

String formatTime(int h, int m) {
  char buf[6];
  sprintf(buf, "%02d:%02d", h, m);
  return String(buf);
}

int getMaxDay(int month, int year) {
  if (month == 2) return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 29 : 28;
  if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
  return 31;
}

void enforceDateLimits() {
  if (iMonth < 1) iMonth = 12;
  else if (iMonth > 12) iMonth = 1;

  int maxDay = getMaxDay(iMonth, iYear);
  if (iDay < 1) iDay = maxDay;
  else if (iDay > maxDay) iDay = 1;
}

// Fungsi menambahkan offset menit ke jam dan menit, dengan penyesuaian
void addOffset(int &hour, int &minute, int offsetMinutes) {
  minute += offsetMinutes;
  if (minute >= 60) {
    hour += minute / 60;
    minute = minute % 60;
  } else if (minute < 0) {
    hour -= 1 + (-minute - 1) / 60;
    minute = 60 - ((-minute) % 60);
  }
  if (hour >= 24) hour -= 24;
  else if (hour < 0) hour += 24;
}

void updatePrayerTimes() {
  DateTime now = rtc.now();

  // Hitung waktu sholat dengan library
  prayer.calculate(now.day(), now.month(), now.year(),
                   fajrH, fajrM, sunriseH, sunriseM, dhuhrH, dhuhrM,
                   asrH, asrM, maghribH, maghribM, ishaH, ishaM);

  // Terapkan offset sesuai kebutuhan
  addOffset(fajrH, fajrM, offsetFajr);
  addOffset(dhuhrH, dhuhrM, offsetDhuhr);
  addOffset(asrH, asrM, offsetAsr);
  addOffset(maghribH, maghribM, offsetMaghrib);
  addOffset(ishaH, ishaM, offsetIsha);

  // Buat teks scroll menampilkan waktu sholat
  scrollText = "Subuh: " + formatTime(fajrH, fajrM) +
               " Dzuhur: " + formatTime(dhuhrH, dhuhrM) +
               " Ashar: " + formatTime(asrH, asrM) +
               " Maghrib: " + formatTime(maghribH, maghribM) +
               " Isya: " + formatTime(ishaH, ishaM);
}

void syncRTCFromNTP() {
  Disp.clear();
  Disp.drawText(0, 0, "Sync NTP...");
  configTime(28800, 0, "ntp.bmkg.go.id");

  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    delay(1000);
    retry++;
  }

  if (retry < 10) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                        timeinfo.tm_mday, timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec));
    Disp.clear();
    Disp.drawText(0, 0, "Sync Berhasil");
  } else {
    Disp.clear();
    Disp.drawText(0, 0, "Sync Gagal");
  }
  delay(2000);
}

void updateRTC() {
  rtc.adjust(DateTime(iYear, iMonth, iDay, iHour, iMinute, iSecond));
}

boolean debounce(boolean last, int pin) {
  boolean current = digitalRead(pin);
  if (last != current) {
    delay(5);
    current = digitalRead(pin);
  }
  return current;
}

void updateScrollDisplay() {
  static unsigned long lastUpdate = 0;
  static int scrollOffset = 0;
  DateTime now = rtc.now();

  if (millis() - lastUpdate > scrollInterval) {
    lastUpdate = millis();
    scrollOffset--;
    if (scrollOffset < -(int)(scrollText.length() * 6)) {
      scrollOffset = Disp.width();
    }

    char bufLine[20];
    if (millis() - lastAlternateTime > alternateInterval) {
      showClock = !showClock;
      lastAlternateTime = millis();
    }

    if (showClock) {
      sprintf(bufLine, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    } else {
      sprintf(bufLine, "%02d-%02d-%04d", now.day(), now.month(), now.year());
    }

    Disp.clear();
    Disp.setFont(FontDefault);
    Disp.drawText(scrollOffset, 0, scrollText.c_str());
    Disp.drawText(0, 9, bufLine);
  }
}

void displayEditState() {
  Disp.clear();
  char buffer[32];

  switch (iState) {
    case 1: sprintf(buffer, "%04d", iYear); break;
    case 2: sprintf(buffer, "%02d", iMonth); break;
    case 3: sprintf(buffer, "%02d", iDay); break;
    case 4: sprintf(buffer, "%02d", iHour); break;
    case 5: sprintf(buffer, "%02d", iMinute); break;
    case 6: sprintf(buffer, "%02d", iSecond); break;
  }

  Disp.setFont(FontDefault);
  Disp.drawText(0, 0, "Edit Mode");
  Disp.drawText(0, 9, buffer);
}

void setup() {
  Disp.start();
  Disp.setBrightness(10);
  Disp.setFont(FontDefault);

  pinMode(EditPin, INPUT_PULLUP);
  pinMode(UpPin, INPUT_PULLUP);
  pinMode(DownPin, INPUT_PULLUP);
  pinMode(BuzzerPin, OUTPUT);
  digitalWrite(BuzzerPin, HIGH);

  Wire.begin(D2, D1);
  rtc.begin();

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    syncRTCFromNTP();
    lastNtpSync = millis();
    lastWiFiConnected = true;
  }

  updatePrayerTimes();
  previousDay = rtc.now().day();
}

void loop() {
  Disp.loop();
  DateTime now = rtc.now();

  if (now.day() != previousDay) {
    previousDay = now.day();
    if (WiFi.status() == WL_CONNECTED) {
      syncRTCFromNTP();
      lastNtpSync = millis();
    }
    updatePrayerTimes();
  }

  bool currentWiFiConnected = WiFi.status() == WL_CONNECTED;
  if (!lastWiFiConnected && currentWiFiConnected) {
    syncRTCFromNTP();
    lastNtpSync = millis();
  }
  lastWiFiConnected = currentWiFiConnected;

  if (WiFi.status() == WL_CONNECTED && millis() - lastNtpSync >= ntpInterval) {
    syncRTCFromNTP();
    lastNtpSync = millis();
  }

  CurrentUp = debounce(LastUp, UpPin);
  CurrentDown = debounce(LastDown, DownPin);
  CurrentEdit = debounce(LastEdit, EditPin);

  if (LastEdit == HIGH && CurrentEdit == LOW) {
    if (iPage == 1) {
      iPage = 2;
      iState = 1;
      iYear = now.year();
      iMonth = now.month();
      iDay = now.day();
      iHour = now.hour();
      iMinute = now.minute();
      iSecond = now.second();
    } else {
      iState++;
      if (iState > 6) {
        updateRTC();
        iPage = 1;
        iState = 1;
      }
    }
  }

  if (LastUp == HIGH && CurrentUp == LOW && iPage == 2) {
    switch (iState) {
      case 1: iYear++; break;
      case 2: iMonth++; break;
      case 3: iDay++; break;
      case 4: iHour++; break;
      case 5: iMinute++; break;
      case 6: iSecond++; break;
    }
    enforceDateLimits();
  }

  if (LastDown == HIGH && CurrentDown == LOW && iPage == 2) {
    switch (iState) {
      case 1: iYear--; break;
      case 2: iMonth--; break;
      case 3: iDay--; break;
      case 4: iHour--; break;
      case 5: iMinute--; break;
      case 6: iSecond--; break;
    }
    enforceDateLimits();
  }

  LastUp = CurrentUp;
  LastDown = CurrentDown;
  LastEdit = CurrentEdit;

  if (iPage == 1) {
    updateScrollDisplay();

    // Cek buzzer untuk waktu sholat, jika waktu sama dengan waktu sholat
    if (!buzzerActive) {
      if ((now.hour() == fajrH && now.minute() == fajrM && now.second() == 0) ||
          (now.hour() == dhuhrH && now.minute() == dhuhrM && now.second() == 0) ||
          (now.hour() == asrH && now.minute() == asrM && now.second() == 0) ||
          (now.hour() == maghribH && now.minute() == maghribM && now.second() == 0) ||
          (now.hour() == ishaH && now.minute() == ishaM && now.second() == 0)) {
        buzzerActive = true;
        buzzerStart = millis();
        digitalWrite(BuzzerPin, LOW);
      }
    } else {
      if (millis() - buzzerStart > buzzerDuration) {
        buzzerActive = false;
        digitalWrite(BuzzerPin, HIGH);
      }
    }
  } else {
    displayEditState();
  }
}
