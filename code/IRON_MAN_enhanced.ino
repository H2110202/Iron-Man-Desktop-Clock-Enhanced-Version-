#include "TM1637Display.h"
#include "Adafruit_NeoPixel.h"
#include "NTPClient.h"
#include "WiFiManager.h"
#include "Preferences.h"
#include <arduinoFFT.h>

#define PIN 17
#define display_CLK 18
#define display_DIO 19
#define NUMPIXELS 35
#define AUDIO_PIN 34
#define BUTTON_PIN 0

#define SAMPLES 256
#define SAMPLING_FREQUENCY 40000

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
TM1637Display display(display_CLK, display_DIO);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 8 * 3600);
Preferences preferences;
arduinoFFT FFT = arduinoFFT();

double vReal[SAMPLES];
double vImag[SAMPLES];

int UTC = 8;
int Display_backlight = 2;
int led_ring_brightness = 20;
int led_ring_brightness_flash = 250;

int red = 0;
int green = 20;
int blue = 255;

bool wifiConnected = false;
int lastHour = -1;

enum DisplayMode {
  CLOCK_MODE,
  STOPWATCH_MODE,
  COUNTDOWN_MODE,
  DATE_MODE,
  TEMPERATURE_MODE,
  MUSIC_MODE
};

DisplayMode currentMode = CLOCK_MODE;
unsigned long stopwatchStartTime = 0;
unsigned long stopwatchElapsed = 0;
bool stopwatchRunning = false;
unsigned long countdownTime = 300000;
unsigned long countdownRemaining = 0;
bool countdownRunning = false;
bool countdownFinished = false;

struct Alarm {
  int hour;
  int minute;
  bool enabled;
  bool weekdays[7];
};

Alarm alarms[5];
int alarmCount = 0;
bool alarmTriggered = false;
unsigned long alarmTriggerTime = 0;

void setup() {
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.println("\nStarting");

  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP_AP");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to Wi-Fi");
    wifiConnected = true;
  }

  timeClient.begin();
  display.setBrightness(Display_backlight);
  pixels.begin();
  pixels.setBrightness(led_ring_brightness);
  blue_light();

  preferences.begin("alarms", false);
  loadAlarms();
  preferences.end();

  Serial.println("System initialized");
}

void loop() {
  checkButton();

  switch (currentMode) {
    case CLOCK_MODE:
      clockMode();
      break;
    case STOPWATCH_MODE:
      stopwatchMode();
      break;
    case COUNTDOWN_MODE:
      countdownMode();
      break;
    case DATE_MODE:
      dateMode();
      break;
    case TEMPERATURE_MODE:
      temperatureMode();
      break;
    case MUSIC_MODE:
      musicMode();
      break;
  }

  checkAlarms();
  delay(10);
}

void checkButton() {
  static unsigned long lastButtonPress = 0;
  static int pressCount = 0;

  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > 50) {
      lastButtonPress = millis();
      pressCount++;
      
      if (pressCount == 1) {
        delay(300);
        if (digitalRead(BUTTON_PIN) == LOW) {
          pressCount = 0;
          longPress();
        }
      } else if (pressCount == 2) {
        delay(300);
        if (digitalRead(BUTTON_PIN) == LOW) {
          pressCount = 0;
          triplePress();
        }
      }
    }
  } else {
    if (pressCount == 1 && millis() - lastButtonPress > 500) {
      pressCount = 0;
      shortPress();
    }
  }
}

void shortPress() {
  Serial.println("Short press - Switch mode");
  currentMode = (DisplayMode)((currentMode + 1) % 6);
  
  switch (currentMode) {
    case CLOCK_MODE:
      Serial.println("Mode: Clock");
      break;
    case STOPWATCH_MODE:
      Serial.println("Mode: Stopwatch");
      break;
    case COUNTDOWN_MODE:
      Serial.println("Mode: Countdown");
      break;
    case DATE_MODE:
      Serial.println("Mode: Date");
      break;
    case TEMPERATURE_MODE:
      Serial.println("Mode: Temperature");
      break;
    case MUSIC_MODE:
      Serial.println("Mode: Music");
      break;
  }
  
  flash_cuckoo();
}

void longPress() {
  Serial.println("Long press - Mode action");
  
  switch (currentMode) {
    case STOPWATCH_MODE:
      if (stopwatchRunning) {
        stopwatchRunning = false;
        Serial.println("Stopwatch stopped");
      } else {
        stopwatchRunning = true;
        stopwatchStartTime = millis() - stopwatchElapsed;
        Serial.println("Stopwatch started");
      }
      break;
    case COUNTDOWN_MODE:
      if (countdownRunning) {
        countdownRunning = false;
        Serial.println("Countdown paused");
      } else if (countdownFinished) {
        countdownFinished = false;
        countdownRemaining = countdownTime;
        Serial.println("Countdown reset");
      } else {
        countdownRunning = true;
        Serial.println("Countdown started");
      }
      break;
  }
}

