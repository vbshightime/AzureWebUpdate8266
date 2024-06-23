#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <FS.h> 

#include <AzureIoTHub.h>
#include <AzureIoTProtocol_MQTT.h>
#include <AzureIoTUtility.h>
#include <ESP8266httpUpdate.h>

#define DEVICE_ID "ncdMSdemo1"

#define MESSAGE_MAX_LEN 256

#define MESSAGE_LEN 128

static WiFiClientSecure sslClient; // for ESP8266

static int interval = 2000;
bool temperatureAlert= false;
const char* ssid     = "AgNext_wifi";
const char* password = "hakunamatata#@!";

// Data upload timer will run for the interval of 20s
unsigned long msg_Interval = 30000;
unsigned long msg_Timer = 0;

String debugLogData;
/*String containing Hostname, Device Id & Device Key in the format:                         */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessKey=<device_key>"                */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessSignature=<device_sas_token>"    */
static  char *connectionString = "HostName=**************************t;DeviceId=TestDeviceA1;SharedAccessKey=***************************/g=";

//Create a char variable to store our JSON format
const char *messageData = "{\"deviceData\":\"%s\"}";
const char *reportedData = "{\"deviceId\":\"%s\",\"Random\":%d}";

String host = "********************************";
String bin="/yawncontainer/espServer.ino.nodemcu.bin?sp=r\"&\"st=2019-04-13T02:59:06Z\"&\"se=2019-04-13T10:59:06Z\"&\"spr=https\"&\"sv=2018-03-28\"&\"sig=KPywl76LMnF%2FoYbTXMPzghzDmsIbDuuiGDxulCpvkxI%3D\"&\"sr=b";
const char *onSuccess = "\"Successfully invoke device method\"";
const char *notFound = "\"No method found\"";

static bool messagePending = false;
static bool messageSending = true;


static int messageCount = 0;

/*void blinkLED()
{
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
}*/

void initWifi()
{
    // Attempt to connect to Wifi network:
    Serial.printf("Attempting to connect to SSID: %s.\r\n", ssid);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        // Get Mac Address and show it.
        // WiFi.macAddress(mac) save the mac address into a six length array, but the endian may be different. The huzzah board should
        // start from mac[0] to mac[5], but some other kinds of board run in the oppsite direction.
        uint8_t mac[6];
        WiFi.macAddress(mac);
        Serial.printf("You device with MAC address %02x:%02x:%02x:%02x:%02x:%02x connects to %s \n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ssid);
        delay(500);
    }
    Serial.printf("Connected to wifi %s.\r\n", ssid);
}

void initTime()
{
    time_t epochTime;
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    while (true)
    {
        epochTime = time(NULL);

        if (epochTime == 0)
        {
            Serial.println("Fetching NTP epoch time failed! Waiting 2 seconds to retry.");
            delay(2000);
        }
        else
        {
            Serial.printf("Fetched NTP epoch time is: %lu.\r\n", epochTime);
            break;
        }
    }
}

static IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;
void setup()
{
    //pinMode(LED_PIN, OUTPUT);

    Serial.begin(115200);
    Serial.setDebugOutput(true);
     while(!Serial);
    WiFi.persistent(false);
    WiFi.disconnect(true);
    Serial.setDebugOutput(true);
    SPIFFS.begin();
    delay(100);  
    File file = SPIFFS.open("/ota.txt", "r");     
    Serial.println("- read from file:");
     if(!file){
        Serial.println("- failed to open file for reading");
        return;
    }
    while(file.available()){
        debugLogData += char(file.read());
    }
    file.close(); 
    Serial.println(debugLogData);
       if(debugLogData.length()<3){
        Serial.println("Creating Json");
        StaticJsonBuffer<256> jsonBuffer;
        JsonObject& root =jsonBuffer.createObject();
        root.set("updateCount",1);
        root.set("fwVersion","1.0.0");
        root.printTo(Serial);
        File fileToWrite = SPIFFS.open("/ota.txt", "w");
           if(!fileToWrite){
              Serial.println("Error opening SPIFFS");
              return;
            }
            if(root.printTo(fileToWrite)){
                Serial.println("--File Written");
                fileToWrite.close(); 
            }else{
                Serial.println("--Error Writing File");
              }
              ESP.restart();
    }
    initWifi();
    initTime();

    /*
     * AzureIotHub library remove AzureIoTHubClient class in 1.0.34, so we remove the code below to avoid
     *    compile error
    */

    // initIoThubClient();
    iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol);
    while(iotHubClientHandle == NULL)
    {
        Serial.println("Failed on IoTHubClient_CreateFromConnectionString.");
        delay(2000);
        //while (1);
    }

    IoTHubClient_LL_SetOption(iotHubClientHandle, "product_info", "ESP8266");
    IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, receiveMessageCallback, NULL);
    IoTHubClient_LL_SetDeviceMethodCallback(iotHubClientHandle, deviceMethodCallback, NULL);
    IoTHubClient_LL_SetDeviceTwinCallback(iotHubClientHandle, twinCallback, NULL);
}

