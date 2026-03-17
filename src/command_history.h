#pragma once
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: command_history — Histórico Circular de CMD_IDs (v2.3)
// ═══════════════════════════════════════════════════════════════════════════
//
// NOVIDADES v2.3:
//   - Tamanho aumentado de 20 → 32 entradas (suporta bursts BLE)
//   - Struct HistoryEntry com ml_real, session_id e done_flag
//   - cmdHistory_registerWithResult() — registra ID + ml real + session
//   - cmdHistory_getResult() — retorna ml_real e session para sincronização
//   - Sincronização pós-reconexão: se duplicado já executado → DONE|ID|ml
//
// INTEGRAÇÃO:
//   - command_parser.cpp: chama registerWithResult após DONE
//   - command_parser.cpp: chama getResult para responder DONE em duplicados
// ═══════════════════════════════════════════════════════════════════════════

// ── Configuração ──────────────────────────────────────────────────────────
#define CMD_HISTORY_SIZE        32      // v2.3: aumentado de 20 para 32
#define CMD_HISTORY_ID_MAX_LEN  32      // Tamanho máximo de um CMD_ID
#define CMD_HISTORY_SES_MAX_LEN 24      // Tamanho máximo de um SESSION_ID

// ── Struct de entrada do histórico ────────────────────────────────────────
typedef struct {
    char     cmd_id[CMD_HISTORY_ID_MAX_LEN];    // ID do comando
    char     session_id[CMD_HISTORY_SES_MAX_LEN]; // SESSION_ID (pode ser vazio)
    uint32_t ml_real;                           // ml reais dispensados (0 se não ML)
    bool     done;                              // true se operação foi concluída
} HistoryEntry;

// ── Inicialização ─────────────────────────────────────────────────────────
void cmdHistory_init();

// ── Verifica se um CMD_ID já foi processado ───────────────────────────────
// Retorna true se o commandId já existe no histórico (duplicado).
bool cmdHistory_isDuplicate(const char* cmd_id);

// ── Verifica se um CMD_ID já foi executado e concluído (DONE) ─────────────
// Retorna true se o comando foi concluído com sucesso.
// Preenche ml_real e session_id se não nulos.
bool cmdHistory_isDone(const char* cmd_id, uint32_t* out_ml, char* out_session, size_t session_len);

// ── Registra um CMD_ID como processado (sem resultado) ───────────────────
void cmdHistory_register(const char* cmd_id);

// ── Registra um CMD_ID com resultado de dispensação ──────────────────────
// Chamado após DONE para permitir sincronização pós-reconexão.
void cmdHistory_registerWithResult(const char* cmd_id, uint32_t ml_real,
                                   const char* session_id);

// ── Limpa todo o histórico ────────────────────────────────────────────────
void cmdHistory_clear();

// ── Retorna o número de entradas no histórico ─────────────────────────────
int cmdHistory_count();
