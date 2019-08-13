// ESP8266 ESP-01 coded to work with Flespi MQTT broker
// Reinhard Dämon, YUM, 10/2018
// Does not work standalone, must be commanded by serial interface!
//
// Prerequisite:
//  1.) Register on Flespi (flespi.io) and prolong your given token
//  2.) At the beginning of this program make the appropriate
//      settings for MQTT_TOKEN, WLAN_SSID and WLAN_PASS
//  2.) Flash this programm into ESP8266 ESP-01 
//      (this can be tricky!)
//
// Typical operating procedure:
//  0.) Connect ESP8266 ESP-01 to USB Port, power with 3V3 (there is a nice 
//      adapter for doing this). Open some serial terminal (115200 8N1) and
//      connect to the appropriate port
//  1.) Apply the Wifi SSID and password (default values can be set herein!)
//      and connect to
//  2.) Check Wifi status connected
//  3.) Check MQTT status connected
//  3.) Publish to and/or subscribe to topics
//  4.) Fetch the received topics and messages
// 
// Simple standalone test:
//  A.) Connect a LED (in series with a 470 Ohm resistor) from GPIO2 to GND
//  B.) Connect a pulled up (10 kOhm) push button from GPIO0 to GND
//  C.) Connect CH_PD to 3V3 (there is a nice adapter for doing this)
//  D.) Power the device with 3V3 (there is a nice adapter for doing this)    
//  E.) Wait for LED info:
//        1 blink:  connected to Wifi
//        2 blink:  connected to Wifi and Flespi MQTT broker (fully OK)
//        
//  F.) Short press the push button and watch toggling the LED
//      (You can also watch and control the device with https://flespi.com/mqtt-broker
//      ,Topic: "dummy/key")
//  G.) Subscribe to topic "dummy/uptime" (use, e.g: https://flespi.com/mqtt-broker)
//      and see the published uptime every 10 seconds



#define DEBUG
#undef DEBUG

#ifdef DEBUG
  #define DEBUG_PRINTLN(str)    \
     Serial.print("DEBUG: "); \
     Serial.print(__LINE__);     \
     Serial.print(": ");      \
     Serial.println(str);
  #define DEBUG_PRINT(str)    \
     Serial.print("DEBUG: "); \
     Serial.print(__LINE__);     \
     Serial.print(": ");      \
     Serial.print(str);
  #define DEBUG_PRINT_CRLF() \
    Serial.println();     
  #else
  #define DEBUG_PRINTLN(str)
  #define DEBUG_PRINT(str)
  #define DEBUG_PRINT_CRLF()
#endif


#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <MQTT.h>         // https://github.com/256dpi/arduino-mqtt
// #include <QueueList.h>    // https://playground.arduino.cc/Code/QueueList
#include <CircularBuffer.h> //https://github.com/rlogiacco/CircularBuffer

//#include <MegunoLink.h>
//#include "CommandHandler.h"
//CommandHandler<> SerialCommandHandler;



/************************* MAIN DEFINITIONS *********************************/
#define MQTT_SERVER           "mqtt.flespi.io"
#define MQTT_SERVERPORT       8883
#define MQTT_CERT_FINGERPRINT "3B BC 95 33 E5 AB C1 1C C8 FC 37 57 F2 94 2C 43 8E 3B 66 F3"
#define MQTT_TOKEN            "insert your token here"
#define WLAN_SSID             "insert your wlan ssid here"
#define WLAN_PASS             "insert your wlan password here"

#define MQTT_KEY_TOPIC        "dummy/key"
#define MQTT_KEY_ON           "on"
#define MQTT_KEY_OFF          "off"
#define MQTT_UPT_TOPIC        "dummy/uptime"

#define IFTTT_HOST            "maker.ifttt.com"
#define IFTTT_KEY             "insert your ifttt key here"
#define IFTTT_URL             "https://maker.ifttt.com/trigger/ESP8266_Flespi_project/with/key/insert your ifttt key here"


#define UPTIME_SLOT           10000       // every 10 seconds

/************************* GLOBAL DEFS *********************************/
const int ledPin = 2;                                       // GPIO2 pin
int ledStatus = 0;

const int keyPin = 0;                                       // push-button
volatile bool keyPressed = false;       