void loop()
{
    if (!messagePending && messageSending)
    {
        if(millis()-msg_Timer >= msg_Interval){
        msg_Timer = millis();
        int randomNum = random(0,50);
        if(randomNum> 10){
           temperatureAlert =true;
          }else{
            temperatureAlert =true;
           }
        char messagePayload[MESSAGE_MAX_LEN];
        char reportedPayload[MESSAGE_MAX_LEN];
        snprintf(messagePayload,MESSAGE_MAX_LEN,messageData,DEVICE_ID,messageCount++,randomNum);
        sprintf(reportedPayload,reportedData,DEVICE_ID,randomNum);
        size_t reportedSize = strlen(reportedPayload);
        Serial.println(reportedPayload);
        sendMessage(iotHubClientHandle, messagePayload, temperatureAlert);
        if(randomNum > 40){
            IoTHubClient_LL_SendReportedState(iotHubClientHandle,(const unsigned char*)reportedPayload,reportedSize,deviceReportCallback,NULL);
          
          }
     }
    }
    IoTHubClient_LL_DoWork(iotHubClientHandle);
    delay(10);
}


static void sendCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback)
{
    if (IOTHUB_CLIENT_CONFIRMATION_OK == result)
    {
        Serial.println("Message sent to Azure IoT Hub");
//        blinkLED();
    }
    else
    {
        Serial.println("Failed to send message to Azure IoT Hub");
    }
    messagePending = false;
}

static void sendMessage(IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle, char *buffer, bool temperatureAlert)
{
    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char *)buffer, strlen(buffer));
    if (messageHandle == NULL)
    {
        Serial.println("Unable to create a new IoTHubMessage.");
    }
    else
    {
        MAP_HANDLE properties = IoTHubMessage_Properties(messageHandle);
        Map_Add(properties, "temperatureAlert", temperatureAlert ? "true" : "false");
        Serial.printf("Sending message: %s.\r\n", buffer);
        if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, messageHandle, sendCallback, NULL) != IOTHUB_CLIENT_OK)
        {
            Serial.println("Failed to hand over the message to IoTHubClient.");
        }
        else
        {
            messagePending = true;
            Serial.println("IoTHubClient accepted the message for delivery.");
        }

        IoTHubMessage_Destroy(messageHandle);
    }
}


IOTHUBMESSAGE_DISPOSITION_RESULT receiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void *userContextCallback)
{
    IOTHUBMESSAGE_DISPOSITION_RESULT result;
    const unsigned char *buffer;
    size_t size;
    if (IoTHubMessage_GetByteArray(message, &buffer, &size) != IOTHUB_MESSAGE_OK)
    {
        Serial.println("Unable to IoTHubMessage_GetByteArray.");
        result = IOTHUBMESSAGE_REJECTED;
    }
    else
    {
        /*buffer is not zero terminated*/
        char *temp = (char *)malloc(size + 1);

        if (temp == NULL)
        {
            return IOTHUBMESSAGE_ABANDONED;
        }

        strncpy(temp, (const char *)buffer, size);
        temp[size] = '\0';
        Serial.printf("Receive C2D message: %s.\r\n", temp);
        free(temp);
//        blinkLED();
    }
    return IOTHUBMESSAGE_ACCEPTED;
}

