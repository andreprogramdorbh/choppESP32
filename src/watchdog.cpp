#include "watchdog.h"
#include "valve_controller.h"
#include "ble_protocol.h"
#include "event_log.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: watchdog.cpp — v2.2 (Watchdog Industrial 24h)
// ═══════════════════════════════════════════════════════════════════════════
//
// WATCHDOGS IMPLEMENTADOS:
//
// [WDG-1] PING Watchdog (5s):
//   Se a válvula estiver aberta e o BLE conectado, mas não receber nenhum
//   dado do Android por mais de PROTO_WATCHDOG_MS (5s), fecha a válvula.
//   Reseta via watchdog_kick() — chamado em onWrite() e handlePing().
//
// [WDG-2] BLE Reconnect Watchdog (60s):
//   Se a válvula estiver aberta e o BLE desconectar, aguarda até
//   WDG_BLE_RECONEXAO_TIMEOUT_MS (60s) pela reconexão.
//   Se não reconectar, fecha a válvula com segurança.
//
// [WDG-3] BLE Advertising Watchdog (30s) — NOVO v2.2:
//   Se o ESP32 ficar mais de PROTO_BLE_WATCHDOG_MS (30s) sem nenhuma
//   conexão BLE (bleConectado=false), reinicia o advertising automaticamente.
//   Registra o evento "ble_watchdog_trigger" no event_log.
//   Garante operação 24h sem necessidade de reinicialização manual.
//
// [WDG-4] Estado Inconsistente (2s):
//   Se o estado for SYS_RUNNING mas a válvula estiver fechada por mais
//   de 2s, reseta o estado para SYS_IDLE.
// ═══════════════════════════════════════════════════════════════════════════

// ── Timeout de reconexão BLE durante dispensação ─────────────────────────
#define WDG_BLE_RECONEXAO_TIMEOUT_MS  60000UL

// ── Inicialização ─────────────────────────────────────────────────────────
void watchdog_init() {
    g_opState.ultimoComandoMs = (uint64_t)esp_timer_get_time() / 1000ULL;
    DBG_PRINTLN("[WDG] Watchdog v2.2 inicializado (PING=5s, BLE_RECONEXAO=60s, ADV_RESTART=30s)");
}

// ── Reset do timer de PING ────────────────────────────────────────────────
void watchdog_kick() {
    g_opState.ultimoComandoMs = (uint64_t)esp_timer_get_time() / 1000ULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// TAREFA FREERTOS: taskWatchdog
// Core 1 (APP_CPU) — Prioridade 4 (máxima segurança)
// Ciclo: 500ms
// ═══════════════════════════════════════════════════════════════════════════
void taskWatchdog(void* param) {
    // Timestamps de controle
    uint64_t bleDesconectadoEm   = 0;  // Quando o BLE desconectou durante dispensação
    uint64_t semConexaoDesde     = 0;  // Quando o BLE ficou sem nenhuma conexão
    uint64_t estadoInconsistenteEm = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));

        uint64_t agora = (uint64_t)esp_timer_get_time() / 1000ULL;

        // ══════════════════════════════════════════════════════════════════
        // [WDG-3] BLE Advertising Watchdog — 30s sem conexão → restart ADV
        // ══════════════════════════════════════════════════════════════════
        if (!g_opState.bleConectado) {
            if (semConexaoDesde == 0) {
                semConexaoDesde = agora;
            } else {
                uint64_t tempoSemConexao = agora - semConexaoDesde;
                if (tempoSemConexao >= PROTO_BLE_WATCHDOG_MS) {
                    DBG_PRINTF("[WDG-3] BLE Advertising Watchdog: %u s sem conexão — reiniciando advertising\n",
                               (uint32_t)(tempoSemConexao / 1000));
                    eventLog_record("ble_watchdog_trigger|adv_restart");
                    bleProtocol_startAdvertising();
                    semConexaoDesde = agora; // Reseta para evitar restart contínuo
                }
            }
        } else {
            // BLE conectado — reseta o contador de tempo sem conexão
            semConexaoDesde = 0;
        }

        // ══════════════════════════════════════════════════════════════════
        // [WDG-1] e [WDG-2] — Só aplicam quando a válvula está aberta
        // ══════════════════════════════════════════════════════════════════
        if (valveController_isOpen() && g_opState.state == SYS_RUNNING) {

            // ── [WDG-2] BLE desconectado durante dispensação ──────────────
            if (!g_opState.bleConectado) {
                if (bleDesconectadoEm == 0) {
                    bleDesconectadoEm = agora;
                    DBG_PRINTF("[WDG-2] BLE desconectado durante dispensação — aguardando reconexão por %u s\n",
                               WDG_BLE_RECONEXAO_TIMEOUT_MS / 1000);
                    eventLog_record("ble_disconnected|during_serve");
                }

                uint64_t tempoDesconectado = agora - bleDesconectadoEm;
                if (tempoDesconectado >= WDG_BLE_RECONEXAO_TIMEOUT_MS) {
                    DBG_PRINTF("[WDG-2] SEGURANÇA: BLE não reconectou em %u s — fechando válvula\n",
                               WDG_BLE_RECONEXAO_TIMEOUT_MS / 1000);
                    eventLog_record("serve_stop|wdg_ble_timeout");
                    valveController_stop("WDG_BLE_TIMEOUT");
                    bleDesconectadoEm = 0;
                }
                continue; // Não aplica WDG-1 enquanto aguarda reconexão
            } else {
                // BLE reconectou — reseta o timer de desconexão
                if (bleDesconectadoEm > 0) {
                    DBG_PRINTLN("[WDG-2] BLE reconectado — retomando monitoramento");
                    bleDesconectadoEm = 0;
                    watchdog_kick();
                }
            }

            // ── [WDG-1] PING Watchdog (5s sem PING/comando com BLE ativo) ─
            uint64_t tempoSemComando = agora - g_opState.ultimoComandoMs;
            if (tempoSemComando >= PROTO_WATCHDOG_MS) {
                DBG_PRINTF("[WDG-1] PING WATCHDOG: %u ms sem PING/comando com válvula aberta — fechando\n",
                           (uint32_t)tempoSemComando);
                bleProtocol_send("ERROR:WATCHDOG");
                eventLog_record("wdg_ping_timeout");
                valveController_stop("WDG_PING_TIMEOUT");
            }

        } else {
            // Válvula fechada — reseta o timer de desconexão BLE
            if (bleDesconectadoEm > 0) {
                bleDesconectadoEm = 0;
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // [WDG-4] Estado Inconsistente: RUNNING mas válvula fechada
        // ══════════════════════════════════════════════════════════════════
        if (g_opState.state == SYS_RUNNING && !valveController_isOpen()) {
            if (estadoInconsistenteEm == 0) {
                estadoInconsistenteEm = agora;
            } else if ((agora - estadoInconsistenteEm) > 2000) {
                DBG_PRINTLN("[WDG-4] Estado inconsistente: RUNNING mas válvula fechada há >2s — resetando para IDLE");
                eventLog_record("state_inconsistent|reset_idle");
                g_opState.state = SYS_IDLE;
                estadoInconsistenteEm = 0;
            }
        } else {
            estadoInconsistenteEm = 0;
        }
    }
}
