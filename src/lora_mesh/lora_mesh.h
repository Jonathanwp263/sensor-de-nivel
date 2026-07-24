#pragma once
#include <stdint.h>
#include "data_model.h"
#include <Arduino.h>

extern TaskHandle_t xLoraLevelTaskHandle;
extern TaskHandle_t xLoraDiagTaskHandle;

bool initLoRaMesh();
void startLoRaTask();
