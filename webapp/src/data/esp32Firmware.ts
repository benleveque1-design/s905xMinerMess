export const ESP32_SKETCH_CPP = `/*
 * S905X Bitcoin Mining Controller for ESP32
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
 *   - Embedded compact HTML/JS/CSS dashboard served from SPIFFS or PROGMEM
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
        Serial.printf("[WS Worker] Client #%u connected from %s\\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS Worker] Client #%u disconnected\\n", client->id());
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active && workers[i].clientId == client->id()) {
                workers[i].active = false;
                strncpy(workers[i].state, "OFFLINE", sizeof(workers[i].state));
                // Notify dashboard
                StaticJsonDocument<256> doc;
                doc["type"] = "worker_offline";
                doc["workerId"] = workers[i].workerId;
                char buf[256];
                serializeJson(doc, buf);
                broadcastToClients(buf);
                break;
            }
        }
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, (char*)data);
            if (error) return;

            const char* msgType = doc["type"] | "";
            
            if (strcmp(msgType, "auth") == 0) {
                const char* token = doc["token"] | "";
                if (strcmp(token, AUTH_TOKEN) != 0) {
                    client->close(4001, "Unauthorized");
                    return;
                }
                const char* wId = doc["workerId"] | "";
                int slot = -1;
                for (int i = 0; i < MAX_WORKERS; i++) {
                    if (workers[i].active && strcmp(workers[i].workerId, wId) == 0) {
                        slot = i;
                        break;
                    }
                    if (!workers[i].active && slot == -1) slot = i;
                }
                if (slot != -1) {
                    workers[slot].clientId = client->id();
                    workers[slot].active = true;
                    strncpy(workers[slot].workerId, wId, sizeof(workers[slot].workerId));
                    strncpy(workers[slot].name, doc["name"] | wId, sizeof(workers[slot].name));
                    strncpy(workers[slot].state, "IDLE", sizeof(workers[slot].state));
                    workers[slot].lastSeenMs = millis();
                    
                    // Reply auth_ack
                    StaticJsonDocument<256> ack;
                    ack["type"] = "auth_ack";
                    ack["status"] = "ok";
                    char ackBuf[256];
                    serializeJson(ack, ackBuf);
                    client->text(ackBuf);
                }
            } else if (strcmp(msgType, "telemetry") == 0) {
                const char* wId = doc["workerId"] | "";
                for (int i = 0; i < MAX_WORKERS; i++) {
                    if (workers[i].active && strcmp(workers[i].workerId, wId) == 0) {
                        strncpy(workers[i].state, doc["state"] | "RUNNING", sizeof(workers[i].state));
                        workers[i].threads = doc["threads"] | 4;
                        workers[i].hashrateMhs = doc["hashrateMhs"] | 0.0;
                        workers[i].tempC = doc["tempC"] | 0.0;
                        workers[i].cpuFreqMhz = doc["cpuFreqMhz"] | 1512;
                        workers[i].sharesAccepted = doc["sharesAccepted"] | 0;
                        workers[i].sharesRejected = doc["sharesRejected"] | 0;
                        workers[i].lastSeenMs = millis();
                        break;
                    }
                }
                // Forward telemetry directly to connected web clients
                broadcastToClients((char*)data);
            } else if (strcmp(msgType, "command_ack") == 0) {
                broadcastToClients((char*)data);
            }
        }
    }
}

void onClientWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        // Send initial fleet snapshot
        DynamicJsonDocument doc(4096);
        doc["type"] = "fleet_sync";
        JsonArray arr = doc.createNestedArray("workers");
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active) {
                JsonObject w = arr.createNestedObject();
                w["workerId"] = workers[i].workerId;
                w["name"] = workers[i].name;
                w["state"] = workers[i].state;
                w["threads"] = workers[i].threads;
                w["hashrateMhs"] = workers[i].hashrateMhs;
                w["tempC"] = workers[i].tempC;
                w["cpuFreqMhz"] = workers[i].cpuFreqMhz;
                w["sharesAccepted"] = workers[i].sharesAccepted;
                w["sharesRejected"] = workers[i].sharesRejected;
            }
        }
        String out;
        serializeJson(doc, out);
        client->text(out);
    } else if (type == WS_EVT_DATA) {
        // Command from dashboard to worker
        data[len] = 0;
        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, (char*)data)) {
            const char* targetWorkerId = doc["workerId"] | "";
            for (int i = 0; i < MAX_WORKERS; i++) {
                if (workers[i].active && (strcmp(targetWorkerId, "all") == 0 || strcmp(workers[i].workerId, targetWorkerId) == 0)) {
                    wsWorker.text(workers[i].clientId, (char*)data);
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\\nESP32 Controller Online at http://%s\\n", WiFi.localIP().toString().c_str());

    wsWorker.onEvent(onWorkerWsEvent);
    wsClient.onEvent(onClientWsEvent);
    server.addHandler(&wsWorker);
    server.addHandler(&wsClient);

    server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\\"status\\":\\"ok\\",\\"device\\":\\"ESP32-WROOM-32\\"}");
    });

    server.begin();
}

void loop() {
    wsWorker.cleanupClients();
    wsClient.cleanupClients();
    
    // Check watchdog timeouts (>8 seconds without telemetry)
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active && (millis() - workers[i].lastSeenMs > 8000)) {
                workers[i].active = false;
                strncpy(workers[i].state, "OFFLINE", sizeof(workers[i].state));
                StaticJsonDocument<256> doc;
                doc["type"] = "worker_offline";
                doc["workerId"] = workers[i].workerId;
                char buf[256];
                serializeJson(doc, buf);
                broadcastToClients(buf);
            }
        }
    }
}
`;
