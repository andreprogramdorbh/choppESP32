#include "ble_protocol.h"
#include "valve_controller.h"
#include "watchdog.h"
#include "command_history.h"
#include "command_queue.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: ble_protocol.cpp - CORRIGIDO PARA ESP32 BLE
// ═══════════════════════════════════════════════════════════════════════════

// ── Variáveis globais ────────────────────────────────────────────────────
SemaphoreHandle_t g_bleMutex = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

// ── Estado interno BLE ────────────────────────────────────────────────────
static BLEServer* s_pServer = nullptr;
static BLECharacteristic* s_pTxChar = nullptr;
static BLECharacteristic* s_pRxChar = nullptr;
static char s_deviceName[32] = {0};

// ── UUIDs customizados (128 bits) ─────────────────────────────────────────
#define CUSTOM_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define RX_CHAR_UUID "12345678-1234-1234-1234-123456789abd"
#define TX_CHAR_UUID "12345678-1234-1234-1234-123456789abe"

// ═══════════════════════════════════════════════════════════════════════════
// CALLBACKS DE SERVIDOR BLE
// ═══════════════════════════════════════════════════════════════════════════
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.println("DEVICE CONNECTED");
        // Adicione lógica adicional se necessário
    }

    void onDisconnect(BLEServer* pServer) override {
        Serial.println("DEVICE DISCONNECTED");
        // Reinicia advertising
        bleProtocol_startAdvertising();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// CALLBACKS DA CARACTERÍSTICA RX
// ═══════════════════════════════════════════════════════════════════════════
class MyRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.empty()) return;

        Serial.print("RX recebido: ");
        Serial.println(value.c_str());

        // Lógica de teste simples
        if (value == "PING") {
            bleProtocol_send("PONG");
        } else if (value == "SERVE|300|1234|SESSION1") {
            bleProtocol_send("ACK|1234");
            // Simula processamento assíncrono
            xTaskCreate([](void* param) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                bleProtocol_send("DONE|1234|300|SESSION1");
                vTaskDelete(NULL);
            }, "TestTask", 2048, NULL, 1, NULL);
        }

        // Enfileira para processamento normal se necessário
        cmdQueue_enqueue(value.c_str());
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// INICIALIZAÇÃO DO BLE
// ═══════════════════════════════════════════════════════════════════════════
void bleProtocol_init() {
    Serial.println("BLE INIT");

    // Cria mutex e fila
    g_bleMutex = xSemaphoreCreateMutex();
    g_cmdQueue = xQueueCreate(5, 256);

    // Gera nome do dispositivo
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_deviceName, sizeof(s_deviceName), "CHOPP_%02X%02X", mac[4], mac[5]);

    // Inicializa BLEDevice
    BLEDevice::init(s_deviceName);
    BLEDevice::setMTU(512);

    // Cria servidor BLE
    s_pServer = BLEDevice::createServer();
    s_pServer->setCallbacks(new MyServerCallbacks());

    // Cria serviço customizado
    BLEService* pService = s_pServer->createService(CUSTOM_SERVICE_UUID);

    // Característica TX (NOTIFY)
    s_pTxChar = pService->createCharacteristic(
        TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    s_pTxChar->addDescriptor(new BLE2902());

    // Característica RX (WRITE)
    s_pRxChar = pService->createCharacteristic(
        RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pRxChar->setCallbacks(new MyRxCallbacks());

    // Inicia o serviço
    pService->start();

    // Configura advertising
    bleProtocol_startAdvertising();
}

// ── Envio thread-safe ────────────────────────────────────────────────────
void bleProtocol_send(const char* data) {
    if (!s_pTxChar) return;

    Serial.print("TX enviado: ");
    Serial.println(data);

    if (xSemaphoreTake(g_bleMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_pTxChar->setValue((uint8_t*)data, strlen(data));
        s_pTxChar->notify();
        xSemaphoreGive(g_bleMutex);
    }
}

// ── Inicia advertising ───────────────────────────────────────────────────
void bleProtocol_startAdvertising() {
    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(CUSTOM_SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    pAdv->setMaxPreferred(0x06);
    pAdv->start();

    Serial.println("BLE ADVERTISING");
}

// ── Nome do dispositivo ───────────────────────────────────────────────────
const char* bleProtocol_getDeviceName() {
    return s_deviceName;
}