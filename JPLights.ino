#include "declarations.h"
#include "functions.h"

void setup(){
  #ifdef DEBUG
    Serial.begin(115200); 
  #endif
  pinMode(PIR_PIN, INPUT);
  pinMode(13, INPUT); //disable ON BOARD LED (Red) by declaring pin 13 as INPUT
  digitalWrite(13, LOW); //making sure it's off. Probably overkill since it's INPUT

  ++bootCount;
  debug_println("Boot #: " + String(bootCount));
  wakeup_reason = esp_sleep_get_wakeup_cause();
  debug_printf("Wake reason:%d\n", wakeup_reason);

  turnLED(LED_ON);
  connectToWiFi();

  switch(wakeup_reason)   {
    case ESP_SLEEP_WAKEUP_EXT1 :  //case=3
    {
      debug_println("----MOTION"); 
      lightsON = true;

      //quick DEFAULT TURN ON, and then check for scene
      StaticJsonDocument<256> docOUT;
      docOUT["method"] = "setPilot";
      JsonObject params = docOUT.createNestedObject("params");
      params["state"] = 1;
      params["sceneId"] = WarmWhite;
      params["dimming"] = b60;
      String PUT_pilot = ""; 
      serializeJson(docOUT, PUT_pilot);
      sendUDP(PUT_pilot);

      setScene();
      break;
    }

    case ESP_SLEEP_WAKEUP_TIMER : //case=4
      debug_println("----TIMER"); 
      lightsON = true;
      setScene();
      break;

    case ESP_SLEEP_WAKEUP_TOUCHPAD : //case=5
    { 
      debug_println("----TOUCH");
      touchedPad = esp_sleep_get_touchpad_wakeup_status(); 
      debug_printf("touchedPad:%d\n", touchedPad);

      //build JSON PILOT
      StaticJsonDocument<256> docOUT;
      docOUT["method"] = "setPilot";
      JsonObject params = docOUT.createNestedObject("params");

      //these 2 are values for most scenes. Will change where applicable
      params["state"] = 1;
      params["dimming"] = b100; 

      switch (touchedPad)  
      {
        case OFF :          
          debug_println("Set lights: OFF"); 
          params["state"] = 0;
          break;
        case NIGHT_LIGHT :  
          debug_println("Set lights: NIGHT LIGHT"); 
          params["sceneId"] = Nightlight;
          break;
        case COZY :         
          debug_println("Set lights: COZY"); 
          params["sceneId"] = Cozy;
          break;
        case WARM_WHITE :   
          debug_println("Set lights: WHITE WARM"); 
          params["sceneId"] = WarmWhite;
          break;
        case DAYLIGHT :     
          debug_println("Set lights: DAYLIGHT"); 
          params["sceneId"] = Daylight;
          break;
        case RED :          
          debug_println("Set lights: RED, Sunset"); 
          params["sceneId"] = Sunset;
          break;
        case GREEN :        
          debug_println("Set lights: GREEN, Jungle"); 
          params["sceneId"] = Jungle;
          break;
        case BLUE :         
          debug_println("Set lights: BLUE, Ocean"); 
          params["sceneId"] = Ocean;
          break;
        default :
          debug_printf("Wakeup w/o typified touchpad: %d\n", touchedPad); 
          break;
      }

      //send UDP only if scene changed or it's OFF. NOTE: Some lightSettings[] have noScene a.k.a. ENUM=0
      if (prevScene != static_cast<int>(params["sceneId"]) || touchedPad == static_cast<touch_pad_t>(OFF)) {
        String PUT_pilot = ""; 
        serializeJson(docOUT, PUT_pilot);
        if (sendUDP(PUT_pilot)) {
          lightsON = true;
          prevScene = static_cast<int>(params["sceneId"]);
          if (touchedPad == static_cast<touch_pad_t>(OFF)) { //my ENUM is cast to touch_pad_t ENUM to avoid compiler warning
            lightsON = false;
            debug_println("--start 5 sec delay");
            delay(5000); //if OFF, delay enough to leave the room before motion detected again
          }
        }else {
          //-----------------------------------------------------------------blink RED
          debug_println("TOUCH: UDP PILOT not successful");
        }
      }else {
        debug_println("NULL ACTION: no change in scene");
      }
      break;
    }

    default : 
      debug_printf("Wakeup w/o typified reason: %d\n",wakeup_reason); 
      break;
  }

  turnLED(LED_OFF);

  for (int i=0; i<10; i++) { //set wake up by touch
    if (i != 1 && i != 2) { //skip T1 & T2 (GPIO 0 & 2): assigned to NeoPixel in Feather V2
      touchSleepWakeUpEnable(touchPad[i].gpio, touchPad[i].threshold);
      //debug_printf("SET: Touch pin:%02d, thld:%d\n",touchPad[i].gpio, touchPad[i].threshold);
    }
  }

  if (!lightsON) { 
    int nowHour = getHour();
    if (nowHour >= onByMotionMin && nowHour <= onByMotionMax) {
      esp_sleep_enable_ext1_wakeup(PIR_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH); //set wake up by motion
      debug_println("SET: wake up by motion");
    }
  }
  else 
    //set circadian wake up timer only if not woken up by touch so selected (touched) color takes precedence over circadian
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 || wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) { 
      TIME_TO_SLEEP = (((nextWakeupHR - getHour()) * 60) - getMinutes()) * 60;
      debug_println("SET: wake up by timer in " + String(TIME_TO_SLEEP/60/60) + " Hrs " + String(TIME_TO_SLEEP/60 - (TIME_TO_SLEEP/60/60 * 60)) + " Mins");
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR); //set wake up by timer
    }
    
  debug_println("Going to sleep now\n");
  #ifdef DEBUG
    Serial.flush(); 
  #endif
  esp_deep_sleep_start();
}


void loop(){
  
}
