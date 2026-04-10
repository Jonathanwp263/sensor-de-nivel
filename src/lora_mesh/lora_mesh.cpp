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
static const long interval = 15000;       // Intervalo entre leituras (5s)

// Definições do protocolo Radioenge (Modo Manual/Frankenstein)
#define CMD_READRSSI     0x15            // Comando de leitura de RSSI

// Variáveis de Sistema
static HardwareSerial* hSerialCommands = NULL;
TaskHandle_t xLoraTaskHandle = NULL;

// Buffers locais para os comandos manuais
uint8_t bufferPayload[MAX_PAYLOAD_SIZE] = {0};
uint8_t receivedBuffer[MAX_PAYLOAD_SIZE] = {0};
uint8_t payloadSize = 0;
uint8_t receivedSize = 0;
uint16_t receivedId;
uint8_t receivedCommand;

// Acesso à variável global de estatísticas (definida no data_model.h)
// Não precisamos declarar "extern" aqui se incluímos o data_model.h
// Apenas usamos a instância 'netStats' que já existe.

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
            netStats.rssi_ida = rssiIda;
            netStats.rssi_volta = rssiVolta;
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
// TAREFA PRINCIPAL (LORA TASK)
// ==========================================
void loraTask(void *pvParameters) {
    
    // Inicializa os filtros/buffers do sensor de nível
    sensorProcessingInit();
    
    Serial.println("[LoRa Task] Iniciada com gerenciamento de timing.");

    while(true) {
        uint16_t rawLevelAdc;
        uint16_t rawBatAdc;
        bool leituraNivelOk = false;

        // 1. ESTATÍSTICA: Incrementa contador de tentativas de TX
        netStats.tx_packets++;

        // =================================================================
        // PASSO 1: LEITURA DE NÍVEL (Prioridade Máxima - GPIO 6)
        // =================================================================
        Serial.print("[LoRa] 1. Lendo Nivel... ");
        
        if (GpioRead(LORA_REMOTE_ID, GPIO6, &rawLevelAdc) == MESH_OK) {
            
            // --- SUCESSO NO NÍVEL ---
            Serial.println("OK!");
            netStats.rx_packets++;
            leituraNivelOk = true;

            // Processa e guarda o Nível
            float nivelFinal = sensorProcess(rawLevelAdc);
            sensorData.nivel_cm = (uint16_t)nivelFinal;
            
            // -----------------------------------------------------------
            // DELAY DE SEGURANÇA 1 (1 Segundo)
            // Dá tempo para o rádio remoto enviar o ACK e voltar a ouvir.
            // -----------------------------------------------------------
            vTaskDelay(pdMS_TO_TICKS(1000)); 

            // =================================================================
            // PASSO 2: LEITURA DE BATERIA (Secundário - GPIO 5)
            // =================================================================
            Serial.print("[LoRa] 2. Lendo Bateria... ");
            
            if (GpioRead(LORA_REMOTE_ID, GPIO5, &rawBatAdc) == MESH_OK) {
                Serial.println("OK!");
                // Cálculo: (ADC / 4095) * 3.3V * 2 (Divisor resistivo 100k/100k)
                float tensao = (rawBatAdc * 3.3f / 4095.0f) * 2.0f;
                sensorData.bateria_V = tensao;
            } else {
                Serial.println("Falha (Ignorado)");
                // Mantemos o valor anterior da bateria para não piscar "0V" na tela
            }

            // -----------------------------------------------------------
            // DELAY DE SEGURANÇA 2 (2 Segundos - CRÍTICO)
            // O "respiro" longo antes de mandar o comando manual de RSSI.
            // Isso evita o TIMEOUT na função updateRemoteRSSI.
            // -----------------------------------------------------------
            Serial.println("[LoRa] Aguardando rádio liberar para RSSI...");
            vTaskDelay(pdMS_TO_TICKS(2000)); 

            // Limpa buffer serial do ESP32 antes do comando manual
            while(hSerialCommands->available() > 0) hSerialCommands->read();

            // =================================================================
            // PASSO 3: LEITURA DE RSSI (Comando Manual 0x15)
            // =================================================================
            if (!updateRemoteRSSI()) {
                Serial.println("[LoRa] RSSI ignorado (Timeout ou Rádio Ocupado)");
            }

        } 
        else {
            // =================================================================
            // FALHA GERAL (O sensor não respondeu ao primeiro chamado)
            // =================================================================
            Serial.println("FALHA GERAL (Sem resposta do Nível)");
            netStats.lost_packets++;
            
            // Zera o nível para indicar visualmente que caiu a conexão
            sensorData.nivel_cm = 0;
            
            // OBS: Não zeramos bateria nem RSSI aqui para manter o gráfico estável
            // até a reconexão.
        }

        // 3. ATUALIZA CÁLCULO DE PERDA (%)
        if (netStats.tx_packets > 0) {
            netStats.packet_loss_pct = ((float)netStats.lost_packets / netStats.tx_packets) * 100.0f;
        }

        // Debug no Serial para acompanhamento
        Serial.printf("[Stats] Perda: %.2f%% | Nivel: %.0fcm | Bat: %.2fV | RSSI: %d\n", 
                      netStats.packet_loss_pct, 
                      sensorData.nivel_cm,
                      sensorData.bateria_V,
                      netStats.rssi_volta);

        // Aguarda próximo ciclo (15s para não congestionar a rede)
        vTaskDelay(pdMS_TO_TICKS(15000)); 
    }
}

// ==========================================
// INICIALIZAÇÃO DO SISTEMA
// ==========================================
bool initLoRaMesh() {
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
    xTaskCreatePinnedToCore(
        loraTask,
        "LoraTask",
        10000,
        NULL,
        1,
        &xLoraTaskHandle,
        0
    );
}