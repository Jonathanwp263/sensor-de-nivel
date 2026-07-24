#include "lora_mesh.h"
#include "data_model.h"
#include <LoRaMESH.h>
#include <HardwareSerial.h>
#include "sensor_processing.h"

#define CMD_READ_RSSI_V2  0xD5
// ==========================================
// CONFIGURAÇÕES GLOBAIS
// ==========================================
static uint16_t LORA_REMOTE_ID = 1;      // ID do Sensor Remoto

// Ciclo da leitura crítica (nível) - roda sozinha, sem esperar bateria/RSSI
static const uint32_t LEVEL_INTERVAL_MS = 5000;
// Ciclo da leitura de diagnóstico (bateria + RSSI) - não crítico, cadência mais lenta
static const uint32_t DIAG_INTERVAL_MS  = 30000;
// Respiro entre bateria e RSSI dentro do mesmo ciclo de diagnóstico (fora do mutex do rádio)
static const uint32_t RSSI_SETTLE_MS    = 2000;
// Respiro após o LocalRead de boot, antes do primeiro comando de config de GPIO
static const uint32_t BOOT_SETTLE_MS    = 1500;

// Variáveis de Sistema
static HardwareSerial* hSerialCommands = NULL;
// Serializa o acesso ao rádio (UART + buffer estático da lib LoRaMESH) entre as tasks
static SemaphoreHandle_t radioMutex = NULL;

TaskHandle_t xLoraLevelTaskHandle = NULL;
TaskHandle_t xLoraDiagTaskHandle = NULL;

// Buffers locais para os comandos manuais
uint8_t bufferPayload[MAX_PAYLOAD_SIZE] = {0};
uint8_t receivedBuffer[MAX_PAYLOAD_SIZE] = {0};
uint8_t payloadSize = 0;
uint8_t receivedSize = 0;
uint16_t receivedId;
uint8_t receivedCommand;

// ==========================================
// FUNÇÕES AUXILIARES INTERNAS
// ==========================================

// --- FUNÇÃO DE DEBUG DO RSSI ---
bool updateRemoteRSSI() {
    // 1. Prepara o Payload exato que o Manual exige (Pag 23)
    // Byte 0: 0x01
    // Byte 1: 0x02
    // Byte 2: 0x00
    bufferPayload[0] = 0x01; 
    bufferPayload[1] = 0x02;
    bufferPayload[2] = 0x00;
    payloadSize = 3; 

    Serial.print("[RSSI] Pedindo sinal (CMD 0xD5)... ");

    // 2. Envia Comando 0xD5
    PrepareFrameCommand(LORA_REMOTE_ID, CMD_READ_RSSI_V2, bufferPayload, payloadSize);
    SendPacket();

    // 3. Aguarda Resposta (Aumentei timeout para garantir)
    if(ReceivePacketCommand(&receivedId, &receivedCommand, receivedBuffer, &receivedSize, 4000) == MESH_OK) {
        Serial.println("Recebido!");

        // Debug para confirmar o que chegou
        // Serial.printf("Dump: [0]:%d [1]:%d [2]:%d [3]:%d\n", receivedBuffer[0], receivedBuffer[1], receivedBuffer[2], receivedBuffer[3]);

        // INTERPRETAÇÃO DA RESPOSTA (Manual Pag 23):
        // A lib LoRaMESH geralmente entrega o buffer JÁ pulando o ID e CMD.
        // Se for isso, a estrutura do bufferPayload recebido será:
        // [0] = GW LSB
        // [1] = GW MSB
        // [2] = RSSI IDA (Módulo)
        // [3] = RSSI VOLTA (Módulo)
        
        // Conversão: O valor vem positivo (ex: 80 significa -80dBm)
        int rssiIda   = -1 * receivedBuffer[2];
        int rssiVolta = -1 * receivedBuffer[3];
        
        Serial.printf("[RSSI] Ida: %d dBm | Volta: %d dBm\n", rssiIda, rssiVolta);

        // Filtro: Só atualiza se o valor for válido (diferente de 0)
        if (rssiVolta != 0 && rssiVolta > -150) {
            xSemaphoreTake(sensorMutex, portMAX_DELAY);
            netStats.rssi_ida = rssiIda;
            netStats.rssi_volta = rssiVolta;
            xSemaphoreGive(sensorMutex);
            return true;
        }
    } 
    else {
        Serial.println("Timeout no RSSI (0xD5)");
    }
    
    return false;
}
/**
 * @brief Tenta configurar um GPIO remoto com retentativas.
 */
