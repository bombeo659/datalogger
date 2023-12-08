// #include "EthernetLarge.h"
#include <Ethernet2.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"
#include <SD.h>
#include <Arduino.h>

TaskHandle_t CheckConnectionTask;
TaskHandle_t CallLoopFunction;
SemaphoreHandle_t mutex;

const char *ssid = "QuocTrong";
const char *password = "Bachkhoa";
const char *mqtt_server_gateway = "172.31.255.254";
const char *mqtt_server_vps = "167.172.81.240";

byte MAC[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
IPAddress IP(172, 31, 255, 253);
IPAddress DNS_SERVER(172, 31, 255, 1);
IPAddress GATEWAY(172, 31, 255, 1);
IPAddress SUBNET(255, 255, 255, 252);

EthernetClient ethClient;
WiFiClient wifiClient;
PubSubClient mqttClientLocal(ethClient);
PubSubClient mqttClientVPS(wifiClient);

#define ONE_BIT_MODE true

const char *latestFiledMessage = nullptr;
size_t lengthOfMessage = 0;
const char *topicVPSClient = "eui-a84041446184392f/up";
const char *loggerFileName = "/datalogger.csv";

void readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while (file.available())
    {
        Serial.write(file.read());
    }
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("File written");
    }
    else
    {
        Serial.println("Write failed");
    }
}

void appendFile(fs::FS &fs, const char *path, const char *message, bool isConnected)
{
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.println("Failed to open file for appending");
        return;
    }
    if (isConnected)
    {
        if (file.print("true;") && file.println(message))
        {
            Serial.println("Message appended with connected server");
        }
        else
        {
            Serial.println("Append failed");
        }
    }
    else
    {
        if (file.print("false;") && file.println(message))
        {
            Serial.println("Message appended without connected server");
            latestFiledMessage = strdup(message);
        }
        else
        {
            Serial.println("Append failed");
        }
    }
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2))
    {
        Serial.println("File renamed");
    }
    else
    {
        Serial.println("Rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\n", path);
    if (fs.remove(path))
    {
        Serial.println("File deleted");
    }
    else
    {
        Serial.println("Delete failed");
    }
}

const char *bytesToHex(const byte *input, size_t length)
{
    static char hexChars[] = "0123456789ABCDEF";
    char *output = new char[length * 2 + 1]; // Mỗi byte tương ứng với 2 ký tự hex + 1 ký tự null

    for (size_t i = 0; i < length; ++i)
    {
        output[i * 2] = hexChars[input[i] >> 4];
        output[i * 2 + 1] = hexChars[input[i] & 0x0F];
    }

    output[length * 2] = '\0'; // Ký tự null kết thúc chuỗi

    return output;
}

byte hexCharToByte(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'A' && c <= 'F')
    {
        return 10 + c - 'A';
    }
    else if (c >= 'a' && c <= 'f')
    {
        return 10 + c - 'a';
    }
    return 0; // Trả về 0 cho các ký tự không hợp lệ
}

byte *hexToBytes(const char *message, size_t length)
{
    if (length % 2 != 0)
    {
        // Chiều dài của chuỗi hex cần là số chẵn
        return nullptr;
    }

    size_t byteLength = length / 2;
    byte *output = new byte[byteLength];

    for (size_t i = 0; i < byteLength; ++i)
    {
        output[i] = (hexCharToByte(message[i * 2]) << 4) | hexCharToByte(message[i * 2 + 1]);
    }

    return output;
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Received message on topic: ");
    Serial.println(topic);

    lengthOfMessage = length;
    const char *message = bytesToHex(payload, length);

    // Copy the payload to the new buffer
    byte *p = (byte *)malloc(length);
    memcpy(p, payload, length);

    if (mqttClientVPS.publish(topicVPSClient, p, length, true))
    {
        Serial.println("Message sent successfully!");
        appendFile(SD_MMC, loggerFileName, message, true);
    }
    else
    {
        Serial.println("Failed to send message!");
        appendFile(SD_MMC, loggerFileName, message, false);
    }
    // Free the memory
    delete[] message;
    free(p);
}

void connectToWiFi()
{
    if (xSemaphoreTake(mutex, portMAX_DELAY))
    {
        Serial.println("Connecting to WiFi...");
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED)
        {
            delay(1000);
            Serial.println("Connecting to WiFi...");
        }
        Serial.print("Connected to WiFi with IP: ");
        Serial.println(WiFi.localIP());
        // Giải phóng mutex
        xSemaphoreGive(mutex);
    }
}

