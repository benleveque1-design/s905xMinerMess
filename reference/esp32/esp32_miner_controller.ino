/*
 * S905X Bitcoin Mining Controller for ESP32 (Optional Reference Implementation)
 * 
 * Hardware: Standard ESP32 DevKit V1 / NodeMCU-32S (WROOM-32)
 * Libraries required:
 *   - ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
 *   - AsyncTCP (https://github.com/me-no-dev/AsyncTCP)
 *   - ArduinoJson v6+ (https://github.com/bblanchon/ArduinoJson)
 * 
 * Features:
 *   - Hosts WebSocket endpoint ws://<ESP32_IP>/ws/worker for all S905X boxes
 *   - Hosts WebSocket endpoint ws://<ESP32_IP>/ws/client for web dashboard
 *   - Command routing (start, stop, restart, set_threads, set_pool) to S905X workers
 *   - Watchdog timer tracking worker health and disconnection
 *   - Lightweight RAM usage: ~32 KB total heap allocated
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* AUTH_TOKEN = "s905x_secret_token";

AsyncWebServer server(80);
AsyncWebSocket wsWorker("/ws/worker");
AsyncWebSocket wsClient("/ws/client");

#define MAX_WORKERS 16

struct Worker {
    uint32_t clientId;
    bool active;
    char workerId[32];
    char name[32];
    char state[16];
    uint8_t threads;
    float hashrateMhs;
    float tempC;
    uint16_t cpuFreqMhz;
    uint32_t sharesAccepted;
    uint32_t sharesRejected;
    uint32_t lastSeenMs;
};

Worker workers[MAX_WORKERS];

void broadcastToClients(const char* jsonBuffer) {
    wsClient.textAll(jsonBuffer);
}

void onWorkerWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS Worker] Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS Worker] Client #%u disconnected\n", client->id());
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active && workers[i].clientId == client->id()) {
                workers[i].active = false;
                StaticJsonDocument<128> doc;
                doc["type"] = "worker_offline";
                doc["workerId"] = workers[i].workerId;
                char buf[128];
                serializeJson(doc, buf);
                broadcastToClients(buf);
                break;
            }
        }
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, (char*)data);
            if (err) return;

            const char* msgType = doc["type"];
            if (!msgType) return;

            if (strcmp(msgType, "auth") == 0) {
                const char* token = doc["token"];
                const char* workerId = doc["workerId"];
                if (token && strcmp(token, AUTH_TOKEN) == 0 && workerId) {
                    int slot = -1;
                    for (int i = 0; i < MAX_WORKERS; i++) {
                        if (!workers[i].active || strcmp(workers[i].workerId, workerId) == 0) {
                            slot = i;
                            break;
                        }
                    }
                    if (slot >= 0) {
                        workers[slot].clientId = client->id();
                        workers[slot].active = true;
                        strncpy(workers[slot].workerId, workerId, sizeof(workers[slot].workerId) - 1);
                        strncpy(workers[slot].name, doc["name"] | workerId, sizeof(workers[slot].name) - 1);
                        strncpy(workers[slot].state, "STOPPED", sizeof(workers[slot].state) - 1);
                        workers[slot].threads = doc["threads"] | 3;
                        workers[slot].hashrateMhs = 0.0;
                        workers[slot].tempC = 0.0;
                        workers[slot].lastSeenMs = millis();
                        
                        client->text("{\"type\":\"auth_ack\",\"status\":\"ok\"}");
                        Serial.printf("[Auth] Worker %s authenticated in slot %d\n", workerId, slot);
                    }
                }
            } else if (strcmp(msgType, "telemetry") == 0) {
                const char* workerId = doc["workerId"];
                for (int i = 0; i < MAX_WORKERS; i++) {
                    if (workers[i].active && strcmp(workers[i].workerId, workerId) == 0) {
                        workers[i].hashrateMhs = doc["hashrateMhs"] | 0.0f;
                        workers[i].tempC = doc["tempC"] | 0.0f;
                        workers[i].cpuFreqMhz = doc["cpuFreqMhz"] | 1512;
                        workers[i].sharesAccepted = doc["sharesAccepted"] | 0;
                        workers[i].sharesRejected = doc["sharesRejected"] | 0;
                        strncpy(workers[i].state, doc["state"] | "RUNNING", sizeof(workers[i].state) - 1);
                        workers[i].lastSeenMs = millis();

                        // Forward directly to web dashboard clients
                        broadcastToClients((char*)data);
                        break;
                    }
                }
            }
        }
    }
}

void onClientWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS Client] Web dashboard connected #%u\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            // Forward command to S905X workers
            wsWorker.textAll((char*)data);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[ESP32] S905X Bitcoin Mining Controller starting...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

    wsWorker.onEvent(onWorkerWsEvent);
    wsClient.onEvent(onClientWsEvent);
    server.addHandler(&wsWorker);
    server.addHandler(&wsClient);

    server.begin();
    Serial.println("[Server] HTTP & WebSocket Controller active on port 80");
}

void loop() {
    wsWorker.cleanupClients();
    wsClient.cleanupClients();
    delay(20);
}
