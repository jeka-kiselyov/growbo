#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include "Seeed_BME280.h"
#include "RTClib.h"


// 0X3C+SA0 - 0x3C or 0x3D /// OLED i2c
#define I2C_ADDRESS 0x3C

// Define proper RST_PIN if required.
#define RST_PIN -1

SSD1306AsciiAvrI2c oled;
BME280 bme280;
DS1307 rtc;

#define touchM 2
#define touchD 3
#define touchU 4

#define outL 5 /// relay light pin
#define outC 6 /// cooler relay pin
#define outH 7 /// humidifier relay pin

#define boozer 9  /// boozer pwm pin ( too loud on default )


struct touchStatus {
  char menu;
  char up;
  char down;  
};
typedef struct touchStatus TouchStatus;

struct settingItem {
  String desc;
  uint8_t min;
  uint8_t max;
  char special;
  uint8_t value;
  uint8_t defaultValue;
};

typedef struct settingItem SettingItem;
#define SETTINGS_COUNT 8
SettingItem settingItems[SETTINGS_COUNT] = {
   {"Time hours", 0, 23, 'h', 0, 12},
   {"Time minutes", 0, 59, 'm', 0, 0},
   {"Sunrise", 0, 23, 0, 0, 7}, /// switch lights on in this time HH
   {"Sunset", 0, 23, 0, 0, 21}, /// switch lights off in this HH
   {"Max temp", 0, 99, 0, 0, 26},  //// temp at which switch cooler on
   {"Temp ok", 0, 99, 0, 0, 24},   //// temp at which switch cooler off
   {"Min humidity", 0, 99, 0, 0, 50},   //// humidity at which switch humidifier on
   {"Humidity ok", 0, 99, 0, 0, 60}     //// humidity at which switch humidifier off
};

#define SETTINGS_SUNRISE 2
#define SETTINGS_SUNSET 3

#define TEMPERATURE_MAX 4
#define TEMPERATURE_OK 5

#define HUMIDITY_MIN 6
#define HUMIDITY_OK 7


void setup() {  
  // put your setup code here, to run once:
  Serial.begin(9600);


  //// pin modes
  pinMode(touchM, INPUT);
  pinMode(touchD, INPUT);
  pinMode(touchU, INPUT);

  pinMode(outL, OUTPUT);
  pinMode(outC, OUTPUT);
  pinMode(outH, OUTPUT);

  pinMode(boozer, OUTPUT);
  digitalWrite(boozer, LOW);

  //// off everything by default
  digitalWrite(outL, HIGH);
  digitalWrite(outC, HIGH);
  digitalWrite(outH, HIGH);
  
  /// make some booz on startup
  booz();
  
  //// I am not sure about this. But oled does not start instatly. Looks that
  /// it requires too much current and we need to wait for capacitor to load first
  /// Would be cool to investigate more. But it works ok with pause on startup
  delay(5000);

  /// make booz after delay
  booz();
  
  rtc.begin();
  
  delay(500);
  
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  
  oled.setFont(Arial14);
  
  oled.clear();
  oled.setContrast(200);
  
  oled.set1X();
  oled.print("Waiting temp sensor...");
  oled.set2X();
  
  if(!bme280.init()){
    Serial.println("Device error!");
  }
  
  checkForDefaultValues();
}

void booz() {
  analogWrite(boozer, 50);
  delay(30);
  analogWrite(boozer, 0);
}

bool isLightsOn = false;
bool isCoolerOn = false;
bool isHumidifierOn = false;

void processLights(DateTime currentTime) {
  uint8_t sunriseHour = settingItems[SETTINGS_SUNRISE].value;
  uint8_t sunsetHour = settingItems[SETTINGS_SUNSET].value;

  if (currentTime.hour() >= sunriseHour && currentTime.hour() < sunsetHour) {
    isLightsOn = true;
    digitalWrite(outL, LOW);
  } else {
    isLightsOn = false;
    digitalWrite(outL, HIGH);
  }
}

void processTemperature(float temp) {
  uint8_t temperatureMax = settingItems[TEMPERATURE_MAX].value;
  uint8_t temperatureOk = settingItems[TEMPERATURE_OK].value;

  if (temp >= temperatureMax) {
    isCoolerOn = true;
    digitalWrite(outC, LOW);
  } else if (isCoolerOn && temp <= temperatureOk) {
    isCoolerOn = false;
    digitalWrite(outC, HIGH);    
  }
}