String strRX = "";
volatile boolean strRXready = false;
volatile boolean strRX_CR = false;
volatile boolean strRX_LF = false;

#define CB_LENGTH 20
CircularBuffer <String,CB_LENGTH> queue_topic;
CircularBuffer <String,CB_LENGTH> queue_payload;

String strWlanSsid = WLAN_SSID;
String strWlanPass = WLAN_PASS;
String strMqttToken = MQTT_TOKEN;

boolean onceConnectedFlag = false;
/**************************** Local files ***********************************/
extern "C" {
#include "user_interface.h"
}
uint32_t esp8266_free;


/********************* Global connection instances **************************/
// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;
MQTTClient mqtt_yum(2000);                                                                                                                  
/*******************+++++** Headers ***********************++++++++++++++***/


/*************************** Sketch Code ************************************/

//----------------------------------------- SETUP ----------------------------
// Generic example for ESP8266 wifi connection
void setup() {
  String cmdString = "";
  Serial.begin(115200);
  delay(5000);
  pinMode(ledPin, OUTPUT);
  pinMode(keyPin, INPUT_PULLUP);
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINTLN("ESP8266 ESP-01: Serially commanded secured flespi MQTT");

  strWlanSsid = WLAN_SSID;
  strWlanPass = WLAN_PASS;
  strMqttToken = MQTT_TOKEN;

  //queue_topic.setPrinter(Serial); 
  //queue_payload.setPrinter(Serial); 

  // Try to connect to the default WiFi access point and MQTT broker:
  cmdString = "w:con:"; cmdString.concat(strWlanSsid); cmdString.concat(",");
  cmdString.concat(strWlanPass);cmdString.concat(","); cmdString.concat(strMqttToken);
  cmd_w_con(cmdString);
  delay(1000);

  // Subscribe to the dummy key topic:
  cmdString = "w:sub:"; cmdString.concat(MQTT_KEY_TOPIC); cmdString.concat(",1");
  cmd_w_sub(cmdString);
  delay(1000);     
  
  // connect to server check the fingerprint of flespi.io's SSL cert (from Adafruit MQTTS example)
  //verify_fingerprint(client);

  // attach interrupt for the push-button key:
  attachInterrupt(digitalPinToInterrupt(keyPin), ISR_keyPin, FALLING);
}
//----------------------------------------------------------------------------

