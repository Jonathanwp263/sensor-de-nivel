#include <Arduino.h>
#include <SPIFFS.h>
#include <LoRaMESH.h>
#include <HardwareSerial.h>

// ==========================================
// CONFIGURAÇÕES
// ==========================================
#define LORA_REMOTE_ID   1
#define SAMPLE_INTERVAL  700    // ms entre leituras LoRa
#define WINDOW_SIZE      8      // amostras para detecção de estabilidade
#define STABLE_RANGE     5      // variação máxima (max-min) para considerar estável
#define CAL_FILENAME     "/calibracao.txt"

// ==========================================
// ESTADO INTERNO
// ==========================================
static HardwareSerial* loraSerial = nullptr;
static uint16_t adcWindow[WINDOW_SIZE] = {0};
static uint8_t  winIdx   = 0;
static bool     winFull  = false;
static bool     notifiedStable = false;
static int      pointCount = 0;
static File     calFile;

// ==========================================
// JANELA DESLIZANTE
// ==========================================
static void windowAdd(uint16_t adc) {
    adcWindow[winIdx++] = adc;
    if (winIdx >= WINDOW_SIZE) {
        winIdx = 0;
        winFull = true;
    }
}

static float windowMean() {
    uint8_t n = winFull ? WINDOW_SIZE : winIdx;
    if (n == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += adcWindow[i];
    return (float)sum / n;
}

static bool windowIsStable() {
    if (!winFull) return false;
    uint16_t mn = adcWindow[0], mx = adcWindow[0];
    for (uint8_t i = 1; i < WINDOW_SIZE; i++) {
        if (adcWindow[i] < mn) mn = adcWindow[i];
        if (adcWindow[i] > mx) mx = adcWindow[i];
    }
    return (mx - mn) <= STABLE_RANGE;
}

static void windowReset() {
    winIdx  = 0;
    winFull = false;
}

// ==========================================
// VALIDAÇÃO DE ENTRADA
// ==========================================
static bool isNumeric(const String& s) {
    if (s.length() == 0) return false;
    bool hasDot = false;
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] == '.' && !hasDot) { hasDot = true; continue; }
        if (!isDigit(s[i])) return false;
    }
    return true;
}

// ==========================================
// IMPRESSÃO DO ARQUIVO NO SERIAL
// ==========================================
static void printFileContents() {
    calFile.flush();
    File f = SPIFFS.open(CAL_FILENAME, "r");
    if (!f) { Serial.println("[ERRO] Nao foi possivel ler o arquivo."); return; }
    Serial.println("\n--- Conteudo de " CAL_FILENAME " ---");
    while (f.available()) Serial.write(f.read());
    f.close();
    Serial.println("--- Fim ---\n");
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=================================");
    Serial.println("  MODO CALIBRACAO - Sensor Nivel");
    Serial.println("=================================");

    if (!SPIFFS.begin(true)) {
        Serial.println("[ERRO] Falha no SPIFFS. Parando.");
        while (true) delay(1000);
    }

    bool fileExisted = SPIFFS.exists(CAL_FILENAME);
    calFile = SPIFFS.open(CAL_FILENAME, "a");
    if (!calFile) {
        Serial.println("[ERRO] Falha ao abrir arquivo. Parando.");
        while (true) delay(1000);
    }
    if (!fileExisted) {
        calFile.println("# Calibracao Sensor de Nivel");
        calFile.println("# profundidade_cm,adc_medio");
    }
    Serial.printf("[OK] Arquivo: %s (%s)\n",
                  CAL_FILENAME, fileExisted ? "continuando" : "novo");

    Serial.println("[INFO] Inicializando LoRaMesh (aguarde ~5s)...");
    loraSerial = SerialCommandsInit(16, 17, 9600, 2);
    if (!loraSerial) {
        Serial.println("[ERRO] Falha ao iniciar serial LoRa. Parando.");
        while (true) delay(1000);
    }

    delay(5000);
    while (loraSerial->available()) loraSerial->read();

    uint16_t localId, localNet;
    uint32_t localUniqueId;
    if (LocalRead(&localId, &localNet, &localUniqueId) != MESH_OK) {
        Serial.println("[ERRO] Modulo LoRa nao responde. Parando.");
        while (true) delay(1000);
    }
    Serial.printf("[OK] LoRa local: ID=%d NET=%d\n", localId, localNet);

    Serial.print("[INFO] Configurando GPIO6 remoto... ");
    if (GpioConfig(LORA_REMOTE_ID, GPIO6, ANALOG_IN, PULL_OFF) == MESH_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FALHA (continuando mesmo assim)");
    }

    Serial.println("\n-----------------------------------------");
    Serial.println(" Posicione o sensor e aguarde [ESTAVEL].");
    Serial.println(" Digite a profundidade (ex: 5.5) + ENTER.");
    Serial.println(" VER  -> imprime o arquivo atual");
    Serial.println(" FIM  -> encerra e fecha o arquivo");
    Serial.println("-----------------------------------------\n");
}

// ==========================================
// LOOP
// ==========================================
void loop() {
    static uint32_t lastRead = 0;

    if (millis() - lastRead >= SAMPLE_INTERVAL) {
        lastRead = millis();

        uint16_t rawAdc = 0;
        if (GpioRead(LORA_REMOTE_ID, GPIO6, &rawAdc) == MESH_OK) {
            windowAdd(rawAdc);
            float mean   = windowMean();
            bool  stable = windowIsStable();

            Serial.printf("[ADC] %4u | Media: %6.1f | %s\n",
                          rawAdc, mean, stable ? "ESTAVEL" : "...");

            if (stable && !notifiedStable) {
                notifiedStable = true;
                Serial.printf("\n>>> ESTAVEL! ADC medio = %.0f\n", mean);
                Serial.print(">>> Profundidade (cm): ");
            } else if (!stable && notifiedStable) {
                notifiedStable = false;
                Serial.println("\n[AVISO] Leitura desestabilizou, aguardando...");
            }
        } else {
            Serial.println("[LoRa] Sem resposta do sensor...");
        }
    }

    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() == 0) return;

        if (input.equalsIgnoreCase("FIM")) {
            calFile.close();
            Serial.println("\n=================================");
            Serial.printf("[OK] %d ponto(s) salvos em %s\n", pointCount, CAL_FILENAME);
            Serial.println("[INFO] Grave o firmware normal para baixar o arquivo via web.");
            Serial.println("=================================");
            while (true) delay(1000);
        }

        if (input.equalsIgnoreCase("VER")) {
            printFileContents();
            return;
        }

        if (isNumeric(input)) {
            float depth  = input.toFloat();
            uint16_t adc = (uint16_t)windowMean();

            calFile.printf("%.1f,%u\n", depth, adc);
            calFile.flush();
            pointCount++;

            Serial.printf("[SALVO] #%d -> %.1f cm = ADC %u\n\n", pointCount, depth, adc);
            Serial.print(">>> Profundidade (cm): ");

            windowReset();
            notifiedStable = false;
        } else {
            Serial.println("[AVISO] Entrada invalida. Use um numero (ex: 5.5), VER ou FIM.");
        }
    }
}
