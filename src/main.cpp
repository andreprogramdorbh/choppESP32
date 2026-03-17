#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "protocol.h"
#include "ble_protocol.h"
#include "command_parser.h"
#include "valve_controller.h"
#include "flow_sensor.h"
#include "watchdog.h"
#include <Preferences.h>

// ═══════════════════════════════════════════════════════════════════════════
// CHOPP ESP32 — FIRMWARE INDUSTRIAL v2.0
// Protocolo BLE Industrial — Arquitetura Modular FreeRTOS
// ═══════════════════════════════════════════════════════════════════════════
//
// ALOCAÇÃO DE CORES:
//   Core 0 (PRO_CPU): BLE Bluedroid stack + taskCommandProcessor
//   Core 1 (APP_CPU): taskDispensacao + taskWatchdog
//
// TAREFAS FREERTOS:
//   taskWatchdog         — Core 1 — Prioridade 4 (segurança máxima)
//   taskDispensacao      — Core 1 — Prioridade 2
//   taskCommandProcessor — Core 0 — Prioridade 3 (junto com BLE)
// ═══════════════════════════════════════════════════════════════════════════

// ── Instância global de estado (declarada em protocol.h) ──────────────────
OperationState g_opState = {
    .state           = SYS_IDLE,
    .mlSolicitado    = 0,
    .mlLiberado      = 0,
    .pulsosAlvo      = 0,
    .pulsosContados  = 0,
    .bleAutenticado  = false,
    .bleConectado    = false,
    .pulsosLitro     = PULSO_LITRO,
    .timeoutSensor   = TIMER_OUT_SENSOR,
    .ultimoComandoMs = 0,
    .ultimoPulsoMs   = 0,
};

// ── Mutex do estado da operação [v2.3 FIX-3] ─────────────────────────────
// Protege sessionId e currentCmdId contra race condition
// entre taskCommandProcessor (Core 0) e taskDispensacao (Core 1)
SemaphoreHandle_t g_opStateMutex = nullptr;

// ── Preferences para persistência de configuração ─────────────────────────
static Preferences prefs;

// ── Carrega configurações da Preferences (NVS) ────────────────────────────
static void carregarConfiguracoes() {
    prefs.begin("chopp", true); // read-only

    uint16_t magic = prefs.getUShort("magic", 0);
    if (magic == MAGIC_EEPROM) {
        g_opState.pulsosLitro   = prefs.getUInt("pulsosLitro",   PULSO_LITRO);
        g_opState.timeoutSensor = prefs.getUInt("timeoutSensor", TIMER_OUT_SENSOR);
        DBG_PRINTF("[MAIN] Config carregada | pulsosLitro=%u | timeout=%u ms\n",
                   g_opState.pulsosLitro, g_opState.timeoutSensor);
    } else {
        DBG_PRINTLN("[MAIN] Primeira inicialização — gravando valores padrão");
        prefs.end();
        prefs.begin("chopp", false); // read-write
        prefs.putUShort("magic",        MAGIC_EEPROM);
        prefs.putUInt("pulsosLitro",    PULSO_LITRO);
        prefs.putUInt("timeoutSensor",  TIMER_OUT_SENSOR);
    }
    prefs.end();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    // Desabilita brownout detector (evita resets por queda de tensão momentânea)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Configura pino de status (LED)
    pinMode(PINO_STATUS, OUTPUT);
    digitalWrite(PINO_STATUS, !LED_STATUS_ON);

    Serial.begin(115200);
    delay(500);

    DBG_PRINTLN("\n");
    DBG_PRINTLN("╔══════════════════════════════════════════╗");
    DBG_PRINTLN("║  CHOPP ESP32 — PROTOCOLO BLE INDUSTRIAL  ║");
    DBG_PRINTLN("║  Firmware v2.0 — Modular FreeRTOS        ║");
    DBG_PRINTLN("╚══════════════════════════════════════════╝");

    // ── Carrega configurações persistidas ─────────────────────────────────
    carregarConfiguracoes();

    // ── [v2.3 FIX-3] Cria mutex de estado ANTES de qualquer tarefa ────────
    // Protege sessionId e currentCmdId contra race condition entre cores
    g_opStateMutex = xSemaphoreCreateMutex();
    configASSERT(g_opStateMutex != nullptr);
    DBG_PRINTLN("[MAIN] g_opStateMutex criado (sessionId/currentCmdId thread-safe)");

    // ── Inicializa módulos de hardware ────────────────────────────────────
    // ORDEM IMPORTANTE: válvula PRIMEIRO para garantir estado seguro
    valveController_init();   // Fecha a válvula imediatamente
    flowSensor_init();
    watchdog_init();
    commandParser_init();

    // ── Inicializa BLE ────────────────────────────────────────────────────
    // bleProtocol_init() cria internamente g_bleMutex e g_cmdQueue
    bleProtocol_init();

    // ── Cria tarefas FreeRTOS ─────────────────────────────────────────────

    // Watchdog — Core 1 — Prioridade 4 (mais alta para garantir segurança)
    xTaskCreatePinnedToCore(
        taskWatchdog,           // Função da tarefa
        "WDG",                  // Nome (debug)
        2048,                   // Stack em bytes
        nullptr,                // Parâmetro
        4,                      // Prioridade (0=mais baixa, 5=mais alta)
        nullptr,                // Handle (não necessário)
        1                       // Core 1 (APP_CPU)
    );

    // Dispensação — Core 1 — Prioridade 2
    xTaskCreatePinnedToCore(
        taskDispensacao,
        "DISP",
        4096,
        nullptr,
        2,
        nullptr,
        1                       // Core 1 (APP_CPU)
    );

    // Processador de comandos — Core 0 — Prioridade 3
    // Fica na mesma core do BLE para garantir ACK em < 100ms
    xTaskCreatePinnedToCore(
        taskCommandProcessor,
        "CMD",
        4096,
        nullptr,
        3,
        nullptr,
        0                       // Core 0 (PRO_CPU) — junto com BLE Bluedroid
    );

    // LED de status: pisca 3x para indicar inicialização bem-sucedida
    for (int i = 0; i < 3; i++) {
        digitalWrite(PINO_STATUS, LED_STATUS_ON);
        delay(100);
        digitalWrite(PINO_STATUS, !LED_STATUS_ON);
        delay(100);
    }

    DBG_PRINTF("[MAIN] Sistema pronto | Nome BLE: %s | PIN: %s\n",
               bleProtocol_getDeviceName(), BLE_AUTH_PIN);
    DBG_PRINTLN("[MAIN] Aguardando conexão Android...\n");
}

// ── Loop principal ────────────────────────────────────────────────────────
// O FreeRTOS scheduler gerencia todas as tarefas.
// O loop() apenas pisca o LED de status a cada 2s para indicar que o
// sistema está vivo (heartbeat visual).
void loop() {
    // Heartbeat LED
    digitalWrite(PINO_STATUS, LED_STATUS_ON);
    vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(PINO_STATUS, !LED_STATUS_ON);
    vTaskDelay(pdMS_TO_TICKS(1900));
}
