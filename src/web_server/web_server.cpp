#include "web_server.h"
#include "data_model.h"
#include "lora_mesh.h" // Acesso ao handle da task
#include "rssi_handler.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Arduino_JSON.h>

AsyncWebServer server(80);

// Declaração antecipada
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

// Página de fallback caso não exista index.html no SPIFFS
const char* upload_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Sistema Iniciado</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family: sans-serif; background: #eee; text-align: center; padding: 50px;}
div{background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); max-width: 400px; margin: auto;}
button{background: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin-top: 10px;}
</style>
</head>
<body>
<div>
<h1>Bem-vindo!</h1>
<p>O sistema de arquivos esta vazio.</p>
<p>Faça o upload do arquivo <b>index.html</b> para ver o painel.</p>
<form method='POST' action='/upload' enctype='multipart/form-data'>
<input type='file' name='upload' accept='.html'>
<br>
<button type='submit'>Enviar Interface</button>
</form>
</div>
</body>
</html>
)rawliteral";

void initWebServer() {
    
    // --- ROTA PRINCIPAL ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SPIFFS.exists("/index.html")) {
            request->send(SPIFFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html", upload_html);
        }
    });

    // --- ROTA DE DADOS (JSON) ---
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        
        // 1. Traduz o RSSI (netStats vem do data_model.h)
        SignalQuality sig = calculateSignalQuality(netStats.rssi_volta);

        JSONVar response;
        
        // Dados do Sensor (Nomes das chaves devem bater com o HTML/JS)
        response["level_cm"]  = sensorData.nivel_cm;
        response["voltage_V"] = sensorData.bateria_V;
        
        // Dados de Sinal & Rede
        response["rssi_dbm"]     = sig.rssi_dbm;
        response["signal_pct"]   = sig.percentage;
        response["signal_qual"]  = sig.status;
        response["signal_class"] = sig.cssClass; // Ex: "signal-good"
        
        // Estatísticas
        response["loss_pct"]     = String(netStats.packet_loss_pct, 2);
        
        // Info Estática
        response["radio_model"]  = "Radioenge LoRaMESH";

        request->send(200, "application/json", JSON.stringify(response));
    });

    // --- ROTA DE UPLOAD ---
    server.on("/upload", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "Upload OK! Reiniciando pagina...");
        },
        handleUpload
    );

    server.begin();
    Serial.println("[WEBSERVER] Iniciado (Async).");
}

// Implementação do Upload
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    
    if (!index) {
        Serial.printf("[Upload] Recebendo: %s\n", filename.c_str());

        // 1. PAUSA O LORA (CRÍTICO PARA NÃO TRAVAR A SPI)
        if (xLoraTaskHandle != NULL) {
            vTaskSuspend(xLoraTaskHandle);
        }

        // Força salvar como /index.html se for a página principal
        // Isso ajuda se você upar um arquivo com nome "dashboard_v2.html" sem querer
        if(filename.endsWith(".html")) filename = "/index.html";
        if(!filename.startsWith("/")) filename = "/" + filename;

        request->_tempFile = SPIFFS.open(filename, "w");
    }

    if (len) {
        if(request->_tempFile) {
            request->_tempFile.write(data, len);
        }
    }

    if (final) {
        if (request->_tempFile) {
            request->_tempFile.close();
        }
        Serial.printf("[Upload] Concluido: %u bytes\n", index + len);

        // 2. RETOMA O LORA
        if (xLoraTaskHandle != NULL) {
            vTaskResume(xLoraTaskHandle);
            Serial.println("[System] LoRa retomado.");
        }
    }
}