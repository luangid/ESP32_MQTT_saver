#include <Arduino.h>
// internet conections
#include "WiFi.h"
#include <include/arduino-mqtt-master/src/MQTTClient.h>
#include <WiFiClientSecure.h>
// Time library
#include <time.h>
// SD packeges
#include "SD.h"
// Multi task library
#include "FreeRTOS.h"
// disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
// My files
#include "secrets.h"

#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define wf WiFi

const int writeBufSize = 1024 * 22;
const int readBufSize = 1024 * 22;

const char *ESP32_SAVER_PUBLISH_FILE = "app/file";
const char *ESP32_SAVER_PUBLISH_IMG = "app/img";

const char *ESP32_MESSAGE_SUBSCRIBE_TOPIC = "saver/msg";
const char *ESP32_SAVER_SUBSCRIBE_TOPIC = "saver/detect/pic";

const char *FLAG_SEND_DIR_TREE = "file";

bool keepRuning = true;

const int timezone = -3;
const byte daysavetime = 2;

// TaskHandle_t taskWiFi;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient mClient = MQTTClient(128, writeBufSize, readBufSize, false);

struct Date
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
};

Date getDate()
{
    struct tm tmstruct;
    tmstruct.tm_year = 0;
    getLocalTime(&tmstruct, 5000);
    Date date;
    date.year = (tmstruct.tm_year) - 100;
    date.month = (tmstruct.tm_mon) + 1;
    date.day = tmstruct.tm_mday;
    date.hour = tmstruct.tm_hour;
    date.min = tmstruct.tm_min;
    date.sec = tmstruct.tm_sec;
    return date;
}
String createPath()
{
    // 20 charactheres
    Date date = getDate();
    String path = "/i/";
    path += date.day < 10 ? "0" + String(date.day) : String(date.day);
    path += date.month < 10 ? "0" + String(date.month) : String(date.month);
    path += String(date.year);
    path += "/i";
    path += date.hour < 10 ? "0" + String(date.hour) : String(date.hour);
    path += date.min < 10 ? "0" + String(date.min) : String(date.min);
    path += date.sec < 10 ? "0" + String(date.sec) : String(date.sec);
    path += ".jpg";
    // const char *cPath =
    return path;
}

void createJsonDirTree(const char *mDir, bool isFile)
{
    File root = SD.open(mDir);
    File folder = root.openNextFile();
    size_t len;
    int add = isFile ? 11 : 3;
    debugln(add);
    String json = "{\"files\":[\"";
    bool firstFolder = true;
    while (folder)
    {
        if (!firstFolder) // Outros dir
        {
            json += "\",\"";
            json += (folder.name() + add);
        }
        else // Primeiro dir
        {
            json += (folder.name() + add);
            firstFolder = false;
        }
        if (json.length() >= 2036)
        {
            len = json.length();
            debugln(json.length());
            mClient.publish(ESP32_SAVER_PUBLISH_FILE, (uint8_t *)json.c_str(), len, false, 0);
            json = "";
        }
        folder = root.openNextFile();
    }
    json += "\"]}";
    len = json.length();
    bool res = mClient.publish(ESP32_SAVER_PUBLISH_FILE, (uint8_t *)json.c_str(), len, false, 0);
    if (!res)
        ESP.restart();
    debugln(res ? len : 0);
    debugln("publish fim");
    root.close();
    folder.close();
}
void sendFIle(const char *path)
{
    File file = SD.open(path);
    if (mClient.publishFile(ESP32_SAVER_PUBLISH_IMG, file, false, 0))
    {
        debug("img sent\n");
    }
    else
    {
        debug("fail img sent\n");
    }
    file.close();
    keepRuning = true;
}

void messageHandler(MQTTClient *client, char *topic, char *bytes, int length)
{
    keepRuning = false;
    debugln(length);
    if (String(topic).equals(ESP32_SAVER_SUBSCRIBE_TOPIC) || length > 512)
    {
        fs::FS &fs = SD;
        String path = createPath();
        String r = path.substring(0, 9) + "";
        const char *rPath = r.c_str();
        const char *cPath = path.c_str();
        // String path = "/dir/file.jpg";
        // path.substring(0, 12).c_str()
        if (!fs.mkdir(rPath))
        {
            debugln("mkdir fail");
        }
        File file = fs.open(cPath, FILE_WRITE);
        if (!file)
        {
            debugln("Fail to open file");
        }
        else if (!file.write((uint8_t *)bytes, length))
        {
            debugln("Fail to write file");
        }
        file.close();
    }
    else
    {
        String msg = String(bytes);
        if (length == 4)
        {
            msg.c_str();
            createJsonDirTree("/i", false);
        }
        else if (msg.substring(0, 3).equals("/i/"))
        {
            debugln(msg);
            if (msg.length() == 22)
            {
                sendFIle(msg.c_str());
            }
            else
            {
                createJsonDirTree(msg.c_str(), true);
            }
        }
    }

    keepRuning = true;
}

void beginWiFi()
{
    wf.disconnect();
    wf.persistent(false);
    wf.mode(WIFI_STA);
    wf.begin(WL_SSID, WL_PASSWORD);
    // wf.begin("TP-Link_8263", "15237033");
    //  wf.begin("TEMPERO_DO_POLEN_COMFIBRA", "temperos");
    int countTries = 0;
    while (wf.status() != WL_CONNECTED)
    {
        delay(500);
        debug(".");
        countTries++;
        if (countTries > 20)
            break;
    }
    if (countTries > 20)
    {
        debug("try againgn\n");
        wf.disconnect();
        wf.begin(WL_SSID2, WL_PASSWORD2);
        countTries = 0;
        while (wf.status() != WL_CONNECTED)
        {
            delay(500);
            debug(".");
            countTries++;
            if (countTries > 20)
                ESP.restart();
        }
    }
    debugln("WL CONNECTED");
    // Configure wfClientSecure to use the AWS IoT device credentials
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    mClient.begin(AWS_IOT_ENDPOINT, 8883, net);
    mClient.setCleanSession(true);
    mClient.onMessageAdvanced(messageHandler);

    debugln("Connecting to AWS");

    while (!mClient.connect(THINGNAME))
    {
        debug(".");
        delay(100);
    }

    if (!mClient.connected())
    {
        debugln("Timeout!");
        ESP.restart();
        return;
    }
    // Topico de recebimento de mensagens
    mClient.subscribe(ESP32_SAVER_SUBSCRIBE_TOPIC);
    mClient.subscribe(ESP32_MESSAGE_SUBSCRIBE_TOPIC);
}

void initSD()
{
    Serial.begin(115200);
    if (!SD.begin())
    {
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return;
    }
    debugln(SD.cardSize() / (1024 * 1024));
    configTime(3600 * timezone, daysavetime * 3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
    delay(1000);
}
void setup()
{
    Serial.begin(115200);
    beginWiFi();
    initSD();
    debugln("setup done");
    // xTaskCreatePinnedToCore(wifiTask, "WiFi_task", 10000, NULL, 1, &taskWiFi, 0);
}
void loop()
{
    if (keepRuning)
        mClient.loop();
}
