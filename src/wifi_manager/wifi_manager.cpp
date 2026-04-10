#include "wifi_manager.h"

// Credenciais isoladas aqui
static const char* WIFI_SSID = "Sala_Andreia";
static const char* WIFI_PASS = "9902125711";

void wifiInit() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool wifiCheckConnection() {
    return (WiFi.status() == WL_CONNECTED);
}

String wifiGetIP() {
    return WiFi.localIP().toString();
}