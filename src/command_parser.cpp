#include "command_parser.h"
#include "valve_controller.h"
#include "flow_sensor.h"
#include "watchdog.h"
#include "ble_protocol.h"
#include <string.h>
#include <stdlib.h>

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: command_parser.cpp
// ═══════════════════════════════════════════════════════════════════════════

// ── Histórico de CMD_IDs para deduplicação ────────────────────────────────
static char s_cmdHistory[PROTO_CMD_ID_HISTORY][PROTO_CMD_ID_MAX_LEN];
static int  s_cmdHistoryIdx = 0;

// ── Inicialização ─────────────────────────────────────────────────────────
void commandParser_init() {
    memset(s_cmdHistory, 0, sizeof(s_cmdHistory));
    s_cmdHistoryIdx = 0;
    DBG_PRINTLN("[PARSER] Inicializado");
}

// ── Deduplicação ──────────────────────────────────────────────────────────
bool commandParser_isDuplicate(const char* cmd_id) {
    if (!cmd_id || strlen(cmd_id) == 0) return false;
    for (int i = 0; i < PROTO_CMD_ID_HISTORY; i++) {
        if (strcmp(s_cmdHistory[i], cmd_id) == 0) return true;
    }
    return false;
}

void commandParser_registerCmdId(const char* cmd_id) {
    if (!cmd_id || strlen(cmd_id) == 0) return;
    strncpy(s_cmdHistory[s_cmdHistoryIdx], cmd_id, PROTO_CMD_ID_MAX_LEN - 1);
    s_cmdHistory[s_cmdHistoryIdx][PROTO_CMD_ID_MAX_LEN - 1] = '\0';
    s_cmdHistoryIdx = (s_cmdHistoryIdx + 1) % PROTO_CMD_ID_HISTORY;
}

// ── Parser ────────────────────────────────────────────────────────────────
// Formato: $CMD:VALOR:CMDID  ou  $CMD:CMDID  ou  $CMD:VALOR  ou  $CMD
bool commandParser_parse(const char* raw, ParsedCommand* out) {
    if (!raw || !out) return false;

    // Limpa a saída
    memset(out, 0, sizeof(ParsedCommand));

    // Verifica prefixo '$'
    if (raw[0] != PROTO_PREFIX) {
        DBG_PRINTF("[PARSER] Prefixo inválido: [%s]\n", raw);
        return false;
    }

    // Copia sem o prefixo '$'
    char buf[PROTO_RX_BUFFER_SIZE];
    strncpy(buf, raw + 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Tokeniza por ':'
    char* token1 = strtok(buf, ":");   // CMD
    char* token2 = strtok(NULL, ":");  // VALOR ou CMD_ID
    char* token3 = strtok(NULL, ":");  // CMD_ID (se token2 é VALOR)

    if (!token1) return false;

    // CMD
    strncpy(out->cmd, token1, sizeof(out->cmd) - 1);

    if (token2) {
        if (token3) {
            // Formato: $CMD:VALOR:CMDID
            strncpy(out->value, token2, sizeof(out->value) - 1);
            strncpy(out->cmd_id, token3, sizeof(out->cmd_id) - 1);
            out->has_value  = true;
            out->has_cmd_id = true;
        } else {
            // Formato: $CMD:TOKEN2 — pode ser VALOR ou CMD_ID
            // Heurística: se começa com letra e não é numérico → CMD_ID
            bool isNumeric = (token2[0] >= '0' && token2[0] <= '9');
            bool isCmdId   = (token2[0] >= 'A' && token2[0] <= 'Z') ||
                             (token2[0] >= 'a' && token2[0] <= 'z');

            // Comandos que sempre têm valor numérico: ML, PL, TO
            bool cmdHasValue = (strcmp(out->cmd, CMD_ML) == 0 ||
                                strcmp(out->cmd, CMD_PL) == 0 ||
                                strcmp(out->cmd, CMD_TO) == 0);

            if (cmdHasValue || isNumeric) {
                strncpy(out->value, token2, sizeof(out->value) - 1);
                out->has_value  = true;
                out->has_cmd_id = false;
            } else {
                // AUTH: token2 é o PIN (pode ser numérico mas é o valor)
                if (strcmp(out->cmd, CMD_AUTH) == 0) {
                    strncpy(out->value, token2, sizeof(out->value) - 1);
                    out->has_value  = true;
                    out->has_cmd_id = false;
                } else {
                    strncpy(out->cmd_id, token2, sizeof(out->cmd_id) - 1);
                    out->has_value  = false;
                    out->has_cmd_id = true;
                }
            }
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// HANDLERS DE CADA COMANDO
// ═══════════════════════════════════════════════════════════════════════════

static void handleAuth(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        bleProtocol_send(RESP_AUTH_FAIL);
        return;
    }
    if (strcmp(cmd->value, BLE_AUTH_PIN) == 0) {
        g_opState.bleAutenticado = true;
        bleProtocol_send(RESP_AUTH_OK);
        DBG_PRINTLN("[CMD] AUTH:OK");
    } else {
        g_opState.bleAutenticado = false;
        bleProtocol_send(RESP_AUTH_FAIL);
        DBG_PRINTF("[CMD] AUTH:FAIL — PIN incorreto: [%s]\n", cmd->value);
    }
}

static void handleML(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        bleProtocol_send(RESP_ERROR_INVALID);
        return;
    }
    uint32_t ml = (uint32_t)atol(cmd->value);
    if (ml == 0) {
        bleProtocol_send(RESP_ERROR_INVALID);
        return;
    }

    // ACK imediato (< 100ms)
    bleProtocol_send(RESP_ML_ACK);
    watchdog_kick();

    if (!valveController_startDispensacao(ml)) {
        bleProtocol_send(RESP_ERROR_BUSY);
    }
}

static void handleStop(const ParsedCommand* cmd) {
    valveController_stop("CMD_STOP");
    // STOP:OK e VALVE:CLOSED são enviados dentro de valveController_stop()
}

static void handleStatus(const ParsedCommand* cmd) {
    switch (g_opState.state) {
        case SYS_RUNNING:
            bleProtocol_send(RESP_STATUS_RUNNING);
            break;
        case SYS_ERROR:
            bleProtocol_send(RESP_STATUS_ERROR);
            break;
        default:
            bleProtocol_send(RESP_STATUS_IDLE);
            break;
    }
}

static void handlePing(const ParsedCommand* cmd) {
    bleProtocol_send(RESP_PONG);
    watchdog_kick(); // PING também reseta o watchdog
}

static void handlePL(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        bleProtocol_send(RESP_ERROR_INVALID);
        return;
    }
    uint32_t pl = (uint32_t)atol(cmd->value);
    if (pl == 0) {
        bleProtocol_send(RESP_ERROR_INVALID);
        return;
    }
    flowSensor_setPulsosLitro(pl);
    bleProtocol_send(RESP_PL_ACK);
}

static void handleTO(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        bleProtocol_send(RESP_ERROR_INVALID);
        return;
    }
    uint32_t to = (uint32_t)atol(cmd->value);
    if (to < 1000) to = 1000; // Mínimo 1s
    g_opState.timeoutSensor = to;
    bleProtocol_send(RESP_TO_ACK);
    DBG_PRINTF("[CMD] Timeout sensor atualizado: %u ms\n", to);
}

