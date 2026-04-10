#include <Arduino.h>
#include <SPIFFS.h>
#include "lora_mesh.h"
#include "web_server.h"
#include "wifi_manager.h"


//INCLUDES PARA VERIFICAR OS VALORES CÁLCULADOS NA SERIAL:
#include "sensor_buffer.h"      // Para ler a média do buffer
#include "sensor_calibration.h" // Para converter ADC -> CM

// ===============================
// STATE MACHINE 
// ===============================
enum SystemState {
    STATE_INIT,
    STATE_LORA_INIT,
    STATE_WIFI_INIT,
    STATE_SERVER_INIT,
    STATE_RUNNING,
    STATE_ERROR
};

SystemState currentState = STATE_INIT;
uint32_t wifiStartTime = 0;

// ======================================================
// SETUP
// ======================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n===============================");
    Serial.println("[BOOT] Iniciando firmware ESP32");
    Serial.println("===============================");

    if (!SPIFFS.begin(true)) {
        Serial.println("[ERRO] Falha ao montar SPIFFS");
        currentState = STATE_ERROR;
        return;
    }

    Serial.println("[OK] SPIFFS montado com sucesso");
}

// ======================================================
// LOOP
// ======================================================
void loop() {
    switch (currentState) {

        // ---------------------------
        case STATE_INIT:
            Serial.println("[STATE] INIT -> LORA_INIT");
            currentState = STATE_LORA_INIT;
            break;

        // ---------------------------
        case STATE_LORA_INIT:
            Serial.println("[STATE] Inicializando LoRaMesh...");
            if (!initLoRaMesh()) {
                Serial.println("[ERRO] Falha ao inicializar LoRaMesh");
                currentState = STATE_ERROR;
            } else {
                Serial.println("[OK] LoRaMesh inicializado");
                currentState = STATE_WIFI_INIT;
            }
            break;

        // ---------------------------
        case STATE_WIFI_INIT:
            Serial.println("[STATE] Conectando ao WiFi...");
            
            wifiInit();
            
            wifiStartTime = millis();
            currentState = STATE_SERVER_INIT;
            break;

        // ---------------------------
        case STATE_SERVER_INIT:
            // Verifica conexão do wifi
            if (wifiCheckConnection()) {
                Serial.println("[OK] WiFi conectado");
                Serial.print("[INFO] IP: ");
                Serial.println(wifiGetIP());

                Serial.println("[STATE] Inicializando Web Server...");
                initWebServer();

                Serial.println("[STATE] Iniciando tarefa LoRa...");
                startLoRaTask();

                Serial.println("[SYSTEM] Sistema operacional");
                currentState = STATE_RUNNING;

            } else if (millis() - wifiStartTime > WIFI_TIMEOUT_MS) {
                Serial.println("[ERRO] Timeout ao conectar no WiFi");
                currentState = STATE_ERROR;
            } else {
                // Ainda tentando conectar...
                Serial.print(".");
                delay(300);
            }
            break;

        // ---------------------------
        case STATE_RUNNING:
        {
            
            // 1. Pega a média atual
            float mediaAdc = sensorBufferMean();

            // 2. Converte
            float nivelCm = sensorAdcToCm(mediaAdc);

            // 3. Printa debug
            Serial.print("[STATUS] Buffer Médio: ");
            Serial.print(mediaAdc, 1);
            Serial.print(" | Nível: ");
            Serial.print(nivelCm, 2);
            Serial.println(" cm");

            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Auto-reconnect
            if (!wifiCheckConnection()) {
                Serial.println("[AVISO] WiFi caiu! Reconectando...");
                wifiInit(); 
            }
            break;
        }

        // ---------------------------
        case STATE_ERROR:
            Serial.println("\n[STATE] ERRO CRITICO");
            Serial.println("[SYSTEM] Reiniciando em 5 segundos...");
            delay(5000);
            ESP.restart();
            break;
    }
}