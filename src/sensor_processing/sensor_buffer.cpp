#include "sensor_buffer.h"

#define BUFFER_SIZE 5

static uint16_t buffer[BUFFER_SIZE];
static uint8_t indexPos = 0;
static bool bufferFull = false;
static SemaphoreHandle_t bufferMutex = NULL;

void sensorBufferInit() {
    if (bufferMutex == NULL) {
        bufferMutex = xSemaphoreCreateMutex();
    }
    for (int i = 0; i < BUFFER_SIZE; i++)
        buffer[i] = 0;
    indexPos = 0;
    bufferFull = false;
}

void sensorBufferAdd(uint16_t adc) {
    if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE) {
        buffer[indexPos++] = adc;
        if (indexPos >= BUFFER_SIZE) {
            indexPos = 0;
            bufferFull = true;
        }
        xSemaphoreGive(bufferMutex);
    }
}

float sensorBufferMean() {
    float result = 0.0f;
    if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE) {
        uint32_t sum = 0;
        uint8_t count = bufferFull ? BUFFER_SIZE : indexPos;
        if (count > 0) {
            for (int i = 0; i < count; i++)
                sum += buffer[i];
            result = (float)sum / count;
        }
        xSemaphoreGive(bufferMutex);
    }
    return result;
}