#pragma once
#include <stdint.h>

void sensorBufferInit();
void sensorBufferAdd(uint16_t adc);
float sensorBufferMean();