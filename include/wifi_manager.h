#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Definições
#define WIFI_TIMEOUT_MS 15000

// Protótipos
void wifiInit();
bool wifiCheckConnection();
String wifiGetIP();