//------------------------------ LOOP ----------------------------------------
void loop() {
  char string_buf[28];
  int32 new_millis;
  static int32 old_millis = millis();
  String cmdString = "";

  // Connected to WiFi and MQTT?
  if (cmd_r_con_silent() == true) {
    onceConnectedFlag = true;  
  }
  // do we have to reconnect?
  else {
    if (onceConnectedFlag == true) {
      // Try to connect to WiFi access point and MQTT broker:
      cmdString = "w:con:"; cmdString.concat(strWlanSsid); cmdString.concat(",");
      cmdString.concat(strWlanPass);cmdString.concat(","); cmdString.concat(strMqttToken);
      cmd_w_con(cmdString);
      delay(1000);
      // Subscribe to the dummy key topic:
      cmdString = "w:sub:"; cmdString.concat(MQTT_KEY_TOPIC); cmdString.concat(",1");
      cmd_w_sub(cmdString);
      delay(1000);             
    }
  }

  // key pressed?
  if (keyPressed == true) {
    DEBUG_PRINTLN(__PRETTY_FUNCTION__);
    DEBUG_PRINTLN("key pressed!");
    if (ledStatus == 1) {
      cmdString = "w:pub:"; cmdString.concat(MQTT_KEY_TOPIC); cmdString.concat(",");
      cmdString.concat("1,false,");cmdString.concat(MQTT_KEY_OFF);
      DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);      
      cmd_w_pub(cmdString);
    }
    else {
      cmdString = "w:pub:"; cmdString.concat(MQTT_KEY_TOPIC); cmdString.concat(",");
      cmdString.concat("1,false,");cmdString.concat(MQTT_KEY_ON);   
      DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);  
      cmd_w_pub(cmdString);
    }
    keyPressed = false;
    delay(500);
    
    // IFTTT:
    const char * ifttt_host_ = IFTTT_HOST;
    const char * ifttt_key_ =  IFTTT_KEY;
    String ifttt_url_       =  IFTTT_URL;
    DEBUG_PRINT("Trying to connect to IFTTT: "); DEBUG_PRINTLN(ifttt_host_); 
    WiFiClient client1; 
    if (!client1.connect(ifttt_host_,80)) {
      DEBUG_PRINT("client NOT connected to IFTTT: "); DEBUG_PRINTLN(ifttt_host_);        
    }
    if (client1.connect(ifttt_host_, 80)) {
      DEBUG_PRINT("client connected to IFTTT: "); DEBUG_PRINTLN(ifttt_host_);
      client1.println(String("POST ") + ifttt_url_ + " HTTP/1.1\r\n" +
        "Host: " + ifttt_host_ + "\r\n" +
        "Content-Type: application/x-www-form-urlencoded\r\n" +
        "Content-Length: 13\r\n\r\n" +
        "value1=" + "555" + "\r\n");
    }
  }

  serialEvent();
  // Serial command ready?
  if (strRXready) {
    serialProcess(strRX);    
  }
    
  // Auto-publish uptime:
  new_millis = millis();
  if ( (new_millis - old_millis) > UPTIME_SLOT) {
    DEBUG_PRINTLN(__PRETTY_FUNCTION__);
    DEBUG_PRINT("Up-time: "); DEBUG_PRINTLN(new_millis);
    old_millis = new_millis;
    //Serial.print("Up-time: "); Serial.println(val);
    //itoa(new_millis, string_buf, 15);
    sprintf(string_buf,"%d",new_millis);
    mqtt_yum.publish(String(MQTT_UPT_TOPIC),string_buf);    
  }

  uint32_t ms_before = millis();
  mqtt_yum.loop();
  uint32_t ms_after = millis();
  uint32_t ms_mqttloop;
  if (ms_after > ms_before) {
    ms_mqttloop = ms_after - ms_before;
  }
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("MQTT loop took (ms): "); DEBUG_PRINTLN(ms_mqttloop);  
  delay(100);

  // free RAM:
  esp8266_free = system_get_free_heap_size();
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("Free RAM: "); DEBUG_PRINTLN(esp8266_free);  
  
  delay(1000);
  
}
//----------------------------------------------------------------------------


//------------------------------ COMMANDS ------------------------------------
int cmd_r_ele(void) {
  // get how many topics/payloads are in the FIFO buffer
  // e.g: r:ele:<CR><LF>
  // answer (3 topics/payloads are in the buffer):
  // e.g: r:con:3<CR><LF>
  // answer, when nothing is in the buffer:
  // e.g: r:con:0<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("number of topics/payloads in buffer: "); DEBUG_PRINTLN(queue_topic.size());
  Serial.print("r:ele:"); Serial.println(queue_topic.size());
  return (queue_topic.size());
}

void cmd_r_rcv(void) {
  // get a topic / payload pair from the FIFO buffer
  // e.g: r:rcv:<CR><LF>
  // answer:
  // e.g: r:top:this_is_the_topic<CR><LF>
  //      r:pay:this_is_the_payload<CR><LF>  
  String mystring;
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  if (!queue_topic.isEmpty()) {
    mystring = queue_topic.shift();
    DEBUG_PRINT("topic popped from buffer: "); DEBUG_PRINTLN(mystring);  
    Serial.print("r:top:"); Serial.println(mystring);  
  
        // MegunoLink Test:
        //InterfacePanel MyPanel;
        //MyPanel.SetText(F("IPTextBoxMsgTopic"), mystring.c_str());
        // --> workaround:
        String stringuhu = "{UI|SET|IPTextBoxMsgTopic.Text=";
        stringuhu.concat(mystring);
        stringuhu.concat("}");
        Serial.println(stringuhu.c_str());
        //MyPanel.SetBackColor("IPTextBoxMsgTopic", "Blue");
    
    mystring = queue_payload.shift();
    DEBUG_PRINT("payload popped from buffer: "); DEBUG_PRINTLN(mystring);  
    Serial.print("r:pay:"); Serial.println(mystring);  

        // MegunoLink Test:
        //InterfacePanel MyPanel;
        //MyPanel.SetText(F("IPTextBoxMsgPayload"), mystring.c_str());
        // --> workaround:
        stringuhu = "{UI|SET|IPTextBoxMsgPayload.Text=";
        stringuhu.concat(mystring);
        stringuhu.concat("}");
        Serial.println(stringuhu.c_str());
        
        //MyPanel.SetBackColor(F("IPTextBoxMsgPayload"), F("Red"));
      
  }
  else {
    String stringuhu;
    mystring = "";
    DEBUG_PRINT("topic buffer empty!: "); DEBUG_PRINTLN(mystring);  
    Serial.print("r:top:"); Serial.println(mystring);  
        // MegunoLink:
        stringuhu = "{UI|SET|IPTextBoxMsgTopic.Text= }";
        Serial.println(stringuhu.c_str());
    mystring = "";
    DEBUG_PRINT("payload buffer empty: "); DEBUG_PRINTLN(mystring);  
    Serial.print("r:pay:"); Serial.println(mystring);    
        // MegunoLink:
        stringuhu = "{UI|SET|IPTextBoxMsgPayload.Text= }";
        Serial.println(stringuhu.c_str());    
  }  
}

