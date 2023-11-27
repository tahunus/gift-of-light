void printEpochDetails(time_t epochTime) {
    struct tm timeinfo;
    gmtime_r(&epochTime, &timeinfo);  // Convert epoch time to broken-down time
    debug_println("----UTC Time details:");
    debug_print("Seconds: "); debug_println(timeinfo.tm_sec);
    debug_print("Minutes: "); debug_println(timeinfo.tm_min);
    debug_print("Hours (24-hour format): "); debug_println(timeinfo.tm_hour);
    debug_print("Day of the month: "); debug_println(timeinfo.tm_mday);
    debug_print("Month (0=January, 11=December): "); debug_println(timeinfo.tm_mon);
    debug_print("Year (number of years since 1900): "); debug_println(timeinfo.tm_year);
    debug_print("Day of the week (0=Sunday, 6=Saturday): "); debug_println(timeinfo.tm_wday);
    debug_print("Day of the year (0-365): "); debug_println(timeinfo.tm_yday);
    debug_print("Daylight Saving Time flag (non-zero if DST is in effect): "); debug_println(timeinfo.tm_isdst);
}

int getHour() {
  const unsigned long MILLISECONDS_PER_SECOND = 1000;
  const int TIMEZONE_OFFSET_MEXICO_CITY = -6;
  unsigned long currentUTC = referenceUTC + ((millis() - referenceMillis) / MILLISECONDS_PER_SECOND);
  time_t adjustedTime = static_cast<time_t>(currentUTC);  //convert to EPOCH in C Time Library <ctime> format 
  //printEpochDetails(adjustedTime);
  struct tm timeinfo;
  gmtime_r(&adjustedTime, &timeinfo);
  int localHour = (timeinfo.tm_hour + TIMEZONE_OFFSET_MEXICO_CITY) % 24; 
  if (localHour < 0) localHour += 24; //in C++ the sign of dividend is the sign of the result
  return localHour;
}

int getMinutes() {
  const unsigned long MILLISECONDS_PER_SECOND = 1000;
  unsigned long currentUTC = referenceUTC + ((millis() - referenceMillis) / MILLISECONDS_PER_SECOND);
  time_t adjustedTime = static_cast<time_t>(currentUTC);  //convert to EPOCH in C Time Library <ctime> format 
  struct tm timeinfo;
  gmtime_r(&adjustedTime, &timeinfo);
  return timeinfo.tm_min;
}

void getReferenceHour() {
  unsigned long UDP_referenceUTC = 0;
  unsigned long UDP_referenceMillis = 0;
  AsyncUDP udp;
  bool packetReceived = false;

  StaticJsonDocument<128> TIME_responseJSON;
  //response UDP callback no need to capture referenceUTC & referenceMillis since are globals
  udp.onPacket(  
    [&TIME_responseJSON, &packetReceived, &UDP_referenceUTC, &UDP_referenceMillis] 
    (AsyncUDPPacket packet) { 
      TIME_responseJSON.clear();
      DeserializationError error = deserializeJson(TIME_responseJSON, packet.data());
      if (error) {
        debug_print("ERROR: TIME Parsing failed: ");
        debug_println(error.c_str());
        return;
      }
      UDP_referenceUTC = TIME_responseJSON["result"]["utc"];
      UDP_referenceMillis = millis();
      packetReceived = true;
    }
  ); 
    
  StaticJsonDocument<96> jsonAskForTime;
  jsonAskForTime.clear();
  jsonAskForTime["method"] = "getTime";
  String stringAskForTime = "";
  serializeJson(jsonAskForTime, stringAskForTime);
  if(udp.connect(lampIP[0], WiZ_PORT)) {
    debug_println("\nUDP connected for getTIME");
    udp.print(stringAskForTime);
  }

  const char* ntpServer1 = "pool.ntp.org";
  const char* ntpServer2 = "time.nist.gov";
  const long  gmtOffset_sec = 0;
  const int   daylightOffset_sec = 0;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  time_t now;
  bool NTPtimeReceived = false;

  unsigned long startTime = millis();
  while (!packetReceived && !NTPtimeReceived && (millis() - startTime < 5000)) { //Wait up to 5 seconds for any response
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) { //request time continuosly for x ms.If x>0 it's blocking. internally it has delay(10) 
      //see https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-time.c
      time(&now);
      NTPtimeReceived = true;
      referenceMillis = millis();
      referenceUTC = now;
      //int tempHr = getHour();
      debug_printf("NTP-Time Epoch UTC=%lu, local hour=%d\n",referenceUTC, getHour());
    }
  }

  if (!NTPtimeReceived) {
    if (packetReceived) {
      referenceUTC = UDP_referenceUTC;
      referenceMillis = UDP_referenceMillis;
      //int tempHr = getHour();
      debug_printf("UDP-Time Epoch UTC=%lu, local hour=%d\n",referenceUTC, getHour());
    }else {
      debug_println("TIME UDP & NTP Timeout");
    } 
  }
  udp.close();
}