bool tryConfigureGpio(uint16_t targetId, GPIO_Typedef pin, int maxRetries) {
    for (int i = 1; i <= maxRetries; i++) {
        Serial.printf("[LoRa] Config GPIO %d ID %d (Tentativa %d/%d)... ", pin, targetId, i, maxRetries);
        
        // Limpa lixo da serial antes
        while(hSerialCommands->available() > 0) hSerialCommands->read();
        
        if (GpioConfig(targetId, pin, ANALOG_IN, PULL_OFF) == MESH_OK) {
            Serial.println("[OK]");
            return true;
        }
        
        Serial.println("[FALHA] - Aguardando 3s...");
        delay(3000);
    }
    return false;
}

// ==========================================
// TASK CRÍTICA: NÍVEL (alta prioridade, ciclo curto)
// ==========================================
// Só lê o GPIO6 (nível) e devolve o rádio. Não fica presa atrás de
// bateria/RSSI, então o dado mais importante para o usuário atualiza
// no ritmo de LEVEL_INTERVAL_MS, independente do que a DiagTask esteja
// fazendo no mesmo rádio.
void loraLevelTask(void *pvParameters) {

    sensorProcessingInit();

    Serial.println("[LoRa][Level] Task iniciada.");

    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        uint16_t rawLevelAdc;

        xSemaphoreTake(sensorMutex, portMAX_DELAY);
        netStats.tx_packets++;
        xSemaphoreGive(sensorMutex);

        // Só segura o rádio pelo tempo desta transação (pede + espera resposta),
        // nunca durante os delays de "respiro" - isso é o que libera a DiagTask
        // para intercalar seus próprios pedidos sem travar o nível por segundos.
        xSemaphoreTake(radioMutex, portMAX_DELAY);
        MeshStatus_Typedef status = GpioRead(LORA_REMOTE_ID, GPIO6, &rawLevelAdc);
        xSemaphoreGive(radioMutex);

        if (status == MESH_OK) {
            xSemaphoreTake(sensorMutex, portMAX_DELAY);
            netStats.rx_packets++;
            xSemaphoreGive(sensorMutex);

            float nivelFinal = sensorProcess(rawLevelAdc);

            xSemaphoreTake(sensorMutex, portMAX_DELAY);
            sensorData.nivel_cm = nivelFinal;
            xSemaphoreGive(sensorMutex);

            Serial.printf("[LoRa][Level] OK -> %.2f cm\n", nivelFinal);
        } else {
            Serial.println("[LoRa][Level] FALHA (sem resposta do nivel)");

            xSemaphoreTake(sensorMutex, portMAX_DELAY);
            netStats.lost_packets++;
            sensorData.nivel_cm = 0;
            xSemaphoreGive(sensorMutex);
            // OBS: bateria/RSSI não são zerados aqui, ficam por conta da DiagTask.
        }

        xSemaphoreTake(sensorMutex, portMAX_DELAY);
        if (netStats.tx_packets > 0) {
            netStats.packet_loss_pct = ((float)netStats.lost_packets / netStats.tx_packets) * 100.0f;
        }
        xSemaphoreGive(sensorMutex);

        // vTaskDelayUntil em vez de vTaskDelay: o período fica estável em
        // LEVEL_INTERVAL_MS mesmo que este ciclo tenha demorado mais que o normal
        // (ex.: teve que esperar a DiagTask liberar o radioMutex).
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(LEVEL_INTERVAL_MS));
    }
}

