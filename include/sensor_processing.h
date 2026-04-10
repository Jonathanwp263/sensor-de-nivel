#pragma once
#include <stdint.h>

void sensorProcessingInit();
float sensorProcess(uint16_t adcRaw);