void triplePress() {
  Serial.println("Triple press - Reset");
  
  switch (currentMode) {
    case STOPWATCH_MODE:
      stopwatchRunning = false;
      stopwatchElapsed = 0;
      Serial.println("Stopwatch reset");
      break;
    case COUNTDOWN_MODE:
      countdownRunning = false;
      countdownFinished = false;
      countdownRemaining = countdownTime;
      Serial.println("Countdown reset");
      break;
  }
}

void clockMode() {
  timeClient.update();
  Serial.println(timeClient.getFormattedTime());

  display.showNumberDecEx(timeClient.getHours(), 0b01000000, true, 2, 0);
  display.showNumberDecEx(timeClient.getMinutes(), 0b01000000, true, 2, 2);

  if (WiFi.status() != WL_CONNECTED) {
    breathing_light();
    wifiConnected = false;
  } else if (!wifiConnected) {
    flash_cuckoo();
    blue_light();
    wifiConnected = true;
  } else {
    blue_light();
  }

  if (timeClient.getMinutes() == 0 && timeClient.getHours() != lastHour) {
    smooth_hour_effect();
    lastHour = timeClient.getHours();
  }
}

void stopwatchMode() {
  if (stopwatchRunning) {
    stopwatchElapsed = millis() - stopwatchStartTime;
  }

  unsigned long totalSeconds = stopwatchElapsed / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  int centiseconds = (stopwatchElapsed % 1000) / 10;

  display.showNumberDecEx(minutes, 0b01000000, true, 2, 0);
  display.showNumberDecEx(seconds, 0b01000000, true, 2, 2);

  rainbow_light(5);
}

void countdownMode() {
  if (countdownRunning && !countdownFinished) {
    unsigned long elapsed = millis() - countdownTime + countdownRemaining;
    countdownRemaining = countdownTime - elapsed;
    
    if (countdownRemaining <= 0) {
      countdownRemaining = 0;
      countdownFinished = true;
      countdownRunning = false;
      alarmAnimation();
    }
  }

  unsigned long totalSeconds = countdownRemaining / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;

  display.showNumberDecEx(minutes, 0b01000000, true, 2, 0);
  display.showNumberDecEx(seconds, 0b01000000, true, 2, 2);

  if (countdownFinished) {
    alarm_light();
  } else {
    green_light();
  }
}

void dateMode() {
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  
  int month = ptm->tm_mon + 1;
  int day = ptm->tm_mday;

  display.showNumberDecEx(month, 0b01000000, true, 2, 0);
  display.showNumberDecEx(day, 0b01000000, true, 2, 2);

  purple_light();
}

void temperatureMode() {
  float temperature = readTemperature();
  int tempInt = (int)temperature;
  
  display.showNumberDecEx(tempInt, 0b00000000, true, 2, 0);
  display.showNumberDecEx(0, 0b00000000, true, 2, 2);

  orange_light();
}

float readTemperature() {
  return 25.0 + (sin(millis() / 10000.0) * 5);
}

void musicMode() {
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = analogRead(AUDIO_PIN);
    vImag[i] = 0;
    delayMicroseconds(25);
  }

  FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);

  int peak = 0;
  for (int i = 2; i < (SAMPLES / 2); i++) {
    if (vReal[i] > peak) {
      peak = vReal[i];
    }
  }

  int brightness = map(peak, 0, 2000, 10, 255);
  brightness = constrain(brightness, 10, 255);

  pixels.setBrightness(brightness);
  
  for (int i = 0; i < NUMPIXELS; i++) {
    int freqIndex = map(i, 0, NUMPIXELS, 2, SAMPLES / 4);
    int intensity = map(vReal[freqIndex], 0, 2000, 0, 255);
    intensity = constrain(intensity, 0, 255);
    
    pixels.setPixelColor(i, pixels.Color(intensity, intensity / 2, 255 - intensity));
  }
  pixels.show();

  display.showNumberDecEx(brightness / 10, 0b00000000, true, 2, 0);
  display.showNumberDecEx(0, 0b00000000, true, 2, 2);
}

