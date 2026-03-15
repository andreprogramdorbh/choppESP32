#pragma once
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: valve_controller — Controle da Válvula Solenoide
// ═══════════════════════════════════════════════════════════════════════════
//
// Responsabilidades:
//   - Abrir e fechar a válvula via relé
//   - Executar a tarefa de dispensação por volume (taskDispensacao)
//   - Enviar VP: (volume parcial) a cada PROTO_HEARTBEAT_INTERVAL_MS
//   - Enviar DONE quando o volume alvo for atingido
//   - Fechar a válvula em qualquer condição de erro
//
// Hardware:
//   - Pino: PINO_RELE (definido em config.h)
//   - Lógica: RELE_ON = LOW (relé ativo em nível baixo)
// ═══════════════════════════════════════════════════════════════════════════

// ── Inicialização ─────────────────────────────────────────────────────────
void valveController_init();

// ── Controle direto ───────────────────────────────────────────────────────
void valveController_open();        // Abre a válvula (RELE_ON)
void valveController_close();       // Fecha a válvula (RELE_OFF)
bool valveController_isOpen();      // Retorna true se a válvula está aberta

// ── Dispensação por volume ────────────────────────────────────────────────
// Inicia a dispensação de 'ml' mililitros.
// Retorna false se o sistema já está em operação (BUSY).
bool valveController_startDispensacao(uint32_t ml);

// ── Parada de emergência ──────────────────────────────────────────────────
// Para a dispensação imediatamente (comando $STOP ou watchdog).
void valveController_stop(const char* motivo);

// ── Tarefa FreeRTOS ───────────────────────────────────────────────────────
// Executada na Core 1 (APP_CPU) para não interferir com o BLE (Core 0).
void taskDispensacao(void* param);