// ═══════════════════════════════════════════════════════════════════════════
// TAREFA FREERTOS: taskCommandProcessor
// Processa a fila de comandos recebidos via BLE.
// Executada na Core 0 (PRO_CPU) junto com o BLE para resposta rápida.
// ═══════════════════════════════════════════════════════════════════════════
void taskCommandProcessor(void* param) {
    char rawCmd[PROTO_RX_BUFFER_SIZE];

    for (;;) {
        // Aguarda próximo comando na fila (bloqueante)
        if (xQueueReceive(g_cmdQueue, rawCmd, portMAX_DELAY) != pdTRUE) continue;

        // ── Log de recebimento ────────────────────────────────────────────
        DBG_PRINTF("[PROTO] RX: %s\n", rawCmd);

        // ── Parse ─────────────────────────────────────────────────────────
        ParsedCommand cmd;
        if (!commandParser_parse(rawCmd, &cmd)) {
            DBG_PRINTF("[PARSER] Comando inválido: [%s]\n", rawCmd);
            bleProtocol_send(RESP_ERROR_INVALID);
            continue;
        }

        // ── Deduplicação ──────────────────────────────────────────────────
        if (cmd.has_cmd_id && commandParser_isDuplicate(cmd.cmd_id)) {
            DBG_PRINTF("[PARSER] DUPLICADO: cmd=%s id=%s\n", cmd.cmd, cmd.cmd_id);
            // Resposta específica para ML duplicado; genérico para outros
            if (strcmp(cmd.cmd, CMD_ML) == 0) {
                bleProtocol_send(RESP_ML_DUPLICATE);
            } else {
                bleProtocol_send(RESP_ERROR_INVALID);
            }
            continue;
        }

        // ── Registra CMD_ID ───────────────────────────────────────────────
        if (cmd.has_cmd_id) {
            commandParser_registerCmdId(cmd.cmd_id);
        }

        // ── Verifica autenticação (exceto para AUTH e PING) ───────────────
        bool requerAuth = (strcmp(cmd.cmd, CMD_AUTH) != 0 &&
                           strcmp(cmd.cmd, CMD_PING) != 0 &&
                           strcmp(cmd.cmd, CMD_STATUS) != 0);

        if (requerAuth && !g_opState.bleAutenticado) {
            DBG_PRINTF("[PARSER] Comando sem autenticação: %s\n", cmd.cmd);
            bleProtocol_send(RESP_ERROR_AUTH);
            continue;
        }

        // ── Despacha para o handler correto ──────────────────────────────
        if      (strcmp(cmd.cmd, CMD_AUTH)   == 0) handleAuth(&cmd);
        else if (strcmp(cmd.cmd, CMD_ML)     == 0) handleML(&cmd);
        else if (strcmp(cmd.cmd, CMD_STOP)   == 0) handleStop(&cmd);
        else if (strcmp(cmd.cmd, CMD_STATUS) == 0) handleStatus(&cmd);
        else if (strcmp(cmd.cmd, CMD_PING)   == 0) handlePing(&cmd);
        else if (strcmp(cmd.cmd, CMD_PL)     == 0) handlePL(&cmd);
        else if (strcmp(cmd.cmd, CMD_TO)     == 0) handleTO(&cmd);
        else {
            DBG_PRINTF("[PARSER] Comando desconhecido: [%s]\n", cmd.cmd);
            bleProtocol_send(RESP_ERROR_INVALID);
        }
    }
}
