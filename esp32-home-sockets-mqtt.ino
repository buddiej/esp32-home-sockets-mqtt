#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#include <RCSwitch.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Arduino.h>

#include "_authentification.h"  /* credentials for WIFI and mqtt. Located in libraries folder */


/*****************************************************************************************/
/*                                    GENERAL DEFINE                                     */
/*****************************************************************************************/
#define TRUE  1
#define FALSE 0

#define STATE_OFF           0
#define STATE_ON            1

/*****************************************************************************************/
/*                                    PROJECT DEFINE                                     */
/*****************************************************************************************/

#define MQTT_PAYLOAD_MAX 120

/* Receive topics */
#define TOPIC_SOCKET_1_CHANNEL_1_SET_ON      "wz/socket_1/channel_1/set/on"
#define TOPIC_SOCKET_1_CHANNEL_2_SET_ON      "wz/socket_1/channel_2/set/on"
#define TOPIC_SOCKET_1_CHANNEL_3_SET_ON      "wz/socket_1/channel_3/set/on"
#define TOPIC_SOCKET_1_CHANNEL_4_SET_ON      "wz/socket_1/channel_4/set/on"

#define TOPIC_SOCKET_2_CHANNEL_1_SET_ON      "wz/socket_2/channel_1/set/on"
#define TOPIC_SOCKET_2_CHANNEL_2_SET_ON      "wz/socket_2/channel_2/set/on"
#define TOPIC_SOCKET_2_CHANNEL_3_SET_ON      "wz/socket_2/channel_3/set/on"
#define TOPIC_SOCKET_2_CHANNEL_4_SET_ON      "wz/socket_2/channel_4/set/on"

/* Send topics */
#define TOPIC_SOCKET_1_CHANNEL_1_GET_ON      "wz/socket_1/temperature/get/on"


#define ONE_WIRE_BUS_PIN 14   /* DS18B20 Temperature Sensor */

#define PWM_LED_ALIVE_PIN        16
#define PWM_LED_ALIVE_FREQ       1000
#define PWM_LED_ALIVE_CHANNEL    0
#define PWM_LED_ALIVE_RESOLUTION 8

#define PIN_WIRELESS433_SEND     26

/*****************************************************************************************/
/*                                     TYPEDEF ENUM                                      */
/*****************************************************************************************/


/*****************************************************************************************/
/*                                   TYPEDEF STRUCT                                      */
/*****************************************************************************************/


/*****************************************************************************************/
/*                                         VARIABLES                                     */
/*****************************************************************************************/
/* create an instance of WiFiClientSecure */
WiFiClient espClient;
PubSubClient client(espClient);

RCSwitch mySwitch = RCSwitch();

int mqttRetryAttempt = 0;
int wifiRetryAttempt = 0;
             
long lastMsg = 0;
long loop_5sec_counter = 0;


// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS_PIN);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

static void receivedCallback(char* topic, byte* payload, unsigned int length);
static void mqttconnect(void);

#define DEBUG_MQTT_RECEIVER   Serial.print("Message received: ");  \
                              Serial.print(topic); \
                              Serial.print("\t"); \
                              Serial.print("payload: "); \
                              Serial.println(PayloadString);

/**************************************************************************************************
Function: ArduinoOta_Init()
Argument: void
return: void
**************************************************************************************************/
void ArduinoOta_Init(void)
{
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("esp32-home-sockets-mqtt");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
}                              
/*************************************************************************************************/
/**************************************************************************************************
Function: setup()
return: void
**************************************************************************************************/
/*************************************************************************************************/
void setup()
{
  Serial.begin(115200);

  Serial.println(" ");
  Serial.println("################################");
  Serial.println("# Program Home-sockets v0.5    #");
  Serial.println("################################");
  Serial.println(__FILE__);
  Serial.println(" ");
  Serial.println("Starting ...");
  Serial.println(" ");

  mySwitch.enableTransmit(PIN_WIRELESS433_SEND); 

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(1000);
    Serial.print(".");
    wifiRetryAttempt++;
    if (wifiRetryAttempt > 5) 
    {
      Serial.println("Restarting!");
      ESP.restart();
    }
  }

  ArduinoOta_Init();
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("IP address of server: ");
  Serial.println(serverHostname);
  /* set SSL/TLS certificate */
  /* configure the MQTT server with IPaddress and port */
  client.setServer(serverHostname, 1883);
  /* this receivedCallback function will be invoked
    when client received subscribed topic */
  client.setCallback(receivedCallback);

  /* Start up the library for temperature aqusition */
  sensors.begin();

  ledcAttachPin(PWM_LED_ALIVE_PIN, PWM_LED_ALIVE_CHANNEL); // assign a led pins to a channel
  // Initialize channels
  // channels 0-15, resolution 1-16 bits, freq limits depend on resolution
  // ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution_bits);
  ledcSetup(PWM_LED_ALIVE_CHANNEL, PWM_LED_ALIVE_FREQ, PWM_LED_ALIVE_RESOLUTION); // 12 kHz PWM, 8-bit resolution
 
  Serial.println("Setup finished ... ");
}

