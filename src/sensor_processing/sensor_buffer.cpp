#include "sensor_buffer.h"

#define BUFFER_SIZE 5

static uint16_t buffer[BUFFER_SIZE];
static uint8_t indexPos = 0;
static bool bufferFull = false;

//Inicia buffer de leituras do sensor, zerando o mesmo
void sensorBufferInit() {
    for (int i = 0; i < BUFFER_SIZE; i++)
        buffer[i] = 0;
    indexPos = 0;
    bufferFull = false;
}

//Carrega o buffer com "BUFFER_SIZE" leituras
void sensorBufferAdd(uint16_t adc) {
    buffer[indexPos++] = adc;

    if (indexPos >= BUFFER_SIZE) {
        indexPos = 0;
        bufferFull = true;
    }
}

//Faz a média móvel dos valores do buffer
float sensorBufferMean() {
    uint32_t sum = 0;
    uint8_t count = bufferFull ? BUFFER_SIZE : indexPos;

    if (count == 0)
        return 0;

    for (int i = 0; i < count; i++)
        sum += buffer[i];

    return (float)sum / count;
}
