#include "command_parser.h"
#include "command_history.h"
#include "command_queue.h"
#include "valve_controller.h"
#include "flow_sensor.h"
#include "watchdog.h"
#include "ble_protocol.h"
#include "event_log.h"
#include <string.h>
#include <stdlib.h>

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: command_parser.cpp — v2.2 (Protocolo Industrial 24h)
// ═══════════════════════════════════════════════════════════════════════════
//
// NOVIDADES v2.2:
//   - Suporte ao formato alternativo: SERVE|ml|ID=XXXX (compatível com vending)
//   - ACK|ID imediato para comandos com ID (ex: ACK|8472)
//   - DONE|ID|ml_reais ao concluir (ex: DONE|8472|298)
//   - lastCommandId — proteção simples anti-duplicação (complementa command_history)
//   - STATUS responde READY / BUSY / ERROR (sem prefixo STATUS:)
//   - Integração com event_log para rastreabilidade de eventos críticos
//   - Comando $LOGS para enviar log interno via BLE
//   - Watchdog BLE de 30s (configurado no watchdog.cpp)
// ═══════════════════════════════════════════════════════════════════════════

// ── Macro de log industrial com timestamp ─────────────────────────────────
#define LOG_RX(cmd)        DBG_PRINTF("[%llu] RX: %s\n",   (uint64_t)esp_timer_get_time()/1000ULL, cmd)
#define LOG_TX(resp)       DBG_PRINTF("[%llu] TX: %s\n",   (uint64_t)esp_timer_get_time()/1000ULL, resp)
#define LOG_EXEC(cmd, val) DBG_PRINTF("[%llu] EXEC: %s %s\n", (uint64_t)esp_timer_get_time()/1000ULL, cmd, val)
#define LOG_DONE()         DBG_PRINTF("[%llu] DONE\n",      (uint64_t)esp_timer_get_time()/1000ULL)
#define LOG_ERR(msg)       DBG_PRINTF("[%llu] ERROR: %s\n", (uint64_t)esp_timer_get_time()/1000ULL, msg)

// ── lastCommandId — proteção simples anti-duplicação ─────────────────────
// Armazena o ID do último comando SERVE executado para evitar dupla liberação.
// Complementa o command_history (buffer circular de 20 IDs).
static char s_lastCommandId[PROTO_CMD_ID_MAX_LEN] = {0};

// ── Wrapper de envio com log TX ───────────────────────────────────────────
static void sendAndLog(const char* resp) {
    LOG_TX(resp);
    bleProtocol_send(resp);
}

// ── Inicialização ─────────────────────────────────────────────────────────
void commandParser_init() {
    cmdHistory_init();
    cmdQueue_init();
    eventLog_init();
    memset(s_lastCommandId, 0, sizeof(s_lastCommandId));
    memset(g_opState.sessionId,    0, sizeof(g_opState.sessionId));
    memset(g_opState.currentCmdId, 0, sizeof(g_opState.currentCmdId));
    DBG_PRINTLN("[PARSER] Inicializado v2.3 (SESSION_ID + registerWithResult + sync pós-reconexão)");
}

// ── Deduplicação (delegada ao command_history) ────────────────────────────
bool commandParser_isDuplicate(const char* cmd_id) {
    return cmdHistory_isDuplicate(cmd_id);
}

