#pragma once

#include <Arduino.h>

// Estrutura para facilitar o envio pro HTML
struct SignalQuality {
    int rssi_dbm;       // Valor em dBm (ex: -95)
    int percentage;     // 0 a 100%
    String status;      // "Excelente", "Bom", "Ruim"
    String cssClass;    // Para cor no HTML
};

// Função que traduz o número técnico para humano
SignalQuality calculateSignalQuality(int rssi);