void processHumidity(uint32_t humidity) {
  uint8_t humidityMin = settingItems[HUMIDITY_MIN].value;
  uint8_t humidityOk = settingItems[HUMIDITY_OK].value;
  
  if (humidity <= humidityMin) {
    isHumidifierOn = true;
    digitalWrite(outH, LOW);
  } else if (isHumidifierOn && humidity >= humidityOk) {
    isHumidifierOn = false;
    digitalWrite(outH, HIGH);    
  }
}

bool isInMenu = false;
unsigned long lastMenuAction = 0;
uint8_t currentMenuSettingItemI = 0;
uint8_t currentMenuSettingValue = 0;

TouchStatus lastTouchStatus;
TouchStatus currentTouchStatus;

void readTouchStatus() {
  lastTouchStatus.menu = currentTouchStatus.menu;
  lastTouchStatus.up = currentTouchStatus.up;
  lastTouchStatus.down = currentTouchStatus.down;
  
  currentTouchStatus.menu = digitalRead(touchM);
  currentTouchStatus.up = digitalRead(touchU);
  currentTouchStatus.down = digitalRead(touchD);
}

void checkForDefaultValues() {
  //// there's flag in rtc memory. We switch it on startup to
  //// last compile time in seconds.
  //// If there's no values in it, or value is different from the
  //// compilation time - we update rtc values with default ones
  uint8_t settingsFlagAddress = SETTINGS_COUNT; //// the byte after our settings bytes
  byte flagValue = getSettingsValue(settingsFlagAddress);
  DateTime compiledTime = DateTime(__DATE__, __TIME__);
  byte somehowUniqCompilationValue = 1 + compiledTime.second() + compiledTime.minute();

  //Serial.println("Unique value");
  //Serial.println(somehowUniqCompilationValue);
  
  if (flagValue == somehowUniqCompilationValue) {
    // we are ok
  } else {
    // set to defaults
    for (byte i = 0; i < SETTINGS_COUNT; i++) {
      saveSettingsValue(i, settingItems[i].defaultValue);
    }
    saveSettingsValue(settingsFlagAddress, somehowUniqCompilationValue);
  }

  /// read values to cache
  settingItems[SETTINGS_SUNRISE].value = getSettingsValue(SETTINGS_SUNRISE);
  settingItems[SETTINGS_SUNSET].value = getSettingsValue(SETTINGS_SUNSET);
  settingItems[TEMPERATURE_MAX].value = getSettingsValue(TEMPERATURE_MAX);
  settingItems[TEMPERATURE_OK].value = getSettingsValue(TEMPERATURE_OK);
  settingItems[HUMIDITY_MIN].value = getSettingsValue(HUMIDITY_MIN);
  settingItems[HUMIDITY_OK].value = getSettingsValue(HUMIDITY_OK);
}

byte getSettingsValue(byte settingsI) {
  byte ret = 0;
  if (settingItems[settingsI].special == 'h') {
    DateTime now = rtc.now();
    ret = now.hour();
  } else if (settingItems[settingsI].special == 'm') {
    DateTime now = rtc.now();
    ret = now.minute();
  } else {
    byte address = 8 + settingsI;  // RAM starts at byte #8
//  Serial.println(address);
//    byte byteH = rtc.read(address);
//    byte byteL = rtc.read(address + 1);
//  Serial.println(byteH);
//  Serial.println(byteL);
//    ret = (byteH << 8) | (byteL);
    
    ret = rtc.read(address);
  }

  //Serial.println("Read value");
  //Serial.println(ret);
    
  return ret;
}

void saveSettingsValue(byte i, byte value) {
    Serial.println(value);
    if (settingItems[i].special == 'h') {
      DateTime adjusted = rtc.now();
      adjusted.sethour(value);
      rtc.adjust(adjusted);
    } else if (settingItems[i].special == 'm') {
      DateTime adjusted = rtc.now();
      adjusted.setminute(value);
      rtc.adjust(adjusted);
    } else {
      /// two bytes for each setting
      settingItems[i].value = value;
      byte address = 8 + i; // RAM starts at byte #8
      rtc.write(address, value);
//      Serial.println(address);
//      rtc.write(address, highByte(currentMenuSettingValue));
//      rtc.write(address+1, lowByte(currentMenuSettingValue));
    }
    
    //Serial.println("Save value");
    //Serial.println(value);
}


