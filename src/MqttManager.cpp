#include "MqttManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <vector>

namespace {
    MqttConfig       g_cfg;
    MqttJsonHandler  g_handler;
    WiFiClient       g_net;
    PubSubClient     g_mqtt(g_net);
    std::vector<String> g_subs;
    String           g_clientId;
    String           g_statusTopic;

    uint32_t g_lastWifiTry = 0;
    uint32_t g_lastMqttTry = 0;
    uint32_t g_retryDelay  = 1000;          // backoff เริ่ม 1s สูงสุด 30s
    bool     g_wifiWasUp   = false;

    void onRaw(char* topic, byte* payload, unsigned int len) {
        if (!g_handler) return;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, len);
        if (err) {
            Serial.printf("[MQTT] JSON error on %s: %s\n", topic, err.c_str());
            return;
        }
        g_handler(topic, doc);
    }

    void ensureWifi() {
        if (WiFi.status() == WL_CONNECTED) {
            if (!g_wifiWasUp) {
                g_wifiWasUp = true;
                Serial.printf("[WiFi] connected IP=%s RSSI=%d\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
            }
            return;
        }
        if (g_wifiWasUp) { g_wifiWasUp = false; Serial.println("[WiFi] lost"); }
        if (millis() - g_lastWifiTry < 10000) return;   // ลองใหม่ทุก 10s
        g_lastWifiTry = millis();
        Serial.println("[WiFi] reconnecting...");
        WiFi.disconnect();
        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPass);
    }

    void ensureMqtt() {
        if (g_mqtt.connected()) return;
        if (millis() - g_lastMqttTry < g_retryDelay) return;
        g_lastMqttTry = millis();

        Serial.printf("[MQTT] connecting %s:%u as %s ...\n",
                      g_cfg.host, g_cfg.port, g_clientId.c_str());
        const char* user = (g_cfg.user && *g_cfg.user) ? g_cfg.user : nullptr;
        const char* pass = user ? g_cfg.pass : nullptr;

        // LWT: ถ้าหลุดแบบไม่ตั้งใจ broker จะส่ง {"online":false} ให้เอง
        bool ok = g_mqtt.connect(g_clientId.c_str(), user, pass,
                                 g_statusTopic.c_str(), 1, true,
                                 "{\"online\":false}", true);
        if (!ok) {
            Serial.printf("[MQTT] failed rc=%d, retry in %lus\n",
                          g_mqtt.state(), (unsigned long)g_retryDelay / 1000);
            g_retryDelay = min<uint32_t>(g_retryDelay * 2, 30000);
            return;
        }
        g_retryDelay = 1000;
        Serial.println("[MQTT] connected");
        g_mqtt.publish(g_statusTopic.c_str(), "{\"online\":true}", true);
        for (auto& t : g_subs) g_mqtt.subscribe(t.c_str(), 1);
    }
}

namespace Mqtt {

void begin(const MqttConfig& cfg, MqttJsonHandler onMessage) {
    g_cfg = cfg;
    g_handler = onMessage;

    uint64_t mac = ESP.getEfuseMac();
    char id[32];
    snprintf(id, sizeof(id), "esp32-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
    g_clientId = id;                         // client id ไม่ซ้ำกันแต่ละบอร์ด
    g_statusTopic = topic("status");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                    // ลด latency / หลุดน้อยลง
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    g_lastWifiTry = millis();

    g_net.setTimeout(5);                     // socket timeout (วินาที) กัน block นาน
    g_mqtt.setServer(cfg.host, cfg.port);
    g_mqtt.setKeepAlive(cfg.keepAlive);
    g_mqtt.setSocketTimeout(5);
    g_mqtt.setBufferSize(cfg.bufferSize);
    g_mqtt.setCallback(onRaw);
}

void loop() {
    ensureWifi();
    if (WiFi.status() != WL_CONNECTED) return;
    ensureMqtt();
    g_mqtt.loop();
}

bool connected() { return g_mqtt.connected(); }

bool publishJson(const char* t, const JsonDocument& doc, bool retain) {
    if (!g_mqtt.connected()) return false;
    size_t len = measureJson(doc);
    if (len + strlen(t) + 8 > g_cfg.bufferSize) {
        Serial.printf("[MQTT] payload too big (%u B), raise bufferSize\n", (unsigned)len);
        return false;
    }
    if (!g_mqtt.beginPublish(t, len, retain)) return false;
    serializeJson(doc, g_mqtt);              // stream ตรง ไม่ต้องจอง String
    return g_mqtt.endPublish();
}

bool publish(const char* subTopic, const JsonDocument& doc, bool retain) {
    return publishJson(topic(subTopic).c_str(), doc, retain);
}

bool subscribe(const char* t) {
    for (auto& s : g_subs) if (s == t) return true;
    g_subs.push_back(t);
    return g_mqtt.connected() ? g_mqtt.subscribe(t, 1) : true;
}

String topic(const char* subTopic) {
    return String(g_cfg.baseTopic) + "/" + subTopic;
}

} // namespace Mqtt