void cmd_w_clr(void) {
  // clear the topic / payload buffer
  // e.g: w:clr:<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  queue_topic.clear();
  queue_payload.clear();
}   

int cmd_r_ele_silent(void) {
  // get how many topics/payloads are in the FIFO buffer
  // e.g: r:ele:<CR><LF>
  // answer (3 topics/payloads are in the buffer):
  // e.g: r:con:3<CR><LF>
  // answer, when nothing is in the buffer:
  // e.g: r:con:0<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("number of topics/payloads in buffer: "); DEBUG_PRINTLN(queue_topic.size());
  return (queue_topic.size());
}

void cmd_r_sca(void) {
  // scan and return all available WiFi network SSIDs
  // ATTENTION: this will maybe disconnect from any WiFi AP!
  // e.g: r:sca:<CR><LF>
  // answer:
  // r:sca:SSID,RSSI,encryption
  // e.g. answer (3 Wifi networks available):
  // e.g: r:sca:myWLAN_93,-33,*<CR><LF>       // protected  
  // e.g: r:sca:GUEST_WLAN_17set,71,*<CR><LF> // protected   
  // e.g: r:sca:myWLAN_93, <CR><LF>           // unprotected
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  WiFi.mode(WIFI_STA);
  //WiFi.disconnect();
  int n = WiFi.scanNetworks();
  DEBUG_PRINTLN("WiFi Network scan done");
  if (n == 0) {
    DEBUG_PRINTLN("No WiFi networks found");
  } 
  else {
    DEBUG_PRINT("Number of networks found: "); DEBUG_PRINTLN(n);
    for (int i = 0; i < n; ++i) {
      // Print SSID for each network
      DEBUG_PRINT("SSID: "); DEBUG_PRINTLN(WiFi.SSID(i));
      DEBUG_PRINT("RSSI: "); DEBUG_PRINTLN(WiFi.RSSI(i));      
      Serial.print("r:sca:");
      Serial.print(WiFi.SSID(i));
      Serial.print(",");
      Serial.print(WiFi.RSSI(i));
      Serial.print(",");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }  
}

void cmd_w_con(String &cmdString) {
  // connect to the specified WiFi network and the given MQTT broker
  // e.g: w:con:SSID,PASS,TOKEN<CR><LF>
  // e.g: w:con:myWLAN_93,myPASS_39,lkadsfajsdlkj42314jklj21354lköj1234<CR><LF>  
  // and some special situations ... see below!
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);
  digitalWrite(ledPin, LOW); 
  String strRX_ssid = "";
  int i;
  int counter;
  for (i = 6; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_ssid += cmdString[i];
    }
  }  
  strWlanSsid = strRX_ssid;                   // filtered SSID String
  
  i++;  
  String strRX_pass = "";
  for (i; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_pass += cmdString[i];
    }
  }    
  strWlanPass = strRX_pass;                   // filtered PASS String 

  String strRX_token = "";  //strMqttToken;   
  if (cmdString[i] == ',') {
    i++;
    strRX_token = "";
    for (i; i < cmdString.length(); i++) {
      if (cmdString[i] == ',') {
        break;
      }
      else {
        strRX_token += cmdString[i];
      }
    }      
  }
  strMqttToken = strRX_token;               // filtered TOKEN String

  // special cases:
  DEBUG_PRINTLN("Handling special cases:");
  DEBUG_PRINT("SSID   length: "); DEBUG_PRINTLN(strRX_ssid.length());
  DEBUG_PRINT("PASS   length: "); DEBUG_PRINTLN(strRX_pass.length());  
  DEBUG_PRINT("TOKEN  length: "); DEBUG_PRINTLN(strRX_token.length());  
        //strWlanSsid = strRX_ssid;
        //strWlanPass = strRX_pass;
        //strMqttToken = strRX_token;  
  // e.g: w:con: 
  //      --> use default SSID, default PASS, default TOKEN
  if ( (strRX_ssid.length() == 0) & (strRX_pass.length() == 0) & \
      (strRX_token.length() ==0) ) {
        strWlanSsid = WLAN_SSID;
        strWlanPass = WLAN_PASS;
        strMqttToken = MQTT_TOKEN;
      }
  else
  // e.g: w:con:SSID or w:con:SSID, or w:con:SSID,,
  //      --> use given SSID, no PASS, default TOKEN
  //  
  if ( (strRX_pass.length() == 0) & (strRX_token.length() == 0) ) {
    //strWlanPass = "";
    strMqttToken = MQTT_TOKEN;
  }
  

  char* strWlanSsid_char = (char *)strWlanSsid.c_str();  //strRX_ssid.c_str();    
  char* strWlanPass_char = (char *)strWlanPass.c_str();   //strRX_pass.c_str();
  char* strMqttToken_char = (char *)strMqttToken.c_str();  //strRX_token.c_str();

  // Connect to WiFi access point.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);
  DEBUG_PRINT("strRX SSID: "); DEBUG_PRINTLN(strRX_ssid);
  DEBUG_PRINT("strRX PASS: "); DEBUG_PRINTLN(strRX_pass);  
  DEBUG_PRINT("strRX TOKEN: "); DEBUG_PRINTLN(strRX_token);  
  DEBUG_PRINT("final SSID: "); DEBUG_PRINTLN(strWlanSsid_char);
  DEBUG_PRINT("final PASS: "); DEBUG_PRINTLN(strWlanPass_char);  
  DEBUG_PRINT("final TOKEN: "); DEBUG_PRINTLN(strMqttToken_char);  
  DEBUG_PRINTLN("Connecting...: ");

  if (strWlanPass.length() == 0) {
    WiFi.begin(strWlanSsid_char);
  }
  else {
    WiFi.begin(strWlanSsid_char, strWlanPass_char);    
  }
  delay(2000);

  counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    DEBUG_PRINTLN(".");
    counter++;
    if (counter > 20)
      return;      
  }
  digitalWrite(ledPin, HIGH); delay(500); digitalWrite(ledPin, LOW); 
  DEBUG_PRINTLN("WiFi connected");
  DEBUG_PRINT("IP address: "); DEBUG_PRINTLN(WiFi.localIP());

  String clientMac = "";
  unsigned char mac[6];
  WiFi.macAddress(mac);
  clientMac += macToStr(mac);
  DEBUG_PRINT("MAC: "); DEBUG_PRINTLN(clientMac);    
  mqtt_yum.disconnect();
  delay(200);
  mqtt_yum.begin(MQTT_SERVER,MQTT_SERVERPORT,client);
  mqtt_yum.onMessage(messageReceived);
  mqtt_yum.setOptions(10,true,1000);
  DEBUG_PRINTLN("Connecting with 256dpi mqtt lib:...");    
  counter = 0;     
  DEBUG_PRINT("Token: "); DEBUG_PRINTLN(strRX_token);                                                                                              
  while (!mqtt_yum.connect(clientMac.c_str(), strMqttToken_char)) {
    DEBUG_PRINTLN(".");
    delay(1000);
    counter++;
    if (counter > 20)
      return;
  }
  digitalWrite(ledPin, HIGH); delay(500); digitalWrite(ledPin, LOW); 
  delay(500); digitalWrite(ledPin, HIGH); delay(500); digitalWrite(ledPin, LOW); 
  DEBUG_PRINTLN("MQTT Connected!");

      // Subscribe to the dummy key topic:
      String cmdString1;
      cmdString1 = "w:sub:"; cmdString1.concat(MQTT_KEY_TOPIC); cmdString1.concat(",1");
      cmd_w_sub(cmdString1);
      delay(1000);              
}