/*************************************************************************************************/
/**************************************************************************************************
Function: loop()
return: void
**************************************************************************************************/
/*************************************************************************************************/
void loop() 
{

  ArduinoOTA.handle();
  
  /* if client was disconnected then try to reconnect again */
  if (!client.connected()) {
    mqttconnect();
  }
  /* this function will listen for incomming
  subscribed topic-process-invoke receivedCallback */
  client.loop();
 

  /* we increase counter every 5 secs we count until 5 secs reached to avoid blocking program if using delay()*/
  long now = millis();
  
  /* calling every 5 sec. */
  if (now - lastMsg > 5000)
  {
    /* store timer value */
    lastMsg = now;

    loop_5sec_counter++;
    
    sensors.requestTemperatures(); // Send the command to get temperatures
    /* After we got the temperatures, we can print them here. */
    /* We use the function ByIndex, and as an example get the temperature from the first sensor only. */
    Serial.print("Temperature DS18B20: ");
    Serial.println(sensors.getTempCByIndex(0));  
    Serial.println("Test...");

    if((loop_5sec_counter % 2) == 0)
    {
      ledcWrite(PWM_LED_ALIVE_CHANNEL, 0); // set the brightness of the LED
    }
    else
    {
      ledcWrite(PWM_LED_ALIVE_CHANNEL, 255); // set the brightness of the LED
    }
    
    /********************************************************************************************/
    /************************      HANDLING OF Send MQTT TOPICS     *****************************/ 
    /********************************************************************************************/
    char data[MQTT_PAYLOAD_MAX];
    String json; 
       
  
    char temp[8];
    char humidity[8];
    dtostrf(sensors.getTempCByIndex(0),  6, 2, temp);
    json = "{\"temperature\":" + String(temp) + "}";
    json.toCharArray(data, (json.length() + 1));
    client.publish(TOPIC_SOCKET_1_CHANNEL_1_GET_ON, data, false);

  }

}