void connectToMQTTClientLocal()
{
    // connect to mqtt server gateway
    mqttClientLocal.setServer(mqtt_server_gateway, 1883);
    //   mqttClientLocal.setCallback(callback);

    while (!mqttClientLocal.connected())
    {
        Serial.println("Connecting to MQTT broker Gateway...");
        if (mqttClientLocal.connect("ESP32Client", "datalogger", "NNSXS.5YVJMIEDAH6OB2UMTGH3KITRMADP4XD6H7RYOZQ.6JQEXQND4WEROYR24CBEGSKZ5LPLDZMUVNTHGBX2YF74F3CVRV4A"))
        {
            Serial.println("Connected to MQTT broker Gateway");
            mqttClientLocal.setCallback(callback);
            mqttClientLocal.subscribe("v3/datalogger/devices/eui-a84041446184392f/up");
            mqttClientLocal.setBufferSize(65535);
            // Serial.println(client.getBufferSize());
        }
        else
        {
            Serial.print("Failed to connect to MQTT broker, rc=");
            Serial.println(mqttClientLocal.state());
            delay(2000);
        }
    }
}

void connectToMQTTClientVPS()
{
    mqttClientVPS.setServer(mqtt_server_vps, 1883);
    //   mqttClientVPS.setCallback(callback);

    while (!mqttClientVPS.connected())
    {
        Serial.println("Connecting to MQTT broker VPS...");
        if (mqttClientVPS.connect("ESP32Client", "nqt", "nqt"))
        {
            Serial.println("Connected to MQTT broker VPS");
            //   mqttClientVPS.setCallback(callback);
            mqttClientVPS.subscribe("eui-a84041446184392f/up");
            mqttClientVPS.setBufferSize(65535);
            // Serial.println(client.getBufferSize());
        }
        else
        {
            Serial.print("Failed to connect to MQTT broker, rc=");
            Serial.println(mqttClientVPS.state());
            delay(2000);
        }
    }
}

bool checkWifiConnection()
{
    if (xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return WiFi.status() != WL_CONNECTED;
        // Giải phóng mutex
        xSemaphoreGive(mutex);
    }
    return false;
}

bool checkVPSServerConnection()
{
    if (xSemaphoreTake(mutex, portMAX_DELAY))
    {
        return mqttClientVPS.connected();
        // Giải phóng mutex
        xSemaphoreGive(mutex);
    }
    return false;
}

void reconnectMQTTClientLocal()
{
    Serial.println("Attempting MQTT Client Local reconnection...");

    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWiFi();
    }

    connectToMQTTClientLocal();
}

