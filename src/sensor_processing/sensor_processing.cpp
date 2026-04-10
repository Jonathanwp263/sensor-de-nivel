#include "sensor_processing.h"
#include "sensor_buffer.h"
#include "sensor_calibration.h"

void sensorProcessingInit() {
    sensorBufferInit();
}

float sensorProcess(uint16_t adcRaw) {

    // 1. Armazena leitura
    sensorBufferAdd(adcRaw);

    // 2. Calcula média
    float adcMean = sensorBufferMean();

    // 3. Converte para cm
    float levelCm = sensorAdcToCm(adcMean);

    // 4. Segurança para valores negativos
    if (levelCm < 0)
        levelCm = 0;

    return levelCm;
}
