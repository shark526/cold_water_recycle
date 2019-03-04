// TODO: add resister for the pullup button D1, intergrate the 4.7k for pullup D3 pin
// enable/disable mqtt
//  removed the dead loop with simple timer
/*
  Configuration (HA) :
  warm_water:
  - platform: mqtt
    name: Warm Water'
    state_topic: 'home/pump/status'
    command_topic: 'home/pump/switch'
    optimistic: false
*/

// Include the libraries we need
#include <FS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
//for LED status
#include <Ticker.h>
Ticker ticker;
#include <SimpleTimer.h> // here is the SimpleTimer library

SimpleTimer timer; // Create a Timer object called "timer"!

//define your default values here, if there are different values in config.json, they are overwritten.
// MQTT: ID, server IP, port, username and password
char mqtt_server[40] = "server ip";
char mqtt_port[6] = "1883";
char mqtt_user[40] = "homeassistant";
char mqtt_password[40] = "mqtt password";


const PROGMEM char *MQTT_CLIENT_ID = "warm_water";
// MQTT: topics
const char *MQTT_PUMP_STATE_TOPIC = "home/pump/status";
const char *MQTT_PUMP_COMMAND_TOPIC = "home/pump/switch";
const  char* MQTT_PUMP_SENSOR_TOPIC = "home/pump/sensor";

// payloads by default (on/off)
const char *PUMP_ON = "ON";
const char *PUMP_OFF = "OFF";

//flag for saving data
bool shouldSaveConfig = false;

// Data wire is plugged into port 2 on the Arduino
#define BTN_RESET D1
#define STATUS_LED D4
#define ONE_WIRE_BUS D3 // for data of DS18X21
#define RADIO_TUBE_PIN D0
#define WATER_PUMP_PIN D5
#define BEEP_PIN D6

#define MQTT_VERSION MQTT_VERSION_3_1_1

const PROGMEM char *HOST_NAME = "Cold-Water-Recycle";
float tempC;
float tempThreshold = 25;
int delayTime = 1000;
int sensorPin = A0;
int sensorValue = 0;
bool isRecycling = false;

// for mqtt connection retry logic
long retryTimes = 2;
long baseRetryTime = 0 ;
boolean giveUpRetryMqtt = false;

long buttonTimer = 0;
long longPressTime = 6000;
long recycleStartTime = 0;



boolean buttonActive = false;
boolean longPressActive = false;

OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


//==========MQTT BEGIN===========
boolean m_pump_state = false;
WiFiClient wifiClient;
PubSubClient client(wifiClient);

void publishPumpState()
{
  if (m_pump_state)
  {
    client.publish(MQTT_PUMP_STATE_TOPIC, PUMP_ON, true);
  }
  else
  {
    client.publish(MQTT_PUMP_STATE_TOPIC, PUMP_OFF, true);
  }
}
////////todo
void setPumpState()
{
  if (m_pump_state)
  {
    //    digitalWrite(LED_PIN, HIGH);
    readTempC();
    if (tempC > tempThreshold) {
      m_pump_state = false;
    }
    Serial.println("MQTT INFO: Turn pump on...");
    startRecycleWater();
  }
  else
  {
    //    digitalWrite(LED_PIN, LOW);
    m_pump_state = false;
    Serial.println("MQTT INFO: Turn pump off...");
    stopRecycle();
  }
}


// function called when a MQTT message arrived
void callback(char *p_topic, byte *p_payload, unsigned int p_length)
{
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++)
  {
    payload.concat((char)p_payload[i]);
  }

  // handle message topic
  if (String(MQTT_PUMP_COMMAND_TOPIC).equals(p_topic))
  {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(PUMP_ON)))
    {
      if (m_pump_state != true)
      {
        m_pump_state = true;
        setPumpState();
        publishPumpState();
      }
    }
    else if (payload.equals(String(PUMP_OFF)))
    {
      if (m_pump_state != false)
      {
        m_pump_state = false;
        setPumpState();
        publishPumpState();
      }
    }
  }
}