// ==========================================
// TASK DE DIAGNÓSTICO: BATERIA + RSSI (baixa prioridade, ciclo longo)
// ==========================================
// Não é dado crítico para a operação - roda bem mais devagar e nunca
// atrasa a LevelTask além da duração de uma única transação de rádio.
void loraDiagTask(void *pvParameters) {

    Serial.println("[LoRa][Diag] Task iniciada.");

    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        uint16_t rawBatAdc;

        xSemaphoreTake(radioMutex, portMAX_DELAY);
        MeshStatus_Typedef battStatus = GpioRead(LORA_REMOTE_ID, GPIO5, &rawBatAdc);
        xSemaphoreGive(radioMutex);

        if (battStatus == MESH_OK) {
            // Cálculo: (ADC / 4095) * 3.3V * 2 (Divisor resistivo 100k/100k)
            float tensao = (rawBatAdc * 3.3f / 4095.0f) * 2.0f;
            xSemaphoreTake(sensorMutex, portMAX_DELAY);
            sensorData.bateria_V = tensao;
            xSemaphoreGive(sensorMutex);
            Serial.printf("[LoRa][Diag] Bateria OK -> %.2fV\n", tensao);
        } else {
            Serial.println("[LoRa][Diag] Falha leitura bateria (ignorado)");
            // Mantemos o valor anterior da bateria para não piscar "0V" na tela
        }

        // Respiro antes do comando manual de RSSI. Fica FORA do radioMutex de
        // propósito: durante esta pausa a LevelTask pode usar o rádio livremente.
        vTaskDelay(pdMS_TO_TICKS(RSSI_SETTLE_MS));

        while (hSerialCommands->available() > 0) hSerialCommands->read();

        xSemaphoreTake(radioMutex, portMAX_DELAY);
        bool rssiOk = updateRemoteRSSI();
        xSemaphoreGive(radioMutex);

        if (!rssiOk) {
            Serial.println("[LoRa][Diag] RSSI ignorado (Timeout ou Rádio Ocupado)");
        }

        Serial.printf("[Stats] Perda: %.2f%% | Nivel: %.2fcm | Bat: %.2fV | RSSI: %d\n",
                      netStats.packet_loss_pct,
                      sensorData.nivel_cm,
                      sensorData.bateria_V,
                      netStats.rssi_volta);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(DIAG_INTERVAL_MS));
    }
}

// ==========================================
// INICIALIZAÇÃO DO SISTEMA
// ==========================================
bool initLoRaMesh() {
    // Cria o mutex de acesso aos dados compartilhados (sensorData + netStats)
    // Deve ser criado aqui, após o scheduler FreeRTOS estar ativo
    sensorMutex = xSemaphoreCreateMutex();
    radioMutex = xSemaphoreCreateMutex();

    uint16_t localId, localNet;
    uint32_t localUniqueId;

    Serial.println("[LoRa] Iniciando SerialCommands (UART 2)...");
    hSerialCommands = SerialCommandsInit(16, 17, 9600, 2);

    if (hSerialCommands == nullptr) {
        Serial.println("[LoRa][ERRO] Falha ao iniciar SerialCommands");
        return false;
    }

    Serial.println("[LoRa] Aguardando 5s para estabilização...");
    delay(5000); 

    // Limpeza de buffer inicial
    while(hSerialCommands->available() > 0) hSerialCommands->read();

    Serial.println("[LoRa] Verificando módulo local...");
    if (LocalRead(&localId, &localNet, &localUniqueId) != MESH_OK) {
        Serial.println("[LoRa][ERRO CRÍTICO] Módulo local não responde.");
        return false;
    }

    Serial.printf("[LoRa] Local OK -> ID: %d | NET: %d\n", localId, localNet);

    // -----------------------------------------------------------------
    // RESPIRO PÓS-LOCALREAD (era a causa do "funciona isolado, falha aqui"):
    // o LocalRead acima acabou de fazer o módulo local transmitir e esperar
    // resposta; disparar o comando de config de GPIO imediatamente em seguida,
    // sem pausa, é inconsistente com o resto do código (que sempre dá 1-2s de
    // folga entre comandos consecutivos ao rádio) e deixa o primeiro
    // GpioConfig competindo com uma resposta/estado ainda em acomodação.
    // -----------------------------------------------------------------
    vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));

    // Configuração dos GPIOs remotos (com retentativas)
    if (!tryConfigureGpio(LORA_REMOTE_ID, GPIO6, 2)) {
        Serial.println("[LoRa][ERRO] Falha ao configurar GPIO 6.");
    }

    delay(1000);

    if (!tryConfigureGpio(LORA_REMOTE_ID, GPIO5, 2)) {
        Serial.println("[LoRa][ERRO] Falha ao configurar GPIO 5.");
    }

    Serial.println("[LoRa] Inicialização concluída.");
    return true; 
}

void startLoRaTask() {
    // Prioridade mais alta: nunca deve ficar presa atrás da diagnóstica.
    xTaskCreatePinnedToCore(
        loraLevelTask,
        "LoraLevelTask",
        10000,
        NULL,
        2,
        &xLoraLevelTaskHandle,
        0
    );

    // Prioridade mais baixa: bateria/RSSI cedem o rádio para a LevelTask.
    xTaskCreatePinnedToCore(
        loraDiagTask,
        "LoraDiagTask",
        10000,
        NULL,
        1,
        &xLoraDiagTaskHandle,
        0
    );
}