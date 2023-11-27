const char * ssid = "";
const char * password = "";

const int maxLamps = 1;
const IPAddress lampIP[maxLamps] = {
  IPAddress(192,168,1,148),
  //IPAddress(192,168,1,136),   //Pole
  //IPAddress(192,168,1,244),   //Wall
  //IPAddress(192,168,1,30),    //Desk
  // ... other IP addresses
};
