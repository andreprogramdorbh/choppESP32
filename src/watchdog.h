#pragma once
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: watchdog — Watchdog de Segurança Industrial
// ═══════════════════════════════════════════════════════════════════════════
//
// Responsabilidades:
//   - Monitorar o tempo sem comandos enquanto válvula está aberta
//   - Fechar a válvula automaticamente após PROTO_WATCHDOG_MS (5s) sem comando
//   - Monitorar desconexão BLE durante dispensação
//   - Resetar o timer a cada comando recebido
//   - Logar todos os eventos de segurança
//
// REGRAS DE SEGURANÇA:
//   1. Válvula NUNCA fica aberta por mais de PROTO_WATCHDOG_MS sem comando
//   2. Se BLE desconectar durante dispensação, aguarda 60s e fecha válvula
//   3. Qualquer erro no sistema fecha a válvula imediatamente
// ═══════════════════════════════════════════════════════════════════════════

// ── Inicialização ─────────────────────────────────────────────────────────
void watchdog_init();

// ── Reset do timer (chamar a cada comando recebido) ───────────────────────
void watchdog_kick();

// ── Tarefa FreeRTOS ───────────────────────────────────────────────────────
void taskWatchdog(void* param);
