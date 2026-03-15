#pragma once
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: flow_sensor — Sensor de Fluxo YF-S401
// ═══════════════════════════════════════════════════════════════════════════
//
// Responsabilidades:
//   - Configurar interrupção de hardware no pino do sensor
//   - Contar pulsos via ISR (Interrupt Service Routine)
//   - Converter pulsos em mililitros
//   - Expor contagem atômica para outros módulos
//
// Hardware: YF-S401
//   - Frequência: 5880 pulsos/litro (padrão, configurável via $PL)
//   - Tensão: 5V (com divisor resistivo para 3.3V no ESP32-C3)
//   - Pino: PINO_SENSOR_FLUSO (definido em config.h)
// ═══════════════════════════════════════════════════════════════════════════

// ── Inicialização ─────────────────────────────────────────────────────────
void flowSensor_init();

// ── Controle de contagem ──────────────────────────────────────────────────
void flowSensor_reset();                    // Zera o contador de pulsos
void flowSensor_enable();                   // Habilita a ISR
void flowSensor_disable();                  // Desabilita a ISR

// ── Leitura ───────────────────────────────────────────────────────────────
uint32_t flowSensor_getPulsos();            // Retorna pulsos contados (thread-safe)
uint32_t flowSensor_getMl();               // Converte pulsos em ml
uint64_t flowSensor_getUltimoPulsoMs();    // Timestamp do último pulso (para timeout)

// ── Configuração ──────────────────────────────────────────────────────────
void flowSensor_setPulsosLitro(uint32_t pl); // Atualiza calibração do sensor
uint32_t flowSensor_calcularAlvo(uint32_t ml); // Calcula pulsos necessários para N ml
