//#define DEBUG //if not defined, all serial output statments become empty in compiled output
#ifdef DEBUG
  #define debug_print(x)      Serial.print(x)
  #define debug_println(...)  Serial.println(__VA_ARGS__)
  #define debug_printf(...)   Serial.printf(__VA_ARGS__)
  #define debug_write(...)   {Serial.print("[");Serial.print(__FUNCTION__);Serial.print("():");Serial.print(__LINE__);Serial.print("] ");Serial.println(__VA_ARGS__);}
#else
  #define debug_print(x)
  #define debug_println(...)
  #define debug_printf(...)
  #define debug_write(...)
#endif

#define STATIC_IP //if not defined, will get IP & DNS from router


//---Deep Sleep
//https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html#
//https://randomnerdtutorials.com/esp32-touch-wake-up-deep-sleep/
#define uS_TO_S_FACTOR 1000000ULL    //micro seconds to seconds
ulong TIME_TO_SLEEP = 60*3;          //in seconds, init value 60*3
RTC_DATA_ATTR byte bootCount = 0;
RTC_DATA_ATTR bool lightsON = false;
RTC_DATA_ATTR int prevScene = 0;
//Only RTC IO's pins as source for external wake: 0,2,4,12-15,25-27,32-39.
#define PIR_PIN         34  //RTC4  GPIO 34 Motion Sensor
#define PIR_PIN_BITMASK 0x400000000 //2^34 in hex
//https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/touch.html#
touch_pad_t touchedPad;
esp_sleep_wakeup_cause_t wakeup_reason;
enum TouchPadNames {NIGHT_LIGHT, NA1, NA2, DAYLIGHT, BLUE, COZY, WARM_WHITE, OFF, GREEN, RED};
struct TouchPads {
  int gpio;
  int threshold;
  TouchPadNames name;
} touchPad[10] = {        //https://learn.adafruit.com/adafruit-esp32-feather-v2/pinouts
  { 4, 50, NIGHT_LIGHT},
  { 0,  0, NA1},          //Feather V2 = neoPixel
  { 2,  0, NA2},          //Feather V2 = neoPixel_I2C_Power
  {15, 40, DAYLIGHT},
  {13, 45, BLUE},         //Feather V2 = onBoard LED equivalent scene: OCEAN
  {12, 43, COZY},
  {14, 46, WARM_WHITE},
  {27, 45, OFF},
  {33, 45, GREEN},        //equivalent scene: JUNGLE
  {32, 43, RED}           //equivalent scene: SUNSET
}; 

//---WiFi, UDP & Time
#include <WiFi.h>
#include "credentials.h"
#include "AsyncUDP.h"
const int WiZ_PORT = 38899; //https://github.com/sbidy/pywizlight
#include <ArduinoJson.h>
#include "IPAddress.h"
unsigned long referenceUTC = 0;
unsigned long referenceMillis = 0; 

//---Lamps, Circadian, ON Hours
enum Brightness {
  NA_bright = 0,
  b10 = 10,
  b30 = 30,
  b60 = 60,
  b80 = 80,
  b100 = 100
};
enum Temperatures {
  NA_temp = 0,
  veryWarm = 2700,
  warm = 3400,
  white = 4000,
  veryWhite = 5200,
  cold = 6500,
};
enum Scenes { //https://docs.pro.wizconnected.com/#light-modes
  noScene, Ocean, Romance, Sunset, Party, Fireplace, Cozy, Forest, PastelColors, WakeUp, Bedtime, WarmWhite, 
  Daylight, CoolWhite, Nightlight, Focus, Relax, TrueColors, TV_Time, PlantGrow, Spring, Summer, Fall, 
  DeepDive, Jungle, Mojito, Club, Christmas, Halloween, Candlelight, GoldenWhite, Pulse, Steampunk, Diwali
};

int onByMotionMin = 6;  //start time of day to consider ON by motion detection
int onByMotionMax = 21; //end time of day for onByMotion (9:59PM since its checking for hour only)
int nextWakeupHR = 0;   //for sleep timer
struct LightSettings {
  int TOD;
  Scenes scene;
  Brightness brightness;
  Temperatures temperature;
};
const int maxLightSettings = 9;
//array must start with an element for time defined by onByMotionMin and end with element for onByMotionMax
LightSettings lightSettings[maxLightSettings] = {  //NOTE: if setting has a scene, temperature is DONT CARE
  {6,  noScene,   b10,  veryWarm},//dimmed veryWarm for just awoken, near night light
  {8,  Cozy,      b80,  NA_temp}, //classic cozy for might get back to sleep
  {10, WarmWhite, b100, NA_temp}, //classic warm 
  {12, Daylight,  b100, NA_temp}, //classic daylight
  {13, noScene,   b100, cold},    //full awake
  {17, noScene,   b80,  white},   //less white
  {19, WarmWhite, b100, NA_temp},           
  {20, Cozy,      b80,  NA_temp},           
  {21, noScene,   b10,  veryWarm}
};

//---RGB LED
const bool LED_ON = true;
const bool LED_OFF = false;
#include <LiteLED.h> // https://github.com/Xylopyrographer/LiteLED
LiteLED onBoardLED(LED_STRIP_WS2812, 0, RMT_CHANNEL_0);  // type, isRGBW?
const uint8_t RGBLED_GPIO = 0;  // 8=on the ESP32-C3-DevKitC-02, 0=on Huzzah V2
uint8_t LED_bright = 5; // 0..255
const crgb_t L_RED = 0xff0000; // format 0xRRGGBB
const crgb_t L_ORANGE = 0xffa500;
const crgb_t L_GREEN = 0x00ff00;
const crgb_t L_BLUE = 0x0000ff;
const crgb_t L_YELLOW = 0xffc800;
const crgb_t L_WHITE = 0xe0e0e0;

//---Timer
hw_timer_t *bootLED_timer = NULL;
volatile bool bootLedState = true;
void IRAM_ATTR onTimer(){
  bootLedState = !bootLedState;
  if (bootLedState) onBoardLED.brightness(LED_bright, true);
  else onBoardLED.brightness(0, true);
}
//>>>>https://techtutorialsx.com/2017/10/07/esp32-arduino-timer-interrupts/
//>>>>https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
//>>>>https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/timer.html#