/**************************************************************************************************
Function: receivedCallback()
Argument: char* topic ; received topic
          byte* payload ; received payload
          unsigned int length ; received length
return: void
**************************************************************************************************/
void receivedCallback(char* topic, byte* payload, unsigned int length) 
{
  uint8_t Loc_Status;
  String val_status;
  uint16_t Loc_Led_Value;
  String json_s;
  char json_data[MQTT_PAYLOAD_MAX];
  uint32_t Loc_nec_code;

  
  char PayloadString[length + 1 ];
  /* convert payload in string */
  for(byte i=0;i<length;i++)
  {
    PayloadString[i] = payload[i];
  }
  PayloadString[length] = '\0';

  /* Debug */
  DEBUG_MQTT_RECEIVER

  StaticJsonBuffer<250> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(PayloadString);

  if (!root.success()) 
  {
    Serial.println("JSon parseObject() failed");
  } 
  else 
  {
    Serial.println("JSON message parsed succesfully");
  }

  /********************************************************************************************/
  /********************      HANDLING OF Received MQTT TOPICS WITH JASON     ******************/ 
  /********************************************************************************************/
  
  /*+++++++++++++++++++++++++++++ Set control +++++++++++++++++++++++++++++++++++++++*/ 
  if(strcmp(topic, TOPIC_SOCKET_1_CHANNEL_1_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 1 channel 1 set: ");
     
      Serial.println(Loc_Status, DEC);
      mySwitch.setPulseLength(200);
      delay(10);
      if(Loc_Status == 1)
      {
          mySwitch.send("000000000000000000001111");
      }
      else
      {
         mySwitch.send("000000000000000000001110");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_1_CHANNEL_2_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 1 channel 2 set: ");
     
      Serial.println(Loc_Status, DEC);
      mySwitch.setPulseLength(200);
      delay(10);
      if(Loc_Status == 1)
      {
          mySwitch.send("000000000000000000000111");
      }
      else
      {
         mySwitch.send("000000000000000000000110");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_1_CHANNEL_3_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 1 channel 3 set: ");
     
      Serial.println(Loc_Status, DEC);
      mySwitch.setPulseLength(200);
      delay(10);
      if(Loc_Status == 1)
      {
          mySwitch.send("000000000000000000001011");
      }
      else
      {
         mySwitch.send("000000000000000000001010");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_1_CHANNEL_4_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 1 channel 4 set: ");
     
      Serial.println(Loc_Status, DEC);
      mySwitch.setPulseLength(200);
      delay(10);
      if(Loc_Status == 1)
      {
          mySwitch.send("000000000000000000000011");
      }
      else
      {
         mySwitch.send("000000000000000000000010");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_2_CHANNEL_1_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 2 channel 1 set: ");
     
      Serial.println(Loc_Status, DEC);
      if(Loc_Status == 1 || Loc_Status == 0)
      {
          mySwitch.setPulseLength(294);
          delay(10);
          mySwitch.send("000000000000000000001000");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_2_CHANNEL_2_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 2 channel 2 set: ");
     
      Serial.println(Loc_Status, DEC);
      if(Loc_Status == 1 || Loc_Status == 0)
      {
          mySwitch.setPulseLength(294);
          delay(10);
          mySwitch.send("000000000000000000000100");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_2_CHANNEL_3_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 2 channel 3 set: ");
     
      Serial.println(Loc_Status, DEC);
      if(Loc_Status == 1 || Loc_Status == 0)
      {
          mySwitch.setPulseLength(294);
          delay(10);
          mySwitch.send("000000000000000000000010");
      }
    }
  }
  if(strcmp(topic, TOPIC_SOCKET_2_CHANNEL_4_SET_ON)==0)
  {
    if(root.containsKey("status")) 
    {
      Loc_Status = root["status"];
      Serial.print("status socket 2 channel 4 set: ");
     
      Serial.println(Loc_Status, DEC);
      if(Loc_Status == 1 || Loc_Status == 0)
      {
          mySwitch.setPulseLength(294);
          delay(10);
          mySwitch.send("000000000000000000000001");
      }
    }
  }



}


/**************************************************************************************************
Function: mqttconnect()
Argument: void
return: void
**************************************************************************************************/
void mqttconnect(void)
{
  /* Loop until reconnected */
  while (!client.connected()) 
  {
    Serial.print("MQTT connecting ...");
    /* client ID */
    String clientId = "esp32-home-sockets-mqtt";
    /* connect now */
    if (client.connect(clientId.c_str(), serverUsername.c_str(), serverPassword.c_str()))
    {
      Serial.println("connected");
      /* subscribe topic's */
      client.subscribe(TOPIC_SOCKET_1_CHANNEL_1_SET_ON);
      client.subscribe(TOPIC_SOCKET_1_CHANNEL_2_SET_ON);
      client.subscribe(TOPIC_SOCKET_1_CHANNEL_3_SET_ON);
      client.subscribe(TOPIC_SOCKET_1_CHANNEL_4_SET_ON);
      client.subscribe(TOPIC_SOCKET_2_CHANNEL_1_SET_ON);
      client.subscribe(TOPIC_SOCKET_2_CHANNEL_2_SET_ON);
      client.subscribe(TOPIC_SOCKET_2_CHANNEL_3_SET_ON);
      client.subscribe(TOPIC_SOCKET_2_CHANNEL_4_SET_ON);
      
    } 
    else 
    {
      Serial.print("failed, status code =");
      Serial.print(client.state());
      Serial.println("try again in 5 seconds");
      /* Wait 5 seconds before retrying */
      delay(5000);
      mqttRetryAttempt++;
      if (mqttRetryAttempt > 5) 
      {
        Serial.println("Restarting!");
        ESP.restart();
      }
    }
  }
}
