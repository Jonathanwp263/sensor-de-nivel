#include "data_model.h"

// Inicialização explícita das leituras do sensor e bateria
SensorData sensorData = {
    .nivel_cm = 0.0,
    .bateria_V = 0.0
};

// Inicialização explícita das estatísticas de Rede (RSSI e Pacotes)
NetworkStats netStats = {
    .tx_packets = 0,
    .rx_packets = 0,
    .lost_packets = 0,
    .packet_loss_pct = 0.0,
    .rssi_volta = 0,
    .rssi_ida = 0
};

// Mutex criado em initLoRaMesh(), após o scheduler FreeRTOS estar ativo
SemaphoreHandle_t sensorMutex = NULL;