void connectMQTTClientVPS(void *parameter)
{
    for (;;)
    {
        if (!mqttClientVPS.connected())
        {
            Serial.println("MQTT VPS client not connected. Reconnecting...");
            if (WiFi.status() != WL_CONNECTED)
            {
                connectToWiFi();
            }

            connectToMQTTClientVPS();
            if (!(latestFiledMessage == nullptr) || !strlen(latestFiledMessage) == 0)
            {
                size_t hexLength = strlen(latestFiledMessage);
                // Serial.println(hexLength);

                byte *byteArray = hexToBytes(latestFiledMessage, hexLength);
                if (byteArray != nullptr)
                {
                    if (mqttClientVPS.publish(topicVPSClient, byteArray, lengthOfMessage, true))
                    {
                        Serial.println("Latest failed message sent successfully!");
                    }
                    else
                    {
                        Serial.println("Failed to send message!");
                    }
                    delete[] latestFiledMessage;
                }
                delete[] byteArray;
            }
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void connectToSDCard()
{
    pinMode(2, INPUT_PULLUP);
    pinMode(4, INPUT_PULLUP);
    pinMode(15, INPUT_PULLUP);
    pinMode(13, INPUT_PULLUP);
    // Serial.println("delay for connect pin12");
    // delay(5000);
    // pinMode(12, INPUT_PULLUP);

    //    SD_MMC.begin("/root", true, false, SDMMC_FREQ_DEFAULT)
    if (!SD_MMC.begin("/sdcard", ONE_BIT_MODE, false, SDMMC_FREQ_DEFAULT))
    {
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD_MMC.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD_MMC card attached");
        return;
    }

    Serial.print("SD_MMC Card Type: ");
    if (cardType == CARD_MMC)
    {
        Serial.println("MMC");
    }
    else if (cardType == CARD_SD)
    {
        Serial.println("SDSC");
    }
    else if (cardType == CARD_SDHC)
    {
        Serial.println("SDHC");
    }
    else
    {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
    // writeFile(SD_MMC, loggerFileName, "isConnected,payload\n");
}

void callLoopFunction(void *parameter)
{
    for (;;)
    {
        mqttClientLocal.loop();
        mqttClientVPS.loop();
        // Check MQTT connection every 5 seconds
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);

    mutex = xSemaphoreCreateMutex();

    // init ethernet for W5500
    Serial.print("Init ethernet for W5500 ...\n");
    Ethernet.init(5);
    Ethernet.begin(MAC, IP, DNS_SERVER, GATEWAY, SUBNET);
    Serial.print("IP of W5500 is: ");
    Serial.println(Ethernet.localIP());

    connectToMQTTClientLocal();
    connectToSDCard();
    connectToWiFi();
    connectToMQTTClientVPS();

    // if (!mqttClientLocal.connected())
    // {
    //     reconnectMQTTClientLocal();
    // }

    xTaskCreatePinnedToCore(
        connectMQTTClientVPS, // Hàm thực thi của task kiểm tra kết nối
        "CheckConnTask",      // Tên task
        10000,                // Kích thước ngăn xếp
        NULL,                 // Tham số truyền vào hàm
        1,                    // Ưu tiên
        &CheckConnectionTask, // Task handle
        1                     // Core để chạy task (Core 1)
    );

    xTaskCreatePinnedToCore(
        callLoopFunction,  // Hàm thực thi của task kiểm tra kết nối
        "CallLoopFunc",    // Tên task
        10000,             // Kích thước ngăn xếp
        NULL,              // Tham số truyền vào hàm
        1,                 // Ưu tiên
        &CallLoopFunction, // Task handle
        0                  // Core để chạy task (Core 1)
    );

    // connect to wifi
    // WiFi.begin(ssid, password);
    // while (WiFi.status() != WL_CONNECTED)
    // {
    //     delay(1000);
    //     Serial.println("Connecting to WiFi...");
    // }
    // Serial.print("Connected to WiFi with IP: ");
    // Serial.println(WiFi.localIP());

    // connect to mqtt server gateway
    // mqttClientLocal.setServer(mqtt_server_gateway, 1883);
    //   mqttClientLocal.setCallback(callback);

    // while (!mqttClientLocal.connected())
    // {
    //     Serial.println("Connecting to MQTT broker Gateway...");
    //     if (mqttClientLocal.connect("ESP32Client", "datalogger", "NNSXS.5YVJMIEDAH6OB2UMTGH3KITRMADP4XD6H7RYOZQ.6JQEXQND4WEROYR24CBEGSKZ5LPLDZMUVNTHGBX2YF74F3CVRV4A"))
    //     {
    //         Serial.println("Connected to MQTT broker Gateway");
    //         mqttClientLocal.setCallback(callback);
    //         mqttClientLocal.subscribe("v3/datalogger/devices/eui-a84041446184392f/up");
    //         mqttClientLocal.setBufferSize(65535);
    //         // Serial.println(client.getBufferSize());
    //     }
    //     else
    //     {
    //         Serial.print("Failed to connect to MQTT broker, rc=");
    //         Serial.println(mqttClientLocal.state());
    //         delay(2000);
    //     }
    // }

    // // connect to mqtt server gateway
    // mqttClientVPS.setServer(mqtt_server_vps, 1883);
    // //   mqttClientVPS.setCallback(callback);

    // while (!mqttClientVPS.connected())
    // {
    //     Serial.println("Connecting to MQTT broker VPS...");
    //     if (mqttClientVPS.connect("ESP32Client", "nqt", "nqt"))
    //     {
    //         Serial.println("Connected to MQTT broker VPS");
    //         //   mqttClientVPS.setCallback(callback);
    //         mqttClientVPS.subscribe("eui-a84041446184392f/up");
    //         mqttClientVPS.setBufferSize(65535);
    //         // Serial.println(client.getBufferSize());
    //     }
    //     else
    //     {
    //         Serial.print("Failed to connect to MQTT broker, rc=");
    //         Serial.println(mqttClientVPS.state());
    //         delay(2000);
    //     }
    // }

    // connect to sd card
}

void loop()
{
    // mqttClientLocal.loop();
    //     mqttClientVPS.loop();
}