void reconnect()
{
  //Serial.println("current millins:" + String(millis()));
  //Serial.println("previous millins:" + String(baseRetryTime));
  long timespan = millis() - baseRetryTime;
  //Serial.println("compare:" + String(timespan) + " with " + String(5000 * retryTimes));
  if (!client.connected() && !giveUpRetryMqtt ) {
    if (retryTimes == 2 || timespan > 5000 * retryTimes) {

      Serial.print("INFO: Attempting MQTT connection...");
      // Attempt to connect
      if (client.connect(MQTT_CLIENT_ID, mqtt_user, mqtt_password))
      {
        Serial.println("INFO: connected");
        // Once connected, publish an announcement...
        publishPumpState();
        // ... and resubscribe
        client.subscribe(MQTT_PUMP_COMMAND_TOPIC);
      }
      else
      {
        Serial.print("ERROR: failed, rc=");
        Serial.print(client.state());

        // debug
        //retryTimes = sq(retryTimes);
        baseRetryTime = millis();
        retryTimes = retryTimes + 1;
        Serial.println("retryTimes" + String(retryTimes));
        if (retryTimes > 1024) {
          giveUpRetryMqtt = true;
        }
        Serial.println("failed to connect mqtt, will retry after " + String(5 * retryTimes) + " seconds");

      }
    }

  }
  else {
    Serial.println("reset retry time");
    retryTimes = 2;
    baseRetryTime = millis();
  }

  //  }
}
void mqttloop() {
  if (isWIFIConnected()) {
    if (!client.connected())
    {
      reconnect();
    }
    client.loop();
    //publishSensorData();
    //publishPumpState();
  }
}

void publishSensorData() {
  readTempC();
  if (client.connected() && tempC != 85.0 && tempC != (-127.0)) {
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["watertemp"] = (String)tempC;
    root.prettyPrintTo(Serial);
    char data[200];
    root.printTo(data, root.measureLength() + 1);
    client.publish(MQTT_PUMP_SENSOR_TOPIC, data);
  }
}
void publishData(){
  publishSensorData();
  publishPumpState();
}
//==========MQTT END===========

void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED); // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);    // set pin to the opposite state
}

void beep(int msBeep, int msSpace, int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(BEEP_PIN, HIGH);
    delay(msBeep);
    digitalWrite(BEEP_PIN, LOW);
    delay(msSpace);
  }
}

//gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

//callback notifying us of the need to save config
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void mountFS() {

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin())
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json"))
    {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
        }
        else
        {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  //end read
}