void commandParser_registerCmdId(const char* cmd_id) {
    cmdHistory_register(cmd_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// PARSER — Suporta dois formatos:
//
// Formato A (prefixo $, separador :):
//   $CMD:VALOR:CMDID  |  $CMD:CMDID  |  $CMD:VALOR  |  $CMD
//
// Formato B (sem prefixo, separador |):
//   SERVE|200|ID=8472
//   PING
//   STATUS
//   STOP
// ═══════════════════════════════════════════════════════════════════════════
bool commandParser_parse(const char* raw, ParsedCommand* out) {
    if (!raw || !out) return false;
    memset(out, 0, sizeof(ParsedCommand));

    // ── Formato B: sem prefixo '$', usa '|' como separador ────────────────
    if (raw[0] != PROTO_PREFIX) {
        char buf[PROTO_RX_BUFFER_SIZE];
        strncpy(buf, raw, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char* token1 = strtok(buf, "|");
        char* token2 = strtok(NULL, "|");
        char* token3 = strtok(NULL, "|");

        if (!token1) return false;

        // Normaliza: SERVE → ML (mapeamento de compatibilidade)
        if (strcasecmp(token1, "SERVE") == 0) {
            strncpy(out->cmd, CMD_ML, sizeof(out->cmd) - 1);
        } else {
            strncpy(out->cmd, token1, sizeof(out->cmd) - 1);
        }

        if (token2) {
            strncpy(out->value, token2, sizeof(out->value) - 1);
            out->has_value = true;
        }

        // Extrai ID=XXXX e SESSION=XXXX do formato pipe
        if (token3) {
            const char* idPrefix  = "ID=";
            const char* sesPrefix = "SESSION=";
            if (strncmp(token3, idPrefix, 3) == 0) {
                strncpy(out->cmd_id, token3 + 3, sizeof(out->cmd_id) - 1);
                out->has_cmd_id = true;
            } else if (strncmp(token3, sesPrefix, 8) == 0) {
                strncpy(out->session_id, token3 + 8, sizeof(out->session_id) - 1);
                out->has_session = true;
            } else {
                strncpy(out->cmd_id, token3, sizeof(out->cmd_id) - 1);
                out->has_cmd_id = true;
            }
            // Verifica token4 para SESSION quando token3 foi ID
            char* token4 = strtok(NULL, "|");
            if (token4 && strncmp(token4, sesPrefix, 8) == 0) {
                strncpy(out->session_id, token4 + 8, sizeof(out->session_id) - 1);
                out->has_session = true;
            }
        }

        return true;
    }

    // ── Formato A: prefixo '$', separador ':' ─────────────────────────────
    char buf[PROTO_RX_BUFFER_SIZE];
    strncpy(buf, raw + 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* token1 = strtok(buf, ":");
    char* token2 = strtok(NULL, ":");
    char* token3 = strtok(NULL, ":");

    if (!token1) return false;

    strncpy(out->cmd, token1, sizeof(out->cmd) - 1);

    if (token2) {
        if (token3) {
            // $CMD:VALOR:CMDID
            strncpy(out->value,  token2, sizeof(out->value)  - 1);
            strncpy(out->cmd_id, token3, sizeof(out->cmd_id) - 1);
            out->has_value  = true;
            out->has_cmd_id = true;
        } else {
            bool isNumeric  = (token2[0] >= '0' && token2[0] <= '9');
            bool cmdHasValue = (strcmp(out->cmd, CMD_ML)   == 0 ||
                                strcmp(out->cmd, CMD_PL)   == 0 ||
                                strcmp(out->cmd, CMD_TO)   == 0 ||
                                strcmp(out->cmd, CMD_AUTH) == 0);

            if (cmdHasValue || isNumeric) {
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

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// HANDLERS — ACK IMEDIATO ANTES DE QUALQUER EXECUÇÃO
// ═══════════════════════════════════════════════════════════════════════════

// ── Envia ACK com ID (ex: ACK|8472) ou ACK genérico ──────────────────────
static void sendAck(const ParsedCommand* cmd, const char* cmdName) {
    char buf[PROTO_TX_BUFFER_SIZE];
    if (cmd->has_cmd_id && cmd->cmd_id[0] != '\0') {
        snprintf(buf, sizeof(buf), "ACK|%s", cmd->cmd_id);
    } else {
        snprintf(buf, sizeof(buf), "%s:ACK", cmdName);
    }
    sendAndLog(buf);
}

// ── Envia DONE com ID, ml reais e SESSION (ex: DONE|8472|298|SES_001) ────────
static void sendDone(const ParsedCommand* cmd, uint32_t mlReais) {
    char buf[PROTO_TX_BUFFER_SIZE];
    if (cmd->has_cmd_id && cmd->cmd_id[0] != '\0') {
        if (cmd->has_session && cmd->session_id[0] != '\0') {
            // [v2.3] DONE|ID|ml|SESSION
            snprintf(buf, sizeof(buf), "DONE|%s|%u|%s",
                     cmd->cmd_id, mlReais, cmd->session_id);
        } else if (g_opState.sessionId[0] != '\0') {
            // Usa SESSION do estado global se disponível
            snprintf(buf, sizeof(buf), "DONE|%s|%u|%s",
                     cmd->cmd_id, mlReais, g_opState.sessionId);
        } else {
            snprintf(buf, sizeof(buf), "DONE|%s|%u", cmd->cmd_id, mlReais);
        }
        // [v2.3] Registra resultado no command_history para sincronização pós-reconexão
        const char* sesId = (cmd->has_session && cmd->session_id[0] != '\0')
                            ? cmd->session_id
                            : g_opState.sessionId;
        cmdHistory_registerWithResult(cmd->cmd_id, mlReais, sesId);
    } else {
        snprintf(buf, sizeof(buf), "DONE");
    }
    sendAndLog(buf);
}

// ── AUTH ──────────────────────────────────────────────────────────────────
static void handleAuth(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        sendAndLog(RESP_AUTH_FAIL);
        eventLog_record("auth_fail|reason=no_pin");
        return;
    }
    if (strcmp(cmd->value, BLE_AUTH_PIN) == 0) {
        g_opState.bleAutenticado = true;
        sendAndLog(RESP_AUTH_OK);
        LOG_EXEC(CMD_AUTH, "OK");
        eventLog_record("auth_ok");
    } else {
        g_opState.bleAutenticado = false;
        sendAndLog(RESP_AUTH_FAIL);
        LOG_ERR("AUTH:FAIL — PIN incorreto");
        eventLog_record("auth_fail|reason=wrong_pin");
    }
}

// ── ML / SERVE ────────────────────────────────────────────────────────────
static void handleML(const ParsedCommand* cmd) {
    if (!cmd->has_value) {
        sendAndLog(RESP_ERROR_INVALID);
        return;
    }

    uint32_t ml = (uint32_t)atol(cmd->value);
    if (ml == 0 || ml > 5000) {
        sendAndLog(RESP_ERROR_INVALID);
        LOG_ERR("ML valor inválido");
        return;
    }

    // ── Proteção lastCommandId (anti-duplicação simples) ──────────────────
    if (cmd->has_cmd_id && cmd->cmd_id[0] != '\0') {
        if (strcmp(s_lastCommandId, cmd->cmd_id) == 0) {
            // Mesmo ID do último comando — duplicado confirmado
            char dupResp[PROTO_TX_BUFFER_SIZE];
            if (cmd->has_cmd_id) {
                snprintf(dupResp, sizeof(dupResp), "ACK|%s", cmd->cmd_id);
            } else {
                strncpy(dupResp, RESP_ML_DUPLICATE, sizeof(dupResp) - 1);
            }
            sendAndLog(dupResp);
            DBG_PRINTF("[PARSER] lastCommandId DUPLICATE: id=%s\n", cmd->cmd_id);
            eventLog_record("duplicate_command|lastCommandId");
            return;
        }
        // Atualiza lastCommandId
        strncpy(s_lastCommandId, cmd->cmd_id, sizeof(s_lastCommandId) - 1);
    }

    // ── ACK IMEDIATO (< 100ms) — ANTES de iniciar a dispensação ──────────
    sendAck(cmd, CMD_ML);
    watchdog_kick();

    // ── [v2.3] Armazena SESSION_ID e CMD_ID no estado global ─────────────
    if (cmd->has_cmd_id) {
        strncpy(g_opState.currentCmdId, cmd->cmd_id,
                sizeof(g_opState.currentCmdId) - 1);
    } else {
        g_opState.currentCmdId[0] = '\0';
    }
    if (cmd->has_session) {
        strncpy(g_opState.sessionId, cmd->session_id,
                sizeof(g_opState.sessionId) - 1);
    } else {
        g_opState.sessionId[0] = '\0';
    }

    // ── Log de evento com SESSION_ID ──────────────────────────────────────
    char evtBuf[80];
    snprintf(evtBuf, sizeof(evtBuf), "serve_start|ml=%u|id=%s|session=%s",
             ml,
             cmd->has_cmd_id  ? cmd->cmd_id    : "none",
             cmd->has_session ? cmd->session_id : "none");
    eventLog_record(evtBuf);

    // ── Executa a operação APÓS o ACK ─────────────────────────────────────
    LOG_EXEC(CMD_ML, cmd->value);
    if (!valveController_startDispensacao(ml)) {
        sendAndLog(RESP_ERROR_BUSY);
        LOG_ERR("ML:BUSY — dispensação já em andamento");
        eventLog_record("serve_start|BUSY");
    }
}

// ── STOP ──────────────────────────────────────────────────────────────────
static void handleStop(const ParsedCommand* cmd) {
    sendAck(cmd, CMD_STOP);
    LOG_EXEC(CMD_STOP, "");

    // Limpa a fila de comandos pendentes
    cmdQueue_clear();

    // Para a válvula (envia VALVE:CLOSED internamente)
    valveController_stop("CMD_STOP");
    LOG_DONE();
    eventLog_record("serve_stop|CMD_STOP");
}

// ── STATUS ────────────────────────────────────────────────────────────────
// Responde READY / BUSY / ERROR (formato vending machine)
static void handleStatus(const ParsedCommand* cmd) {
    const char* resp;
    switch (g_opState.state) {
        case SYS_RUNNING:  resp = "BUSY";  break;
        case SYS_ERROR:    resp = "ERROR"; break;
        default:           resp = "READY"; break;
    }
    sendAndLog(resp);
    LOG_EXEC(CMD_STATUS, resp);
}

// ── PING ──────────────────────────────────────────────────────────────────
static void handlePing(const ParsedCommand* cmd) {
    sendAndLog(RESP_PONG);
    watchdog_kick();
    // Sem LOG_EXEC para não poluir o log com PINGs frequentes
}

// ── PL (pulsos/litro) ─────────────────────────────────────────────────────
static void handlePL(const ParsedCommand* cmd) {
    if (!cmd->has_value) { sendAndLog(RESP_ERROR_INVALID); return; }
    uint32_t pl = (uint32_t)atol(cmd->value);
    if (pl == 0) { sendAndLog(RESP_ERROR_INVALID); return; }
    flowSensor_setPulsosLitro(pl);
    sendAndLog(RESP_PL_ACK);
    LOG_EXEC(CMD_PL, cmd->value);
}

// ── TO (timeout sensor) ───────────────────────────────────────────────────
static void handleTO(const ParsedCommand* cmd) {
    if (!cmd->has_value) { sendAndLog(RESP_ERROR_INVALID); return; }
    uint32_t to = (uint32_t)atol(cmd->value);
    if (to < 1000) to = 1000;
    g_opState.timeoutSensor = to;
    sendAndLog(RESP_TO_ACK);
    LOG_EXEC(CMD_TO, cmd->value);
}

// ── LOGS (envia log interno via BLE) ─────────────────────────────────────
static void handleLogs(const ParsedCommand* cmd) {
    LOG_EXEC("LOGS", "");
    eventLog_sendViaBLE();
}

// ═══════════════════════════════════════════════════════════════════════════
// TAREFA FREERTOS: taskCommandProcessor v2.2
// ═══════════════════════════════════════════════════════════════════════════
void taskCommandProcessor(void* param) {
    char rawCmd[PROTO_RX_BUFFER_SIZE];

    DBG_PRINTLN("[PARSER] taskCommandProcessor iniciada (v2.2)");

    for (;;) {
        // ── Aguarda próximo comando da command_queue (bloqueante) ─────────
        if (!cmdQueue_dequeue(rawCmd, sizeof(rawCmd), portMAX_DELAY)) continue;

        // ── Log RX industrial ─────────────────────────────────────────────
        LOG_RX(rawCmd);

        // ── Parse (suporta formato $ e formato pipe |) ────────────────────
        ParsedCommand cmd;
        if (!commandParser_parse(rawCmd, &cmd)) {
            LOG_ERR("Comando inválido — parse falhou");
            sendAndLog(RESP_ERROR_INVALID);
            continue;
        }

        // ── Deduplication via command_history ────────────────────────────────────────────
        if (cmd.has_cmd_id && cmdHistory_isDuplicate(cmd.cmd_id)) {
            DBG_PRINTF("[PARSER] DUPLICADO (history): cmd=%s id=%s\n", cmd.cmd, cmd.cmd_id);
            if (strcmp(cmd.cmd, CMD_ML) == 0) {
                // [v2.3] Sincronização pós-reconexão:
                // Se já foi concluído (DONE), responde DONE|ID|ml para o Android saber
                uint32_t mlReal = 0;
                char sesId[OP_SESSION_ID_MAX_LEN] = {0};
                if (cmdHistory_isDone(cmd.cmd_id, &mlReal, sesId, sizeof(sesId))) {
                    char doneResp[PROTO_TX_BUFFER_SIZE];
                    if (sesId[0] != '\0') {
                        snprintf(doneResp, sizeof(doneResp), "DONE|%s|%u|%s",
                                 cmd.cmd_id, mlReal, sesId);
                    } else {
                        snprintf(doneResp, sizeof(doneResp), "DONE|%s|%u",
                                 cmd.cmd_id, mlReal);
                    }
                    sendAndLog(doneResp);
                    DBG_PRINTF("[PARSER] Sync pós-reconexão: DONE enviado para id=%s ml=%u\n",
                               cmd.cmd_id, mlReal);
                    eventLog_record("duplicate_command|sync_done");
                } else {
                    // Em andamento ou não concluído: reenvia ACK
                    sendAck(&cmd, CMD_ML);
                    eventLog_record("duplicate_command|ack_resent");
                }
            } else {
                sendAndLog(RESP_DUPLICATE);
                eventLog_record("duplicate_command|history");
            }
            continue;
        }

        // ── Registra CMD_ID no histórico ────────────────────────────────────────────
        if (cmd.has_cmd_id) {
            cmdHistory_register(cmd.cmd_id);
        }

        // ── Verifica autenticação ─────────────────────────────────────────
        bool requerAuth = (strcmp(cmd.cmd, CMD_AUTH)   != 0 &&
                           strcmp(cmd.cmd, CMD_PING)   != 0 &&
                           strcmp(cmd.cmd, CMD_STATUS) != 0);

        if (requerAuth && !g_opState.bleAutenticado) {
            LOG_ERR("Comando sem autenticação");
            sendAndLog(RESP_ERROR_AUTH);
            continue;
        }

        // ── Despacha handler ──────────────────────────────────────────────
        if      (strcmp(cmd.cmd, CMD_AUTH)   == 0) handleAuth(&cmd);
        else if (strcmp(cmd.cmd, CMD_ML)     == 0) handleML(&cmd);
        else if (strcmp(cmd.cmd, CMD_STOP)   == 0) handleStop(&cmd);
        else if (strcmp(cmd.cmd, CMD_STATUS) == 0) handleStatus(&cmd);
        else if (strcmp(cmd.cmd, CMD_PING)   == 0) handlePing(&cmd);
        else if (strcmp(cmd.cmd, CMD_PL)     == 0) handlePL(&cmd);
        else if (strcmp(cmd.cmd, CMD_TO)     == 0) handleTO(&cmd);
        else if (strcmp(cmd.cmd, "LOGS")     == 0) handleLogs(&cmd);
        else {
            LOG_ERR("Comando desconhecido");
            sendAndLog(RESP_ERROR_INVALID);
        }
    }
}
