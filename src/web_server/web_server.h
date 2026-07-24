#pragma once

#include <ESPAsyncWebServer.h>

void initWebServer();

void handleUpload(
    AsyncWebServerRequest *request,
    String filename,
    size_t index,
    uint8_t *data,
    size_t len,
    bool final
);

extern const char* upload_html;