#include "sensor_calibration.h"

// ===== TABELA DE CALIBRAÇÃO DO SENSOR COM LEITURAS MANUAIS DE 0 A 20CM PASSO DE 0.5CM =====
static const uint16_t adcTable[] = {
    343, 353, 365, 378, 393, 409, 427, 441, 457, 473,
    491, 501, 517, 529, 545, 559, 569, 577, 587, 597,
    609, 619, 627, 633, 643, 653, 657, 663, 668, 678,
    681, 685, 689, 697, 703, 707, 713, 717, 721, 729,
    737
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
