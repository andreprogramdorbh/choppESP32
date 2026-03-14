#include "Arduino.h"

#ifndef _CONFIG_H_
    #define _CONFIG_H_   

    // ─────────────────────────────────────────────────────────────────────────
    // DEBUG
    // ─────────────────────────────────────────────────────────────────────────
    #define debug_debug

    // ─────────────────────────────────────────────────────────────────────────
    // SENSOR DE FLUXO — YF-S401
    // 1 litro = 5880 pulsos (square waves)
    // ─────────────────────────────────────────────────────────────────────────
    #define PULSO_LITRO 5880

    // ─────────────────────────────────────────────────────────────────────────
    // VÁLVULA / RELÉ
    // ─────────────────────────────────────────────────────────────────────────
    #define RELE_ON    LOW
    #define SENSOR_ON  LOW

    // ─────────────────────────────────────────────────────────────────────────
    // TIMEOUT DO SENSOR DE FLUXO
    //
    // Tempo máximo (ms) sem pulsos antes de fechar a válvula.
    //
    // IMPORTANTE: Este valor deve ser MAIOR que o tempo máximo de reconexão BLE.
    // O Android leva até ~15s para reconectar (backoff + bond + discoverServices).
    // Com 30s, a válvula permanece aberta durante toda a reconexão.
    //
    // Se o BLE estiver desconectado, o firmware aguarda até BLE_RECONEXAO_TIMEOUT_MS
    // (60s) antes de fechar a válvula definitivamente.
    // ─────────────────────────────────────────────────────────────────────────
    #define TIMER_OUT_SENSOR 30000LL

    // ─────────────────────────────────────────────────────────────────────────
    // COMANDOS BLE (prefixo dos comandos enviados pelo Android)
    // ─────────────────────────────────────────────────────────────────────────
    #define COMANDO_ML   "ML:"   // Libera quantidade de ML (ex: $ML:300)
    #define COMANDO_PL   "PL:"   // Configura pulso por litro no sensor de fluxo
    #define COMANDO_ID   "ID:"   // Tag RFID lida
    #define COMANDO_LB   "LB:"   // Aciona liberação contínua (sem limite de ML)
    #define COMANDO_VZ   "VZ:"   // Vazão
    #define COMANDO_QP   "QP:"   // Quantidade de pulsos
    #define COMANDO_RI   "RI:"   // Registra ID RFID do administrador
    #define COMANDO_VP   "VP:"   // Volume parcial (enviado pelo ESP32 durante dispensação)
    #define COMANDO_TO   "TO:"   // Configura timeout do sensor de fluxo
    #define COMANDO_AUTH "AUTH:" // Autenticação por PIN (ex: $AUTH:259087)

    // ─────────────────────────────────────────────────────────────────────────
    // SEGURANÇA BLE — PIN DE AUTENTICAÇÃO
    //
    // Usado em DUAS camadas de segurança:
    //
    // CAMADA 1 — Bond/Pairing nativo (BLESecurity::setStaticPIN):
    //   Qualquer dispositivo que tente parear precisa confirmar este PIN.
    //   Impede que dispositivos desconhecidos se conectem.
    //
    // CAMADA 2 — Autenticação por comando ($AUTH:259087):
    //   Após conectar, o Android envia $AUTH:259087 via característica BLE.
    //   O ESP32 só processa comandos de operação após validar este PIN.
    //   Impede que apps não autorizados operem o dispensador mesmo após parear.
    //
    // PARA ALTERAR O PIN: mude apenas este define e recompile o firmware.
    // O Android (BluetoothService.java) usa a constante ESP32_PIN = "259087".
    // ─────────────────────────────────────────────────────────────────────────
    #define BLE_AUTH_PIN "259087"

    // ─────────────────────────────────────────────────────────────────────────
    // NOME BLE — GERADO DINAMICAMENTE EM setupBLE()
    //
    // O nome NÃO é definido aqui. Em setupBLE() (operaBLE.cpp), o firmware lê
    // o MAC BLE da placa via esp_read_mac(mac, ESP_MAC_BT) e gera o nome:
    //
    //   CHOPP_XXXX  onde XXXX = 4 últimos dígitos hexadecimais do MAC BLE
    //
    // Exemplos:
    //   MAC BLE = AA:BB:CC:DD:EE:F1  →  CHOPP_EEF1
    //   MAC BLE = AA:BB:CC:DD:12:34  →  CHOPP_1234
    //   MAC BLE = AA:BB:CC:DD:AB:CD  →  CHOPP_ABCD
    //
    // VANTAGEM: O mesmo firmware pode ser gravado em todas as placas.
    // Cada unidade terá automaticamente um nome único baseado no seu MAC.
    //
    // O Android (Bluetooth2.java) filtra por startsWith("CHOPP_") no scan BLE.
    // Após o primeiro bond, o Android conecta diretamente pelo MAC (sem scan).
    // ─────────────────────────────────────────────────────────────────────────
    // (sem define BLE_NAME — nome gerado em runtime)

    // ─────────────────────────────────────────────────────────────────────────
    // MÓDULOS HABILITADOS
    // ─────────────────────────────────────────────────────────────────────────
    #define USAR_ESP32_UART_BLE   // BLE (Nordic UART Service)
    //#define USAR_PAGINA_CONFIG  // Página web de configuração (WiFi) — desabilitado
    #define USAR_RFID             // Leitor RFID RC522

    // ─────────────────────────────────────────────────────────────────────────
    // PINOUT — Mapeamento de pinos por placa
    // ─────────────────────────────────────────────────────────────────────────
    #ifdef ARDUINO_ESP32S3_DEV 
        #define PINO_RELE           48
        #define PINO_STATUS         21
        #define PINO_SENSOR_FLUSO   2
        #define PINO_RC522_SSEL     14
        #define PINO_RC522_RSET     13
        #define PINO_RC522_MOSI     36
        #define PINO_RC522_MISO     37
        #define PINO_RC522_SCLK     35
    #elif defined(ARDUINO_ESP32_DEV)
        #define PINO_SENSOR_FLUSO   17
        #define PINO_RELE           16
        #define PINO_STATUS         2
        #define PINO_RC522_SSEL     5
        #define PINO_RC522_RSET     4
        #define PINO_RC522_MOSI     23
        #define PINO_RC522_MISO     19
        #define PINO_RC522_SCLK     18
        #define LED_STATUS_ON HIGH
    #else
        // Lolin C3 Mini (padrão)
        #define PINO_RC522_SSEL     7
        #define PINO_RC522_RSET     3
        #define PINO_RC522_MOSI     6
        #define PINO_RC522_MISO     5
        #define PINO_RC522_SCLK     4
        #define PINO_SENSOR_FLUSO   0
        #define PINO_RELE           1
        #define PINO_STATUS         8
        #define LED_STATUS_ON LOW
    #endif

    // ─────────────────────────────────────────────────────────────────────────
    // EEPROM — Magic flag para identificar configuração gravada
    // ─────────────────────────────────────────────────────────────────────────
    #define MAGIC_FLAG_EEPROM 0xF2F2  

    // ─────────────────────────────────────────────────────────────────────────
    // WiFi (modo AP e credenciais padrão de desenvolvimento)
    // ─────────────────────────────────────────────────────────────────────────
    #define AP_SSID     "CHOPPE"
    #define AP_PASSWORD "1234567890"
        
    #ifndef WIFI_DEFAULT_SSID
        #define WIFI_DEFAULT_SSID "brisa-448561"
    #endif    
    #ifndef WIFI_DEFAULT_PSW
        #define WIFI_DEFAULT_PSW "9xmkuiw1"
    #endif    
   
    // ─────────────────────────────────────────────────────────────────────────
    // ESTRUTURA DE CONFIGURAÇÃO (gravada na EEPROM)
    // ─────────────────────────────────────────────────────────────────────────
    typedef struct {
        uint16_t magicFlag;
        uint8_t  modoAP;
        char     wifiSSID[30];
        char     wifiPass[30];
        char     rfidMaster[12];
        uint32_t pulsosLitro;
        uint32_t timeOut;
    } __attribute__ ((packed)) config_t;

    // ─────────────────────────────────────────────────────────────────────────
    // MACROS DE DEBUG
    // ─────────────────────────────────────────────────────────────────────────
    #ifdef debug_debug
        #define DBG_WRITE(...)    Serial.write(__VA_ARGS__)
        #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
        #define DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
        #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
    #else
        #define DBG_WRITE(...)
        #define DBG_PRINT(...)
        #define DBG_PRINTF(...)
        #define DBG_PRINTLN(...)
    #endif
    
#endif