// initailize for SPIFFS and wifi config manager
void configPortal()
{
  // put your setup code here, to run once:

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  mountFS();
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("cold_water_recycle_system"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();
  beep(200, 200, 2);
  //keep LED on
  digitalWrite(BUILTIN_LED, LOW);

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig)
  {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}

bool isWaterFlow()
{
  sensorValue = analogRead(sensorPin);
  Serial.println("water flow is:");
  Serial.println(sensorValue);
  return sensorValue > 1000;
}

void readTempC()
{
  do
  {
    sensors.requestTemperatures(); // Send the command to get temperatures
    tempC = sensors.getTempCByIndex(0);
    Serial.println("the tempreture is:");
    Serial.println(tempC);
  } while (tempC == 85.0 || tempC == (-127.0));
}

void startRecycle()
{
  beep(200, 0, 1);
  digitalWrite(RADIO_TUBE_PIN, LOW);
  //  delay(1500);
  digitalWrite(WATER_PUMP_PIN, LOW);
  isRecycling = true;
  Serial.println("start recycling!");
  if (client.connected()) {
    publishPumpState();
  }
}

void stopRecycle()
{
  if (isRecycling) {
    beep(200, 200, 2);
    digitalWrite(WATER_PUMP_PIN, HIGH);
    //  delay(1500);
    digitalWrite(RADIO_TUBE_PIN, HIGH);
    isRecycling = false;
    Serial.println("stop recycle!");

  }
  //if (client.connected()) {
    publishPumpState();
  //}
}



void otaConfig()
{
  //=====================OTA start====================================
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  //=====================OTA end====================================
}

void startRecycleWater()
{
  readTempC();
  if (tempC < tempThreshold)
  {
    Serial.println("start recycle cold water...");

    if (!isRecycling)
    {
      m_pump_state = true;
      startRecycle();
    }
  }
  else
  {
    // if tempreture is good, set the flag to false
    m_pump_state = false;
    if (isRecycling)
    {
      stopRecycle();
    }
    Serial.println("the tempreture is good enough! will not start recycle.");
  }

  //if (client.connected()) {
  //  publishPumpState();
  //}
}

void manualButtonListener() {

  if (digitalRead(BTN_RESET) == LOW)
  {

    if (buttonActive == false)
    {
      buttonActive = true;
      buttonTimer = millis();
    }
    if ((millis() - buttonTimer > longPressTime) && (longPressActive == false))
    {
      longPressActive = true;

      WiFiManager wifiManager;
      wifiManager.resetSettings();
      Serial.println("reseting to factory...0");
      beep(400, 400, 3);
      //      ESP.reset();
      configPortal();
    }
  }
  else
  {
    if (buttonActive == true)
    {
      if (longPressActive == true)
      {
        longPressActive = false;
      }
      else
      {
        Serial.println("trigger manually");

        if (m_pump_state == false) {
          m_pump_state = true;
          startRecycleWater();
        }
        else {
          m_pump_state = false;
          stopRecycle();
        }

        buttonActive = false;
      }
    }
  }
}
void startMain() {

  Serial.println("m_pump_state:" + String(m_pump_state));
  Serial.println("isRecycling:" + String(isRecycling));

  flowTriggerListener();

  mqttloop();

  if(isRecycling){
    publishSensorData();
  }
  
  if (tempC > tempThreshold && isRecycling) {
    Serial.println("water stoped");
    m_pump_state = false;
    stopRecycle();
  }

  checkLongRunException();

  //set signal led status
  if (isWIFIConnected() && client.connected())
  {
    digitalWrite(BUILTIN_LED, LOW);
    ticker.detach();
  }
}

void flowTriggerListener() {
  if (isWaterFlow())
  {
    delay(300);
    if (isWaterFlow()) {
      Serial.println("water flowing...");
      m_pump_state = true;
      startRecycleWater();
    }
  }
}
bool isWIFIConnected() {
  //WiFi.status() == WL_CONNECTED not work
  return WiFi.status() == WL_CONNECTED;//(WiFi.localIP().toString() != "0.0.0.0");
}

void checkLongRunException(){
  if(isRecycling){
    if(recycleStartTime<1){
      recycleStartTime = millis();
    }
    else{
      // if keep running more than 5 minites, stop recycle
      if(millis() - recycleStartTime > (5 * 60000)){
        Serial.println("Warning! keep running to long time to stop the recycle!!");
        m_pump_state = false;
        stopRecycle();
      }
    }
    
  }
  else{
    recycleStartTime = 0;
  }
  
  
}

void setup(void)
{
  // start serial port
  Serial.begin(115200);

  //set led pin as output
  pinMode(STATUS_LED, OUTPUT);

  otaConfig();

  mountFS();

  Serial.println("####### Cold Water Recycle System #######");
  pinMode(RADIO_TUBE_PIN, OUTPUT);
  digitalWrite(RADIO_TUBE_PIN, HIGH);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, HIGH);
  pinMode(BEEP_PIN, OUTPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);

  // Start up the library
  sensors.begin();
  // system start signal
  beep(600, 0, 1);

  m_pump_state = false;

  // init the MQTT connection
  // TODO: parameterlize the port
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  ticker.attach(1, tick);

  timer.setInterval(1000L, startMain);
  timer.setInterval(100L, manualButtonListener);
  timer.setInterval(120000L, publishData);//2 mins
  


}

void loop(void)
{
  timer.run(); // SimpleTimer is working

  ArduinoOTA.handle();
}