int deviceMethodCallback(
    const char *methodName,
    const unsigned char *payload,
    size_t size,
    unsigned char **response,
    size_t *response_size,
    void *userContextCallback)
{
    Serial.printf("Try to invoke method %s.\r\n", methodName);
    const char *responseMessage = onSuccess;
    int result = 200;

    if (strcmp(methodName, "start") == 0)
    {
      Serial.println("Start sending data.");
      messageSending = true;
    }
    else if (strcmp(methodName, "stop") == 0)
    {
      Serial.println("Stop sending data");
      messageSending = false;
    }
    else
    {
        Serial.printf("No method %s found.\r\n", methodName);
        responseMessage = notFound;
        result = 404;
    }

    *response_size = strlen(responseMessage);
    *response = (unsigned char *)malloc(*response_size);
    strncpy((char *)(*response), responseMessage, *response_size);

    return result;
}

void twinCallback(DEVICE_TWIN_UPDATE_STATE updateState,const unsigned char *payLoad,size_t size,void *userContextCallback)
{   
     StaticJsonBuffer<200> jsonBuffer; 
    int updateCount;
    String fileLog;
    char *mem = (char *)malloc(size + 1);
    for (int i = 0; i < size; i++)
    {
      mem[i] = (char)(payLoad[i]);
    }
    mem[size] = '\0';
    //char messageId[MESSAGE_LEN];
    //parseTwinMessage(temp);
    Serial.println("device Twins updated with");
    Serial.printf("%s",mem);
     JsonObject& readRoot = jsonBuffer.parseObject(mem);
    if(!readRoot.success()){
        return;
      }
      if(readRoot.containsKey("firmware")){
          Serial.println("foundFirmware");
          updateCount = readRoot["firmware"]["firmwareUpdateCount"];
       //   fwVersion = readRoot["firmwareVersion"];
        }
        jsonBuffer.clear();
        File file = SPIFFS.open("/ota.txt", "r");     
        if(!file){
        Serial.println("- failed to open file for reading");
        return;
    }
    while(file.available()){
        fileLog += char(file.read());
    } 
    Serial.println("- read from file:");
    Serial.println(fileLog);
    Serial.println(updateCount);
    file.close();
    free(mem);
    if(fileLog.length()>0){
      StaticJsonBuffer<200> jsonBuffer1; 
      JsonObject& readRootSpiff = jsonBuffer1.parseObject(fileLog);
      if(readRootSpiff["updateCount"]<updateCount){
             //char spiffData[MESSAGE_MAX_LEN];
             //snprintf(spiffData,MESSAGE_MAX_LEN,spiffFormat,updateCount);
             jsonBuffer.clear();
             StaticJsonBuffer<200> jsonBuffer2; 
             JsonObject& readRootSpiff1 = jsonBuffer2.createObject();
             readRootSpiff1["updateCount"] = updateCount;
             File file = SPIFFS.open("/ota.txt", "w");     
             if(!file){
                Serial.println("- failed to open file for reading");
                return;
               }
            if (readRootSpiff1.printTo(file)) {
             Serial.println("File was written");
             readRootSpiff1.printTo(Serial);
               } else {
             Serial.println("File write failed");
                }
                file.close();
                fwUpdateStart();
        }
      } 
}


void deviceReportCallback(int status_code, void* userContextCallback){
    (void)userContextCallback;
      Serial.printf("Device Twin reported properties update succeded with result: %d\r\n", status_code);              
      /*if((status_code == IotHubStatusCode.OK) || (status_code == IotHubStatusCode.OK_EMPTY))
            {
              Serial.printf("Device Twin reported properties update succeded with result: %d\r\n", status_code);              
            }else{
                  Serial.printf("Device Twin reported properties update failed with result: %d\r\n", status_code);    
              }*/  
  }
 void fwUpdateStart(){
  t_httpUpdate_return ret = ESPhttpUpdate.update(host,443,bin);
        //t_httpUpdate_return  ret = ESPhttpUpdate.update("https://server/file.bin");

        switch(ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
                break;

            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("HTTP_UPDATE_NO_UPDATES");
                break;

            case HTTP_UPDATE_OK:
                Serial.println("HTTP_UPDATE_OK");
                break;
        }
  }