bool sendUDP(String &localPilot) {
  bool success = false;
  AsyncUDP udpSetLamp; 
  
  //callback for UDP SetLamp
  udpSetLamp.onPacket(
    [&success](AsyncUDPPacket packet) {
      StaticJsonDocument<256> docIN;
      DeserializationError error = deserializeJson(docIN, packet.data());
      if (error) {
        debug_print("ERROR: Parsing failed: ");
        debug_println(error.c_str());
        return; //the lambda returns without a value (as it should be for a void lambda)
      }
      if (docIN["result"]["success"]) success = true;
    }
  );
  //iterate all lamps
  for (int i = 0; i < maxLamps; i++) { 
    if(!udpSetLamp.connect(lampIP[i], WiZ_PORT)) {
      debug_print("ERROR: Failed to connect UDP to IP:");
      debug_println(lampIP[i]);
      return false;
    }
    success = false;
    unsigned long startTime; 
    for (int j = 0; j < 7; j++) { //up to 6 attempts to get "success" from lamp
      udpSetLamp.print(localPilot);
      String ipStr = lampIP[i].toString();
      debug_printf("Waiting on response from IP:%s i:%d\n", ipStr.c_str(), i);
      debug_println(localPilot);
      startTime = millis();
      while (!success && millis() - startTime < 3500) 
        delay(50);  //wait up to 3.5 seconds for UDP response
      if (success) {
        debug_println("UDP Success");
        break;
      }else {
        //----------------------------------------------------------------fastBlink(L_YELLOW);
        debug_print("Cycle #");
        debug_println(j);
      }
    }           
  }
  udpSetLamp.close(); //close the udp.onPacket callback after all lamps are actioned
  return success;
}

void setScene() {
  getReferenceHour();
  int nowHour = getHour();
  nextWakeupHR = 6; //default value in case nowHour outside onByMotion range
  if (nowHour >= onByMotionMin && nowHour <= onByMotionMax) {
    StaticJsonDocument<256> docOUT; //build JSON string for SET PILOT
    docOUT["method"] = "setPilot";
    JsonObject params = docOUT.createNestedObject("params");
    params["state"] = 1;
    for (int i=0; i<maxLightSettings; i++) {
      if (nowHour < lightSettings[i].TOD) {
        params["dimming"] = lightSettings[i-1].brightness;
        if (lightSettings[i-1].scene != noScene) {
          params["sceneId"] = lightSettings[i-1].scene;
        }else {
          params["temp"] = lightSettings[i-1].temperature;
        }
        nextWakeupHR = lightSettings[i].TOD;
        i = maxLightSettings;
      }
    }
    String PUT_pilot = ""; 
    serializeJson(docOUT, PUT_pilot);
    sendUDP(PUT_pilot);
    prevScene = static_cast<int>(params["sceneId"]);
  } 
}

void connectToWiFi() {
  int connectionAttempts = 0;  
  WiFi.disconnect(true);  //clear all credentials. Pass 'true' to also erase stored credentials
  while (WiFi.status() != WL_CONNECTED) {
    connectionAttempts++;
    if (connectionAttempts > 5) {
      debug_println("\nFailed to connect to WiFi after 3 attempts.");
      ESP.restart(); 
    }
    WiFi.mode(WIFI_STA);
    #ifdef STATIC_IP
      IPAddress staticIP(192, 168, 1, 57);
      IPAddress gateway(192, 168, 1, 1);      
      IPAddress subnet(255, 255, 255, 0); 
      IPAddress primaryDNS(192, 168, 1, 1);      
      //IPAddress secondaryDNS(8, 8, 4, 4);      // Optional
      if (!WiFi.config(staticIP, gateway, subnet)) {debug_println("STA Failed to configure");}
    #endif
    debug_print("Connecting to WiFi.");
    WiFi.begin(ssid, password);
    int innerCounter = 0;
    while (WiFi.status() != WL_CONNECTED && innerCounter < 30) {  // wait for 3 seconds in total
      delay(100);
      debug_print(".");
      innerCounter++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      debug_println("\nConnection attempt failed. Trying again...");
      WiFi.disconnect(true);  // Pass 'true' to also erase stored credentials
      delay(3000); // Wait before trying again
    }
  }
  debug_println("\nConnected to WiFi");
  debug_println(WiFi.localIP());
}

void turnLED(bool turnON) {
  if (turnON) {
    float measuredBatt = analogReadMilliVolts(A13) * 2 / 1000.0; // double voltage divider & convert millis to volts
    debug_printf("batt:%1.2f\n", measuredBatt);
    onBoardLED.begin(RGBLED_GPIO, 1, false); 
    onBoardLED.setPixel(0, L_GREEN, true); 
    if (measuredBatt < 3.4) 
      onBoardLED.setPixel(0, L_RED, true); 
    onBoardLED.brightness(LED_bright, true);

    //start timer for LED blink
    bootLED_timer = timerBegin(2, 80, true); //timer #2, 80=1MHz (once per microsecond), count up
    timerAttachInterrupt(bootLED_timer, &onTimer, true);
    timerAlarmWrite(bootLED_timer, 500000, true); //value of timer counter in which to generate timer interrupt, true=reload timer
    timerAlarmEnable(bootLED_timer);
  }else {
    timerDetachInterrupt(bootLED_timer);
    timerAlarmDisable(bootLED_timer);
    timerEnd(bootLED_timer);
  }
}