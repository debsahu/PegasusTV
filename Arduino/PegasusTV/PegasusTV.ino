#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>         //https://github.com/me-no-dev/ESPAsyncTCP
#include <ESPAsyncWebServer.h>   //https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWiFiManager.h> //https://github.com/alanswx/ESPAsyncWiFiManager
#include <ESPAsyncDNSServer.h>   //https://github.com/devyte/ESPAsyncDNSServer
// #include <DNSServer.h>
#include <Ticker.h>
#include <AsyncMqttClient.h> //https://github.com/marvinroger/async-mqtt-client
//#include "secrets.h"
#include "version.h"

#define HOSTNAME "PegasusTV"
const byte HTTP_PORT = 80;
const byte DNS_PORT = 53;
#define PIN_CTRL D2
#define WIFI_RECONNECT_RETRIES_MAX 10

#ifndef SECRET
char AP_PASS[32] = "";

char MQTT_HOST[64] = "192.168.0.xxx";
char MQTT_PORT[6] = "1883";
char MQTT_USER[32] = "";
char MQTT_PASS[32] = "";
#endif

bool startMDNS = true;
uint16_t wifi_reconnect_interval = 60; // in seconds
uint8_t wifi_reconnect_retries = 0;
char MQTT_PUB_TOPIC[] = "home/" HOSTNAME "/out";
char MQTT_SUB_TOPIC[] = "home/" HOSTNAME "/in";
char NameChipId[64] = {0}, chipId[7] = {0};

AsyncWebServer server(HTTP_PORT);
AsyncDNSServer dns;
// DNSServer dns;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

Ticker wifiReconnectTimer;
Ticker togglePowerPin;

bool shouldReboot = false;

static const char root_html[] PROGMEM = R"=====(
<!DOCTYPE html><html lang="en"><head><style>body{margin:0;height:100vh;display:flex;flex-direction:column;justify-content:center;background-color:#A8D296}.button{background-color:#296B0D;color:#fff;padding:16px 25px;font-size:100px;margin:0 auto;border:none;cursor:pointer;width:100%;border-radius:8px}.button:hover{background-color:green}.button:active{background-color:#8F2A2D}</style></head><body><input type="button" class="button" onclick="location.href=&#34;/togglePower&#34;" value="Toggle Power" style="width:auto"></body></html>
)=====";
static const char update_html[] PROGMEM = R"=====(<!DOCTYPE html><html lang="en"><head><title>Firmware Update</title><meta http-equiv="Content-Type" content="text/html; charset=utf-8"><meta name="viewport" content="width=device-width"><link rel="shortcut icon" type="image/x-icon" href="favicon.ico"></head><body><h3>Update Firmware</h3><br><form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="update"> <input type="submit" value="Update"></form></body></html>)=====";

void connectToWifi()
{
    if(wifi_reconnect_retries > WIFI_RECONNECT_RETRIES_MAX)
    {
        ESP.restart();
    }
    if (!WiFi.isConnected())
    {
        Serial.println("Connecting to Wi-Fi...");
        WiFi.begin();
    }
    else
    {
        Serial.println("WiFi is connected");
    }
    wifi_reconnect_retries++;
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
    Serial.println("Connected to Wi-Fi.");
    startMDNS = true;
    MDNS_init();
    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
    Serial.println("Disconnected from Wi-Fi.");
    mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
    wifiReconnectTimer.once(wifi_reconnect_interval, connectToWifi);
}

