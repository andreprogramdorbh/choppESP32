#include "command_history.h"
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: command_history.cpp — v2.3
// Memória circular de 32 CMD_IDs com resultado de dispensação.
// Thread-safe via mutex FreeRTOS.
// ═══════════════════════════════════════════════════════════════════════════

// ── Buffer circular ───────────────────────────────────────────────────────
static HistoryEntry      s_history[CMD_HISTORY_SIZE];
static int               s_writeIdx = 0;
static int               s_count    = 0;
static SemaphoreHandle_t s_mutex    = nullptr;

// ── Inicialização ─────────────────────────────────────────────────────────
void cmdHistory_init() {
    memset(s_history, 0, sizeof(s_history));
    s_writeIdx = 0;
    s_count    = 0;

    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }

    DBG_PRINTF("[HIST] Inicializado v2.3 | capacidade=%d | id_max=%d bytes\n",
               CMD_HISTORY_SIZE, CMD_HISTORY_ID_MAX_LEN);
}

// ── Busca interna por cmd_id (sem lock — deve ser chamada com mutex) ──────
static int findIndex(const char* cmd_id) {
    for (int i = 0; i < CMD_HISTORY_SIZE; i++) {
        if (s_history[i].cmd_id[0] != '\0' &&
            strcmp(s_history[i].cmd_id, cmd_id) == 0) {
            return i;
        }
    }
    return -1;
}

// ── Verifica duplicado (thread-safe) ─────────────────────────────────────
bool cmdHistory_isDuplicate(const char* cmd_id) {
    if (!cmd_id || cmd_id[0] == '\0') return false;
    if (!s_mutex) return false;

    bool found = false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        found = (findIndex(cmd_id) >= 0);
        xSemaphoreGive(s_mutex);
    } else {
        DBG_PRINTLN("[HIST] WARN: mutex timeout em isDuplicate — assumindo não duplicado");
    }

    if (found) {
        DBG_PRINTF("[HIST] DUPLICADO detectado: [%s]\n", cmd_id);
    }

    return found;
}

// ── Verifica se já foi concluído (DONE) e retorna resultado ───────────────
bool cmdHistory_isDone(const char* cmd_id, uint32_t* out_ml,
                       char* out_session, size_t session_len) {
    if (!cmd_id || cmd_id[0] == '\0') return false;
    if (!s_mutex) return false;

    bool done = false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        int idx = findIndex(cmd_id);
        if (idx >= 0 && s_history[idx].done) {
            done = true;
            if (out_ml)      *out_ml = s_history[idx].ml_real;
            if (out_session && session_len > 0) {
                strncpy(out_session, s_history[idx].session_id, session_len - 1);
                out_session[session_len - 1] = '\0';
            }
        }
        xSemaphoreGive(s_mutex);
    }

    if (done) {
        DBG_PRINTF("[HIST] DONE encontrado para [%s] | ml_real=%u\n",
                   cmd_id, out_ml ? *out_ml : 0);
    }

    return done;
}

// ── Registra CMD_ID simples (sem resultado) ───────────────────────────────
void cmdHistory_register(const char* cmd_id) {
    if (!cmd_id || cmd_id[0] == '\0') return;
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        // Verifica se já existe (atualiza em vez de duplicar)
        int idx = findIndex(cmd_id);
        if (idx < 0) {
            idx = s_writeIdx;
            s_writeIdx = (s_writeIdx + 1) % CMD_HISTORY_SIZE;
            if (s_count < CMD_HISTORY_SIZE) s_count++;
        }

        memset(&s_history[idx], 0, sizeof(HistoryEntry));
        strncpy(s_history[idx].cmd_id, cmd_id, CMD_HISTORY_ID_MAX_LEN - 1);
        s_history[idx].ml_real = 0;
        s_history[idx].done    = false;

        DBG_PRINTF("[HIST] Registrado: [%s] | total=%d/%d\n",
                   cmd_id, s_count, CMD_HISTORY_SIZE);

        xSemaphoreGive(s_mutex);
    } else {
        DBG_PRINTF("[HIST] WARN: mutex timeout ao registrar [%s]\n", cmd_id);
    }
}

// ── Registra CMD_ID com resultado de dispensação (v2.3) ──────────────────
void cmdHistory_registerWithResult(const char* cmd_id, uint32_t ml_real,
                                   const char* session_id) {
    if (!cmd_id || cmd_id[0] == '\0') return;
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        // Busca entrada existente ou cria nova
        int idx = findIndex(cmd_id);
        if (idx < 0) {
            idx = s_writeIdx;
            s_writeIdx = (s_writeIdx + 1) % CMD_HISTORY_SIZE;
            if (s_count < CMD_HISTORY_SIZE) s_count++;
        }

        strncpy(s_history[idx].cmd_id, cmd_id, CMD_HISTORY_ID_MAX_LEN - 1);
        s_history[idx].cmd_id[CMD_HISTORY_ID_MAX_LEN - 1] = '\0';
        s_history[idx].ml_real = ml_real;
        s_history[idx].done    = true;

        if (session_id && session_id[0] != '\0') {
            strncpy(s_history[idx].session_id, session_id, CMD_HISTORY_SES_MAX_LEN - 1);
            s_history[idx].session_id[CMD_HISTORY_SES_MAX_LEN - 1] = '\0';
        } else {
            s_history[idx].session_id[0] = '\0';
        }

        DBG_PRINTF("[HIST] Resultado registrado: [%s] | ml=%u | session=%s | done=true\n",
                   cmd_id, ml_real, session_id ? session_id : "none");

        xSemaphoreGive(s_mutex);
    } else {
        DBG_PRINTF("[HIST] WARN: mutex timeout ao registrar resultado [%s]\n", cmd_id);
    }
}

// ── Limpa o histórico ─────────────────────────────────────────────────────
void cmdHistory_clear() {
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memset(s_history, 0, sizeof(s_history));
        s_writeIdx = 0;
        s_count    = 0;
        xSemaphoreGive(s_mutex);
    }

    DBG_PRINTLN("[HIST] Histórico limpo (reconexão BLE)");
}

// ── Contagem de entradas ──────────────────────────────────────────────────
int cmdHistory_count() {
    return s_count;
}
