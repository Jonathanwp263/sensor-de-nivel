#pragma once
#include <Arduino.h>

// ==========================================
// DADOS DOS SENSORES (Aplicação)
// ==========================================
typedef struct {
    volatile float nivel_cm;
    volatile float bateria_V;
} SensorData;

// ==========================================
// DADOS DA REDE (Diagnóstico)
// ==========================================
typedef struct {
    volatile uint32_t tx_packets;      // Total de tentativas
    volatile uint32_t rx_packets;      // Total de sucessos
    volatile uint32_t lost_packets;    // Total de falhas
    volatile float packet_loss_pct;    // % de perda calculada
    volatile int rssi_volta;           // Sinal Sensor -> Gateway (O mais importante)
    volatile int rssi_ida;             // Sinal Gateway -> Sensor
} NetworkStats;

// ==========================================
// VARIÁVEIS GLOBAIS (EXTERN)
// ==========================================
extern SensorData sensorData;
extern NetworkStats netStats;
extern SemaphoreHandle_t sensorMutex;
