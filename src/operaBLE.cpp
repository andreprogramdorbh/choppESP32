#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer *pServer = NULL;
    BLECharacteristic *pTxCharacteristic;
    bool deviceConnected = false;

    // Controla se o dispositivo Android foi autenticado via PIN
    // Resetado para false a cada nova conexao BLE
    bool bleAutenticado = false;

    class MyServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer *pServer) {
            digitalWrite( PINO_STATUS, LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Conectado"));
            deviceConnected = true;
            // Reseta autenticacao a cada nova conexao
            bleAutenticado = false;
            DBG_PRINT(F("\n[BLE] Aguardando autenticacao por PIN"));
        };

        void onDisconnect(BLEServer *pServer) {
            digitalWrite( PINO_STATUS, !LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Desconectado"));
            deviceConnected = false;
            bleAutenticado = false;
            delay(500);
            pServer->startAdvertising();
        }
    };

    class MyCallbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic *pCharacteristic) {
            String cmd = "";
            std::string rxValue = pCharacteristic->getValue();
            DBG_PRINT(F("\n[BLE] Recebido: "));
            if (rxValue.length() > 0) {
                for (int i = 0; i < rxValue.length(); i++) {
                    cmd += (char)rxValue[i];
                }
                cmd.trim();
                DBG_PRINT(cmd);

                // ---------------------------------------------------------
                // Verifica comando de autenticacao por PIN
                // Formato esperado do Android: $AUTH:259087
                // ---------------------------------------------------------
                if (cmd.startsWith("$") && cmd.substring(1, 6) == COMANDO_AUTH) {
                    String pinRecebido = cmd.substring(6);
                    pinRecebido.trim();
                    DBG_PRINT(F("\n[BLE] PIN recebido: "));
                    DBG_PRINT(pinRecebido);
                    if (pinRecebido == BLE_AUTH_PIN) {
                        bleAutenticado = true;
                        DBG_PRINT(F("\n[BLE] Autenticacao OK"));
                        enviaBLE("AUTH:OK");
                    } else {
                        bleAutenticado = false;
                        DBG_PRINT(F("\n[BLE] Autenticacao FALHOU - PIN incorreto"));
                        enviaBLE("AUTH:FAIL");
                    }
                    return;
                }

                // ---------------------------------------------------------
                // Bloqueia qualquer comando de operacao se nao autenticado
                // ---------------------------------------------------------
                if (!bleAutenticado) {
                    DBG_PRINT(F("\n[BLE] Comando bloqueado - dispositivo nao autenticado"));
                    enviaBLE("ERROR:NOT_AUTHENTICATED");
                    return;
                }

                // Dispositivo autenticado: processa o comando normalmente
                executaOperacao(cmd);
            }
        }
    };

void setupBLE() {

    // Create the BLE Device
    BLEDevice::init(BLE_NAME);

    // Create the BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create a BLE Characteristic
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Start the service
    pService->start();

    // Start advertising
    pServer->getAdvertising()->start();
    DBG_PRINT(F("\n[BLE] Aguardando conexao"));
}

void enviaBLE( String msg ) {
    msg += '\n';
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
}
#endif