boolean cmd_r_con(void) {
  // check connections (WiFi and MQTT)
  // e.g: r:con:<CR><LF>
  // answer, when connected:
  // e.g: r:con:+<CR><LF>
  // answer, when not connected:
  // e.g: r:con:-<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  boolean wifi_con = false;
  boolean mqtt_con = false;
  boolean con = false;
  if (WiFi.status() == WL_CONNECTED) {
    wifi_con = true;
  }
  DEBUG_PRINT("wifi_con: "); DEBUG_PRINTLN(wifi_con);
  
  if (mqtt_yum.connected()) {
    mqtt_con = true;
  }
  DEBUG_PRINT("mqtt_con: "); DEBUG_PRINTLN(mqtt_con); 
  con = wifi_con & mqtt_con;
  DEBUG_PRINT("con: "); DEBUG_PRINTLN(con);   
  DEBUG_PRINT("SSID: "); DEBUG_PRINTLN(strWlanSsid);   
  DEBUG_PRINT("PASS: "); DEBUG_PRINTLN(strWlanPass); 
  DEBUG_PRINT("TOKEN: "); DEBUG_PRINTLN(strMqttToken);   
  if (con) {
    Serial.print("r:con:");
    Serial.println(strWlanSsid);
  }
  else {
    Serial.println("r:con:");
  }
  return (con);
}

