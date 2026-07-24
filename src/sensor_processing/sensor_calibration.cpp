#include "sensor_calibration.h"

// ===== TABELA DE CALIBRAÇÃO DO SENSOR COM LEITURAS MANUAIS DE 0 A 20CM PASSO DE 0.5CM =====
static const uint16_t adcTable[] = {
     678,  733,  813,  867,  923,  960, 1019, 1052, 1107, 1149,
    1185, 1225, 1260, 1299, 1334, 1365, 1409, 1428, 1465, 1491,
    1526, 1553, 1577, 1604, 1627, 1657, 1683, 1702, 1727, 1748,
    1771, 1793, 1809, 1827, 1849, 1867, 1888, 1906, 1925, 1937,
    1969
};

static const float STEP_CM = 0.5f;
static const int TABLE_SIZE = sizeof(adcTable) / sizeof(adcTable[0]);
// =================================

//Função que faz interpolação linear dos valores de leitura com a tabela de calibração
float sensorAdcToCm(float adc) {

    if (adc <= adcTable[0])
        return 0.0f;

    if (adc >= adcTable[TABLE_SIZE - 1])
        return (TABLE_SIZE - 1) * STEP_CM;

    for (int i = 0; i < TABLE_SIZE - 1; i++) {
        if (adc >= adcTable[i] && adc <= adcTable[i + 1]) {

            float adc1 = adcTable[i];
            float adc2 = adcTable[i + 1];

            float cm1 = i * STEP_CM;
            float cm2 = (i + 1) * STEP_CM;

            return cm1 + (adc - adc1) * (cm2 - cm1) / (adc2 - adc1);
        }
    }

    return 0.0f;
}
