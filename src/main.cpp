#include "config.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "operacional.h"
#include "operaPagina.h"
#include "operaBLE.h"
#include "operaRFID.h"

config_t configuracao = {0};

xQueueHandle listaLiberarML;

TaskHandle_t taskRFIDHandle = NULL;

void setup() {
    // Desabilita brownout detector (evita resets por queda de tensão momentânea)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Configura pino de status (LED)
    pinMode(PINO_STATUS, OUTPUT);
    digitalWrite(PINO_STATUS, !LED_STATUS_ON);

    pinMode( PINO_SENSOR_FLUSO, INPUT);
    
    // Inicia porta para debug
    #ifdef debug_debug
        Serial.begin(115200);
        delay(3000);
        unsigned long tF = millis() + 5000UL;
        while ((!Serial)&&(millis() < tF)){
            yield();
        }
        Serial.println();
        Serial.println(F("[SETUP] Iniciando Maquina")); 
    #endif

    // Efetua a leitura da configuração gravada na EEPROM
    leConfiguracao();
        
    #ifdef USAR_ESP32_UART_BLE
        setupBLE();
    #endif

    listaLiberarML = xQueueCreate(1,sizeof(uint32_t));

    #ifdef USAR_RFID
        xTaskCreate(taskRFID, "taskRFID", 4096, NULL, 3, &taskRFIDHandle);
    #endif

    // FIX STATUS=8: taskLiberaML movida para Core 1 (APP_CPU_NUM).
    // O BLE Bluedroid roda na Core 0 (PRO_CPU). Quando taskLiberaML rodava
    // tambem na Core 0, o loop while() com vTaskDelay(50ms) impedia que o
    // stack BLE processasse os LL keep-alive packets com prioridade suficiente,
    // causando o Connection Supervision Timeout (status=8) no Android.
    // Com taskLiberaML na Core 1, o BLE tem a Core 0 exclusivamente.
    xTaskCreatePinnedToCore(taskLiberaML, "taskLiberaML", 8192, NULL, 3, NULL, 1);

}

void loop() {
    // Heartbeat LED
    digitalWrite(PINO_STATUS, LED_STATUS_ON);
    vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(PINO_STATUS, !LED_STATUS_ON);
    vTaskDelay(pdMS_TO_TICKS(1900));
}
