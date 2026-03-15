#include "flow_sensor.h"
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: flow_sensor.cpp
// ═══════════════════════════════════════════════════════════════════════════

// ── Estado interno (volatile para acesso seguro da ISR) ───────────────────
static volatile uint32_t s_pulsos       = 0;
static volatile uint64_t s_ultimoPulso  = 0;
static volatile bool     s_enabled      = false;
static volatile uint32_t s_pulsosLitro  = PULSO_LITRO;

// ── ISR — executada a cada pulso do sensor ────────────────────────────────
// IRAM_ATTR garante que a função fica na RAM interna (não na flash)
// para execução rápida e segura durante interrupções.
static void IRAM_ATTR fluxoISR() {
    if (!s_enabled) return;
    s_pulsos++;
    s_ultimoPulso = (uint64_t)esp_timer_get_time() / 1000ULL; // microseconds → ms
}

// ── Inicialização ─────────────────────────────────────────────────────────
void flowSensor_init() {
    s_pulsos      = 0;
    s_ultimoPulso = 0;
    s_enabled     = false;
    s_pulsosLitro = g_opState.pulsosLitro > 0 ? g_opState.pulsosLitro : PULSO_LITRO;

    pinMode(PINO_SENSOR_FLUSO, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PINO_SENSOR_FLUSO), fluxoISR, FALLING);

    DBG_PRINTF("[FLOW] Sensor inicializado | pino=%d | pulsos/litro=%u\n",
               PINO_SENSOR_FLUSO, s_pulsosLitro);
}

// ── Reset do contador ─────────────────────────────────────────────────────
void flowSensor_reset() {
    portDISABLE_INTERRUPTS();
    s_pulsos      = 0;
    s_ultimoPulso = (uint64_t)esp_timer_get_time() / 1000ULL;
    portENABLE_INTERRUPTS();
    DBG_PRINTLN("[FLOW] Contador resetado");
}

// ── Habilitar / Desabilitar ISR ───────────────────────────────────────────
void flowSensor_enable() {
    s_enabled = true;
    DBG_PRINTLN("[FLOW] ISR habilitada");
}

void flowSensor_disable() {
    s_enabled = false;
    DBG_PRINTLN("[FLOW] ISR desabilitada");
}

// ── Leitura thread-safe ───────────────────────────────────────────────────
uint32_t flowSensor_getPulsos() {
    portDISABLE_INTERRUPTS();
    uint32_t p = s_pulsos;
    portENABLE_INTERRUPTS();
    return p;
}

uint32_t flowSensor_getMl() {
    uint32_t p = flowSensor_getPulsos();
    if (s_pulsosLitro == 0) return 0;
    return (uint32_t)((uint64_t)p * 1000UL / s_pulsosLitro);
}

uint64_t flowSensor_getUltimoPulsoMs() {
    portDISABLE_INTERRUPTS();
    uint64_t t = s_ultimoPulso;
    portENABLE_INTERRUPTS();
    return t;
}

// ── Configuração ──────────────────────────────────────────────────────────
void flowSensor_setPulsosLitro(uint32_t pl) {
    if (pl == 0) {
        DBG_PRINTLN("[FLOW] ERRO: pulsosLitro não pode ser zero");
        return;
    }
    s_pulsosLitro = pl;
    g_opState.pulsosLitro = pl;
    DBG_PRINTF("[FLOW] Calibração atualizada: %u pulsos/litro\n", pl);
}

uint32_t flowSensor_calcularAlvo(uint32_t ml) {
    // alvo = (pulsosLitro / 1000) * ml
    return (uint32_t)((uint64_t)s_pulsosLitro * ml / 1000UL);
}
