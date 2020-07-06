#include <FS.h>
#include <ThingsBoard.h>

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> //https://github.com/tzapu/WiFiManager
                                 // #include "SoftwareSerial.h"

// See https://thingsboard.io/docs/getting-started-guides/helloworld/
// to understand how to obtain an access token

// // Baud rate for serial debug
// #define SERIAL_DEBUG_BAUD 9600
// // Baud rate for communicating with ESP chip
// #define SERIAL_ESP8266_BAUD 9600

// Serial driver for ESP
// SoftwareSerial soft(2, 3); // RX, TX
// Initialize the Ethernet client object
// WiFiEspClient espClient;
// Initialize ThingsBoard instance
#define VALUE_KEY "value"
#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))
#define SWITCH_PIN 5

class CustomLogger {
public:
  static void log(const char *msg) {
    Serial.print("[Serial Logger] ");
    Serial.println(msg);
  }
};

WiFiClient wifiClient;
ThingsBoardSized<128, 32, CustomLogger> tb(wifiClient);

AsyncWebServer server(80);
DNSServer dns;
bool subscribed = false;
bool lastValue = false;

RPC_Response setValue(const RPC_Data &data)
{
  bool enabled = data[VALUE_KEY];
  lastValue = enabled;
  Serial.println("Received rpc request,");
  Serial.print("value:");
  Serial.print(enabled);
  digitalWrite(SWITCH_PIN, enabled);
  return RPC_Response(VALUE_KEY, lastValue);
}

RPC_Response getValue(const RPC_Data &data)
{
  return RPC_Response(VALUE_KEY, lastValue);
}

RPC_Callback callbacks[] = {
    {"setValue", setValue},
    {"getValue", getValue},
};

void subscribeToRPC()
{
  // Subscribe for RPC, if needed
  if (!subscribed)
  {
    Serial.println("Subscribing for RPC...");

    // Perform a subscription. All consequent data processing will happen in
    // callbacks as denoted by callbacks[] array.
    if (!tb.RPC_Subscribe(callbacks, COUNT_OF(callbacks)))
    {
      Serial.println("Failed to subscribe for RPC");
      return;
    }

    Serial.println("Subscribed with ");
    Serial.print(COUNT_OF(callbacks));
    Serial.print(" callbacks");

    Serial.println("Subscribe done");
    subscribed = true;
  }
}

char tb_server[100];
char tb_token[34] = "YOUR_DEVICE_TOKEN";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup()
{

  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  pinMode(SWITCH_PIN, OUTPUT);

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
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());

        serializeJson(json, Serial);
        if (error)
        {
          Serial.println("failed to load json config");
        }
        else
        {
          Serial.println("\nparsed json");

          strcpy(tb_server, json["tb_server"]);
          strcpy(tb_token, json["tb_token"]);
        }
      }
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  AsyncWiFiManagerParameter custom_tb_server("server", "Thingsboard server", tb_server, 100);
  AsyncWiFiManagerParameter custom_tb_token("token", "Access Token", tb_token, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server, &dns);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  // wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_tb_server);
  wifiManager.addParameter(&custom_tb_token);

  //reset settings - for testing
  // wifiManager.resetSettings();

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
  if (!wifiManager.autoConnect("Motor-Relay-01"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(tb_server, custom_tb_server.getValue());
  strcpy(tb_token, custom_tb_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig)
  {
    Serial.println("saving config");
    // DynamicJsonBuffer jsonBuffer;
    //  jsonBuffer.createObject();
    DynamicJsonDocument json(1024);
    json["tb_server"] = tb_server;
    json["tb_token"] = tb_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    // json.printTo(Serial);
    // json.printTo(configFile);
    serializeJson(json, configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}

void loop()
{
  delay(1000);

  if (!tb.connected())
  {
    // Connect to the ThingsBoard
    Serial.print("Connecting to: ");
    Serial.print(tb_server);
    Serial.print(" with token ");
    Serial.println(tb_token);
    if (!tb.connect(tb_server, tb_token))
    {
      Serial.println("Failed to connect, retrying ...");
      return;
    }
  }

  subscribeToRPC();

  Serial.println("Sending telemetry data...");

  const int data_items = 1;
  Telemetry data[data_items] = {
      {"value", false}
  };

  // // Uploads new telemetry to ThingsBoard using MQTT.
  // // See https://thingsboard.io/docs/reference/mqtt-api/#telemetry-upload-api
  // // for more details
  tb.sendTelemetry(data, data_items);

  Serial.println("Sending attributes data...");

  const int attribute_items = 2;
  Attribute attributes[attribute_items] = {
      {"device_type", "switch"},
      {"active", true},
  };

  // Publish attribute update to ThingsBoard using MQTT.
  // See https://thingsboard.io/docs/reference/mqtt-api/#publish-attribute-update-to-the-server
  // for more details
  tb.sendAttributes(attributes, attribute_items);
  tb.loop();
}