#pragma once
#include <stdint.h>
#include <Arduino.h>

void sensorBufferInit();
void sensorBufferAdd(uint16_t adc);
float sensorBufferMean();