#include "Arduino.h"

#ifndef _CONFIG_H_
    #define _CONFIG_H_   

    // Definições para debug via porta serial (Serial Monitor)
    #define debug_debug

    #define PULSO_LITRO 5880 //1L = 5880 square waves
    #define RELE_ON     LOW
    #define SENSOR_ON   LOW
    // FIX: Timeout aumentado de 2000ms para 30000ms (30s).
    // Necessário para que a válvula permaneça aberta durante reconexão BLE.
    // O Android leva até 15s de backoff + bond + discoverServices para reconectar.
    #define TIMER_OUT_SENSOR 30000LL

    #define COMANDO_ML "ML:" // Libera quantidade de ML
    #define COMANDO_PL "PL:" // Configura pulso por litro no sensor de fluxo
    #define COMANDO_ID "ID:" // Tag rfid lida
    #define COMANDO_LB "LB:" // Aciona liberação continua
    #define COMANDO_VZ "VZ:" // Vazão
    #define COMANDO_QP "QP:" // Quantidade de pulsos
    #define COMANDO_RI "RI:" // Registra id RFID do administrador (para remover basta gravar uma zerada)
    #define COMANDO_VP "VP:" // Volume parcial
    #define COMANDO_TO   "TO:"   // Configura timeout, tempo aguardando inicio do fluxo
    
    // Habilita modulos para compilação
    #define USAR_ESP32_UART_BLE
    //#define USAR_PAGINA_CONFIG
    #define USAR_RFID
    
    // pinout
    #ifdef ARDUINO_ESP32S3_DEV 
        #define PINO_RELE           48
        #define PINO_STATUS         21 //47
        #define PINO_SENSOR_FLUSO   2

        #define PINO_RC522_SSEL 14
        #define PINO_RC522_RSET 13
        #define PINO_RC522_MOSI 36
        #define PINO_RC522_MISO 37
        #define PINO_RC522_SCLK 35
    #elif defined(ARDUINO_ESP32_DEV)
        #define PINO_SENSOR_FLUSO   17
        #define PINO_RELE           16
        #define PINO_STATUS         2

        #define PINO_RC522_SSEL 5
        #define PINO_RC522_RSET 4
        #define PINO_RC522_MOSI 23
        #define PINO_RC522_MISO 19
        #define PINO_RC522_SCLK 18

        #define LED_STATUS_ON HIGH
    #else
        #define PINO_RC522_SSEL 7
        #define PINO_RC522_RSET 3
        #define PINO_RC522_MOSI 6
        #define PINO_RC522_MISO 5
        #define PINO_RC522_SCLK 4

        #define PINO_SENSOR_FLUSO   0
        #define PINO_RELE           1
        #define PINO_STATUS         8
        #define LED_STATUS_ON LOW
    #endif

    // Nome BLE do dispositivo
    // IMPORTANTE: deve iniciar com prefixo "CHOPP_" seguido do ID unico da unidade.
    // O app Android aceita apenas dispositivos com este prefixo (ex: CHOPP_E123, CHOPP_F45A).
    // Substitua 0001 pelo identificador unico de cada unidade (ex: ultimos 4 digitos do MAC).
    #define BLE_NAME "CHOPP_0001"

    // PIN de autenticacao BLE
    // Enviado pelo app Android apos conexao GATT para validar o acesso.
    // O ESP32 compara o PIN recebido via comando $AUTH:<pin> com este valor.
    #define BLE_AUTH_PIN "259087"

    // Comando de autenticacao BLE
    #define COMANDO_AUTH "AUTH:"  // Autenticacao por PIN

    // Flag para identificar se os dados foram gravados na EEPROM
    #define MAGIC_FLAG_EEPROM 0xF2F2  
    
    // Dados para o modo AP (Access Point)
    #define AP_SSID     "CHOPPE"
    #define AP_PASSWORD "1234567890"
        
    // Configuração apenas para o período de desenvolvimetno
    #ifndef WIFI_DEFAULT_SSID
        #define WIFI_DEFAULT_SSID "brisa-448561"
        //#define WIFI_DEFAULT_SSID "ridimuim"
    #endif    
    #ifndef WIFI_DEFAULT_PSW
        #define WIFI_DEFAULT_PSW "9xmkuiw1"
        //#define WIFI_DEFAULT_PSW "88999448494"
    #endif    
   
    // Estrutura da variável de configuração
    typedef struct {
        uint16_t magicFlag;
        uint8_t modoAP;
        char wifiSSID[30];
        char wifiPass[30];
        char rfidMaster[12];
        uint32_t pulsosLitro;
        uint32_t timeOut;
    } __attribute__ ((packed)) config_t;

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