bool processTouch() {
  readTouchStatus();
  unsigned long currentTime = millis();
  
  if (currentTouchStatus.menu == HIGH && lastTouchStatus.menu == LOW) {
    booz();
    if (!isInMenu) {
      oled.clear();
      isInMenu = true;
      currentMenuSettingItemI = 0;
      //Serial.println("Menu");
    } else {
      saveSettingsValue(currentMenuSettingItemI, currentMenuSettingValue);
      currentMenuSettingItemI++;
      if (currentMenuSettingItemI >= SETTINGS_COUNT) {
         currentMenuSettingItemI = 0;
      }
    }

    currentMenuSettingValue = getSettingsValue(currentMenuSettingItemI);  
    lastMenuAction = millis();
  }

  unsigned long actionDiff = 0;
  if (currentTime > lastMenuAction) {
    actionDiff = currentTime - lastMenuAction;
  }  

  if (isInMenu) {
    if (actionDiff > 5000L) {
      booz();
      saveSettingsValue(currentMenuSettingItemI, currentMenuSettingValue);
      //Serial.println("Out of menu");
      isInMenu = false;
    } else {
      if (currentTouchStatus.up == HIGH && lastTouchStatus.up == LOW) {
        booz();
        if (currentMenuSettingValue == settingItems[currentMenuSettingItemI].max) {
          currentMenuSettingValue = settingItems[currentMenuSettingItemI].min;
        } else {
          currentMenuSettingValue++;
        }
        
        lastMenuAction = millis();
      }
      if (currentTouchStatus.down == HIGH && lastTouchStatus.down == LOW) {
        booz();
        if (currentMenuSettingValue == settingItems[currentMenuSettingItemI].min) {
          currentMenuSettingValue = settingItems[currentMenuSettingItemI].max;
        } else {
          currentMenuSettingValue--;
        }
        
        lastMenuAction = millis();
      }
    }
  }

  if (isInMenu) {
    //// display menu information
    char valueString[6];
    oled.set1X();
    oled.setCursor(0, 0);
    oled.print(settingItems[currentMenuSettingItemI].desc);
    oled.print(": ");
    
    sprintf(valueString,"%02d      ", currentMenuSettingValue);
    oled.print(valueString);
    oled.println("           "); // clear display
  }

  return isInMenu;
}

void centeredString(const char* str) {
  int strWidth = oled.strWidth(str);
  
  oled.setCursor(64 - strWidth / 2, 0);
  oled.println(str);
}

void printBME280Data() {
  char stringBuffer80[80];
  char str_temp[6];

  float temp = bme280.getTemperature();
  uint32_t humidity = bme280.getHumidity();
  
  dtostrf(temp, 4, 1, str_temp);
  sprintf(stringBuffer80,"%s C %d%% ", str_temp, humidity);
  centeredString(stringBuffer80);

  processTemperature(temp);
  processHumidity(humidity);
}

void printTime(DateTime time) {
  char stringBuffer80[80];
  sprintf(stringBuffer80,"       %02d:%02d       ", time.hour(), time.minute());
  centeredString(stringBuffer80);
  Serial.println(stringBuffer80);
}

unsigned long lastInformationRedrawTime = 0;
bool lastInformationDrawWasTime = false;

void displayInformation() {
  unsigned long currentTime = millis();
  if (currentTime - lastInformationRedrawTime > 3000L) {
    oled.set2X();
    if (lastInformationDrawWasTime) {
      printBME280Data();
      lastInformationDrawWasTime = false;
    } else {
      DateTime now = rtc.now();
      printTime(now);
      processLights(now);
      lastInformationDrawWasTime = true;
    }
    lastInformationRedrawTime = millis();
  }
}

void loop() {
  bool isInMenu = processTouch();
  if (!isInMenu) {
    displayInformation();
  }
  delay(33);
}