boolean cmd_r_con_silent(void) {
  // check connections (WiFi and MQTT)
  // e.g: r:con:<CR><LF>
  // answer, when connected:
  // e.g: r:con:+<CR><LF>
  // answer, when not connected:
  // e.g: r:con:-<CR><LF>
  boolean wifi_con = false;
  boolean mqtt_con = false;
  boolean con = false;
  if (WiFi.status() == WL_CONNECTED) {
    wifi_con = true;
  }
  if (mqtt_yum.connected()) {
    mqtt_con = true;
  }
  con = wifi_con & mqtt_con;
  return (con);
}

void cmd_w_pub(String &cmdString) {
  // publish to a topic with Qos and retain option
  // e.g: w:pub:topic,qos,retainflag,payload<CR><LF>
  // e.g: w:pub:top/1,1,false,akdlsfjlajsddf<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);
  String strRX_topic = "";
  int i;
  for (i = 6; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_topic += cmdString[i];
    }
  }  
  i++;
  String strRX_qos = "";
  strRX_qos += cmdString[i];
  i++;
  i++;
  String strRX_retainflag = "";
  for (i; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_retainflag += cmdString[i];
    }
  }
  i++;
  String strRX_payload = "";
  for (i; i < cmdString.length(); i++) {
    strRX_payload += cmdString[i];
    }
  
  int strRX_qos_int = (int)(strRX_qos.toInt());
  boolean strRX_retainflag_bool = false;
  if (strRX_retainflag.compareTo("true") == 0)
    strRX_retainflag_bool = true;
    
  DEBUG_PRINT("strRX_topic: "); DEBUG_PRINTLN(strRX_topic);
  DEBUG_PRINT("strRX_qos_int: "); DEBUG_PRINTLN(strRX_qos_int);  
  DEBUG_PRINT("strRX_payload: "); DEBUG_PRINTLN(strRX_payload);
  DEBUG_PRINT("strRX_retainflag_bool: "); DEBUG_PRINTLN(strRX_retainflag_bool);  
  mqtt_yum.publish(strRX_topic, strRX_payload,strRX_retainflag_bool,strRX_qos_int);
}

void cmd_w_sub(String &cmdString) {
  // subscribe to a topic with QoS
  // e.g: w:sub:topic,qos<CR><LF>
  // e.g: w:pub:top/1,1<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);
  String strRX_topic = "";
  int i;
  for (i = 6; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_topic += cmdString[i];
    }
  }  
  i++;
  String strRX_qos = "";
  strRX_qos += cmdString[i];
  int strRX_qos_int = (int)(strRX_qos.toInt());
  DEBUG_PRINT("strRX_topic: "); DEBUG_PRINTLN(strRX_topic);
  DEBUG_PRINT("strRX_qos_int: "); DEBUG_PRINTLN(strRX_qos_int);  
  mqtt_yum.subscribe(strRX_topic, strRX_qos_int);
}      

