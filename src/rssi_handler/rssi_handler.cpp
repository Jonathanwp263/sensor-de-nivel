#include "rssi_handler.h"

SignalQuality calculateSignalQuality(int rssi) {
    SignalQuality q;
    q.rssi_dbm = rssi;

    // Se o RSSI for 0 (erro de leitura) ou positivo (impossível em LoRa real)
    // considera desconectado.
    if (rssi >= 0) {
        q.rssi_dbm = 0; 
        q.percentage = 0;
        q.status = "Sem Sinal";
        q.cssClass = "signal-dead"; // Vermelho
        return q;
    }

    // --- CÁLCULO DA PORCENTAGEM ---
    // Mapeia -120dBm (0%) até -50dBm (100%)
    int pct = map(rssi, -120, -50, 0, 100);
    q.percentage = constrain(pct, 0, 100);

    // --- CLASSIFICAÇÃO ---
    if (rssi >= -65) {
        q.status = "Excelente";
        q.cssClass = "signal-excellent";
    } else if (rssi >= -85) {
        q.status = "Bom";
        q.cssClass = "signal-good";
    } else if (rssi >= -100) {
        q.status = "Regular";
        q.cssClass = "signal-fair";
    } else if (rssi >= -115) {
        q.status = "Fraco";
        q.cssClass = "signal-weak";
    } else {
        q.status = "Crítico";
        q.cssClass = "signal-dead";
    }

    return q;
}