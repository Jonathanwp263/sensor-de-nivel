#pragma once
#include <stdint.h>
#include "data_model.h"
#include <Arduino.h>

extern TaskHandle_t xLoraTaskHandle;

bool initLoRaMesh();
void startLoRaTask();