void cmd_w_uns(String &cmdString) {
  // unsubscribe from a topic
  // e.g: w:uns:topic<CR><LF>
  // e.g: w:uns:top/1<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);  
  String strRX_topic = "";
  int i;
  for (i = 6; i < cmdString.length(); i++) {
    if (cmdString[i] == ',') {
      break;
    }
    else {
      strRX_topic += cmdString[i];
    }
  }  
  DEBUG_PRINT("strRX_topic: "); DEBUG_PRINTLN(strRX_topic);  
  mqtt_yum.unsubscribe(strRX_topic);
}     

void cmd_w_dis(void) {
  // unsubscribe from a topic
  // e.g: w:dis:<CR><LF>
  // e.g: w:dis:<CR><LF>
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  // DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);  
  mqtt_yum.disconnect(); 
  delay(500);
  WiFi.disconnect();
  delay(2000);
  onceConnectedFlag = false;
}    
//----------------------------------------------------------------------------


//------------------------------ ROUTINES, FUNCTIONS-------------------------- 
String macToStr(const uint8_t* mac) {
  String result;
  for (int i = 0; i < 6; ++i) {
  result += String(mac[i], 16);
  if (i < 5)
  result += ':';
  }
  return result;
}  

void messageReceived(String &topic, String &payload) {
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("topic: "); DEBUG_PRINTLN(topic);
  DEBUG_PRINT("payload: "); DEBUG_PRINTLN(payload);  
  if (topic.compareTo(MQTT_KEY_TOPIC) == 0) {
    if (payload.compareTo(MQTT_KEY_ON) == 0) {
      DEBUG_PRINTLN("CMD ledon detected");
      digitalWrite(ledPin, HIGH); 
      ledStatus = 1;
      mqtt_yum.publish(MQTT_KEY_TOPIC,"LED is on");       
    }
    if (payload.compareTo(MQTT_KEY_OFF) == 0) {
      DEBUG_PRINTLN("CMD ledoff detected");
      digitalWrite(ledPin, LOW); 
      ledStatus = 0;
      mqtt_yum.publish(MQTT_KEY_TOPIC,"LED is off");       
    }
  }
  // put topic and payload into buffer:
  queue_topic.push(topic);
  queue_payload.push(payload);  
}

void ISR_keyPin() {
  keyPressed = true;
}

void serialEvent() {
  char inChar;
  while (Serial.available()) {
    inChar = (char)Serial.read();
    strRX += inChar;

    if (strRX_CR == true) {
      if (inChar == 0x0A) {
        strRXready = true;
      }
      else {
        strRX_CR = false;
        strRX_LF = false;
      }
    }
    if (strRX_LF == true) {
      if (inChar == 0x0D) {
        strRXready = true;
      }
      else {
        strRX_CR = false;
        strRX_LF = false;
      }
    }
  if (inChar == 0x0A)
    strRX_LF = true;
  if (inChar == 0x0D)
    strRX_CR = true;  
  }

  if (strRXready == true) {
    strRX.remove(strRX.length()-2);        
  }
}

void strRX_reset() {
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  strRX = "";  
  strRX_CR = false;
  strRX_LF = false;
  strRXready = false;
}

void serialProcess(String cmdString) {
  DEBUG_PRINTLN(__PRETTY_FUNCTION__);
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);
  strRX_reset();    
  DEBUG_PRINT("cmdString: "); DEBUG_PRINTLN(cmdString);
  DEBUG_PRINT("strRX: "); DEBUG_PRINTLN(strRX);
  if (cmdString.startsWith("w:pub:")) 
    cmd_w_pub(cmdString);
  if (cmdString.startsWith("w:sub:")) 
    cmd_w_sub(cmdString);  
  if (cmdString.startsWith("w:uns:")) 
    cmd_w_uns(cmdString); 
  if (cmdString.startsWith("w:con:")) 
    cmd_w_con(cmdString);   
  if (cmdString.startsWith("w:dis:")) 
    cmd_w_dis();
  if (cmdString.startsWith("r:con:")) 
    cmd_r_con();   
  if (cmdString.startsWith("r:ele:")) 
    cmd_r_ele();   
  if (cmdString.startsWith("r:rcv:")) 
    cmd_r_rcv();   
  if (cmdString.startsWith("w:clr:")) 
    cmd_w_clr();
  if (cmdString.startsWith("r:sca:")) 
    cmd_r_sca();    
        
        
}