void checkAlarms() {
  if (alarmTriggered) {
    if (millis() - alarmTriggerTime > 60000) {
      alarmTriggered = false;
      blue_light();
    }
    return;
  }

  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentDayOfWeek = timeClient.getDay();

  for (int i = 0; i < alarmCount; i++) {
    if (alarms[i].enabled && 
        alarms[i].hour == currentHour && 
        alarms[i].minute == currentMinute &&
        alarms[i].weekdays[currentDayOfWeek]) {
      
      alarmTriggered = true;
      alarmTriggerTime = millis();
      alarmAnimation();
      Serial.println("Alarm triggered!");
      break;
    }
  }
}

void loadAlarms() {
  alarmCount = preferences.getInt("alarmCount", 0);
  
  for (int i = 0; i < alarmCount; i++) {
    String key = "alarm" + String(i);
    String data = preferences.getString(key.c_str(), "");
    
    if (data.length() > 0) {
      int firstComma = data.indexOf(',');
      int secondComma = data.indexOf(',', firstComma + 1);
      
      alarms[i].hour = data.substring(0, firstComma).toInt();
      alarms[i].minute = data.substring(firstComma + 1, secondComma).toInt();
      alarms[i].enabled = data.substring(secondComma + 1, secondComma + 2).toInt() == 1;
      
      for (int j = 0; j < 7; j++) {
        alarms[i].weekdays[j] = data.substring(secondComma + 3 + j, secondComma + 4 + j).toInt() == 1;
      }
    }
  }
}

void saveAlarms() {
  preferences.putInt("alarmCount", alarmCount);
  
  for (int i = 0; i < alarmCount; i++) {
    String key = "alarm" + String(i);
    String data = String(alarms[i].hour) + "," + 
                  String(alarms[i].minute) + "," + 
                  String(alarms[i].enabled ? 1 : 0) + ",";
    
    for (int j = 0; j < 7; j++) {
      data += String(alarms[i].weekdays[j] ? 1 : 0);
    }
    
    preferences.putString(key.c_str(), data);
  }
}

void addAlarm(int hour, int minute, bool weekdays[7]) {
  if (alarmCount < 5) {
    alarms[alarmCount].hour = hour;
    alarms[alarmCount].minute = minute;
    alarms[alarmCount].enabled = true;
    
    for (int i = 0; i < 7; i++) {
      alarms[alarmCount].weekdays[i] = weekdays[i];
    }
    
    alarmCount++;
    
    preferences.begin("alarms", false);
    saveAlarms();
    preferences.end();
    
    Serial.println("Alarm added: " + String(hour) + ":" + String(minute));
  }
}

void blue_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
  }
  pixels.show();
}

void green_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 255, 0));
  }
  pixels.show();
}

void purple_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(128, 0, 128));
  }
  pixels.show();
}

void orange_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 165, 0));
  }
  pixels.show();
}

void rainbow_light(int delayVal) {
  static uint16_t j = 0;
  
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, Wheel((i + j) & 255));
  }
  pixels.show();
  
  j++;
  if (j >= 256) j = 0;
  
  delay(delayVal);
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void breathing_light() {
  for (int i = 10; i <= 250; i += 5) {
    pixels.setBrightness(i);
    for (int j = 0; j < NUMPIXELS; j++) {
      pixels.setPixelColor(j, pixels.Color(0, 0, 255));
    }
    pixels.show();
    delay(20);
  }
  for (int i = 250; i >= 10; i -= 5) {
    pixels.setBrightness(i);
    for (int j = 0; j < NUMPIXELS; j++) {
      pixels.setPixelColor(j, pixels.Color(0, 0, 255));
    }
    pixels.show();
    delay(20);
  }
}

void flash_cuckoo() {
  pixels.setBrightness(led_ring_brightness_flash);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
  }
  pixels.show();
  delay(500);
}

void smooth_hour_effect() {
  for (int i = led_ring_brightness; i <= 150; i += 5) {
    pixels.setBrightness(i);
    pixels.show();
    delay(20);
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 255));
    }
    pixels.show();
    delay(200);
    blue_light();
    delay(200);
  }

  for (int i = 150; i >= led_ring_brightness; i -= 5) {
    pixels.setBrightness(i);
    pixels.show();
    delay(20);
  }

  blue_light();
}

void alarmAnimation() {
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < NUMPIXELS; j++) {
      pixels.setPixelColor(j, pixels.Color(255, 0, 0));
    }
    pixels.show();
    delay(200);
    
    for (int j = 0; j < NUMPIXELS; j++) {
      pixels.setPixelColor(j, pixels.Color(0, 0, 0));
    }
    pixels.show();
    delay(200);
  }
}

void alarm_light() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 0, 0));
  }
  pixels.show();
}