void connectToMqtt()
{
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
    Serial.println("Connected to MQTT.");
    Serial.print("Session present: ");
    Serial.println(sessionPresent);
    uint16_t packetIdSub = mqttClient.subscribe(MQTT_SUB_TOPIC, 2);
    Serial.printf("Subscribing [%s] at QoS 2, packetId: ", MQTT_SUB_TOPIC);
    Serial.println(packetIdSub);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Serial.print("Disconnected from MQTT, reason: ");
    if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT)
    {
        Serial.println("Bad server fingerprint.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED)
    {
        Serial.println("TCP Disconnected.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION)
    {
        Serial.println("Bad server fingerprint.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED)
    {
        Serial.println("MQTT Identifier rejected.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE)
    {
        Serial.println("MQTT server unavailable.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS)
    {
        Serial.println("MQTT malformed credentials.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED)
    {
        Serial.println("MQTT not authorized.");
    }
    else if (reason == AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE)
    {
        Serial.println("Not enough space on esp8266.");
    }

    if (WiFi.isConnected())
    {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
    Serial.print("Subscribe acknowledged: QoS: ");
    Serial.print(qos);
    Serial.print(", packetId: ");
    Serial.println(packetId);
}

void onMqttUnsubscribe(uint16_t packetId)
{
    Serial.println("Unsubscribe acknowledged.");
    Serial.print("  packetId: ");
    Serial.println(packetId);
}

void onMqttMessage(char *topic, char *payload_in, AsyncMqttClientMessageProperties properties, size_t length, size_t index, size_t total)
{
    Serial.print("MQTT: Recieved [");
    Serial.print(topic);
    char *payload = (char *)malloc(length + 1);
    memcpy(payload, payload_in, length);
    payload[length] = NULL;
    String payload_str = String(payload);
    free(payload);
    Serial.printf("]: %s\n", payload);

    if (payload_str == "ON" or payload_str == "on" or payload_str == "1")
    {
        Serial.println(">>>>>>>>>>>Toggle Power MQTT<<<<<<<<<");
        togglePower();
    }
}

void onMqttPublish(uint16_t packetId)
{
    Serial.println("Publish acknowledged.");
    Serial.print("  packetId: ");
    Serial.println(packetId);
}

void pinLow(void){
    digitalWrite(PIN_CTRL, LOW);
}

void togglePower(void)
{
    Serial.println(">>>>>>>>>>>>>>togglePower()<<<<<<<<<<<<");
    digitalWrite(PIN_CTRL, HIGH);
    togglePowerPin.once_ms(500, pinLow);
    if(mqttClient.connected()) mqttClient.publish(MQTT_PUB_TOPIC, 0, false, "toggle");
    // dont use delays in Async web-server requests, use tickers!
    // delay(1000);
    // digitalWrite(PIN_CTRL, LOW);
}

void MDNS_init(void) {
    MDNS.setInstanceName(String(HOSTNAME " (" + String(chipId) + ")").c_str());
    if (MDNS.begin(NameChipId))
    {
        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("mqtt", "tcp", atoi(MQTT_PORT));
        // MDNS.addServiceTxt("mqtt", "tcp", "CID", chipId);
        // MDNS.addServiceTxt("mqtt", "tcp", "Model", HOSTNAME);
        // MDNS.addServiceTxt("mqtt", "tcp", "Manuf", "debsahu");
        Serial.printf(">>> MDNS Started: http://%s.local/\n", NameChipId);
    }
    else
    {
        Serial.println(F(">>> Error setting up mDNS responder <<<"));
    }
}

void setup()
{
    SPIFFS.begin();
    Serial.begin(115200);
    Serial.println();
    Serial.println();
    pinMode(PIN_CTRL, OUTPUT);
    digitalWrite(PIN_CTRL, LOW);
    delay(10);

    snprintf(chipId, sizeof(chipId), "%06x", ESP.getChipId());
    snprintf(NameChipId, sizeof(NameChipId), "%s_%06x", HOSTNAME, ESP.getChipId());

    WiFi.mode(WIFI_AP_STA);
    WiFi.hostname(const_cast<char *>(NameChipId));
    AsyncWiFiManager wifiManager(&server, &dns); //Local intialization. Once its business is done, there is no need to keep it around
    wifiManager.setConfigPortalTimeout(60);     //sets timeout until configuration portal gets turned off, useful to make it all retry or go to sleep in seconds
    AsyncWiFiManagerParameter custom_mqtt_host("host", "MQTT hostname", MQTT_HOST, 64);
    AsyncWiFiManagerParameter custom_mqtt_port("port", "MQTT port", MQTT_PORT, 6);
    AsyncWiFiManagerParameter custom_mqtt_user("user", "MQTT user", MQTT_USER, 32);
    AsyncWiFiManagerParameter custom_mqtt_pass("pass", "MQTT pass", MQTT_PASS, 32);

    wifiManager.addParameter(&custom_mqtt_host);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);

    if (!wifiManager.autoConnect(NameChipId))
    {
        Serial.println("Failed to connect and hit timeout");
        //ESP.restart();
        //nothing to do here, entering AP mode
        startMDNS = false;
    } 
    else
    {
        Serial.println("---------------------------------------");
        Serial.print("Router IP: ");
        Serial.println(WiFi.localIP());
    }

    Serial.println("---------------------------------------");
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(NameChipId, AP_PASS);
    Serial.print("HotSpt IP: ");
    Serial.println(WiFi.softAPIP());
    dns=AsyncDNSServer();
    dns.start(DNS_PORT, "*", WiFi.softAPIP());

    strcpy(MQTT_HOST, custom_mqtt_host.getValue());
    strcpy(MQTT_PORT, custom_mqtt_port.getValue());
    strcpy(MQTT_USER, custom_mqtt_user.getValue());
    strcpy(MQTT_PASS, custom_mqtt_pass.getValue());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Accessing root");
        request->send_P(200, "text/html", root_html);
    });
    server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Accessing /version");
        request->send(200, "text/plain", SKETCH_VERSION);
    });
    server.on("/connect_wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Connecting to previously stored WiFi credentials");
        //WiFi.mode(WIFI_AP_STA);
        connectToWifi();
        request->send(200, "text/html", "<META http-equiv='refresh' content='5;URL=/'> Connecting to WiFi");
    });
    server.on("/togglePower", HTTP_GET, [](AsyncWebServerRequest *request) {
        togglePower();
        request->send(200, "text/html", "<META http-equiv='refresh' content='2;URL=/'> Toggle Success, refreshing ...");
    });
    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/restart");
        request->send(200, "text/html", "<META http-equiv='refresh' content='15;URL=/'> Restarting...");
        ESP.restart();
    });
    server.on("/reset_wlan", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/reset_wlan");
        request->send(200, "text/html", "<META http-equiv='refresh' content='15;URL=/'> Resetting WLAN and restarting...");
        server.reset();
        dns=AsyncDNSServer();
        AsyncWiFiManager wifiManager(&server, &dns);
        wifiManager.resetSettings();
        ESP.restart();
    });
    server.on("/start_config_ap", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/start_config_ap");
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Starting config AP ...");
        response->addHeader("Connection", "close");
        request->send(response);
        AsyncWiFiManager wifiManager(&server, &dns);
        wifiManager.startConfigPortal(NameChipId);
    });
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Accessing /update");
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", update_html);
        request->send(response);
    });
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", shouldReboot ? "<META http-equiv='refresh' content='15;URL=/'>Update Success, rebooting..." : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response); },
              [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
          if (!filename.endsWith(".bin")) {
                return;
            }
            if(!index){
                Serial.printf("Update Start: %s\n", filename.c_str());
                Update.runAsync(true);
                if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
                Update.printError(Serial);
                }
            }
            if(!Update.hasError()){
                if(Update.write(data, len) != len) {
                Update.printError(Serial);
                }
            }
            if(final){
                if(Update.end(true)) 
                Serial.printf("Update Success: %uB\n", index+len);
                else {
                Update.printError(Serial);
                }
            } });
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/"); // send all DNS requests to root
        //request->send_P(200, "text/html", root_html);
    });

    if(startMDNS) MDNS_init();

    server.begin();

    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(MQTT_HOST, atoi(MQTT_PORT));
    if (MQTT_USER != "" or MQTT_PASS != "")
        mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
    mqttClient.setClientId(NameChipId);

    if(WiFi.isConnected()) connectToMqtt(); // Connect to MQTT server only if WiFi is connected to router, do not connect if in AP mode
}

void loop()
{
    if(startMDNS) MDNS.update();

    if (shouldReboot)
        ESP.reset();

}