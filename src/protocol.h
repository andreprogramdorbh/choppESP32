#pragma once
#include "Arduino.h"
#include "config.h"

// ═══════════════════════════════════════════════════════════════════════════
// PROTOCOLO BLE INDUSTRIAL — CHOPP ESP32
// ═══════════════════════════════════════════════════════════════════════════
//
// Protocolo textual ASCII com prefixo '$'.
// Cada comando possui resposta ACK obrigatória em até 100ms.
//
// FORMATO DE COMANDO:
//   $<COMANDO>:<VALOR>:<CMD_ID>
//   $<COMANDO>:<CMD_ID>          (sem valor)
//
// EXEMPLOS:
//   APP → $AUTH:259087:CMD001
//   ESP → AUTH:OK
//
//   APP → $ML:300:CMD002
//   ESP → ML:ACK
//   ESP → VALVE:OPEN
//   ESP → VP:50
//   ESP → VP:150
//   ESP → VP:300
//   ESP → DONE
//
//   APP → $STOP:CMD003
//   ESP → STOP:OK
//   ESP → VALVE:CLOSED
//
//   APP → $STATUS:CMD004
//   ESP → STATUS:IDLE
//
//   APP → $PING:CMD005
//   ESP → PONG
//
// ═══════════════════════════════════════════════════════════════════════════

// ── Prefixo de comando ────────────────────────────────────────────────────
#define PROTO_PREFIX            '$'

// ── Comandos recebidos do Android ─────────────────────────────────────────
#define CMD_AUTH                "AUTH"      // $AUTH:<pin>:<id>
#define CMD_ML                  "ML"        // $ML:<ml>:<id>
#define CMD_STOP                "STOP"      // $STOP:<id>
#define CMD_STATUS              "STATUS"    // $STATUS:<id>
#define CMD_PING                "PING"      // $PING:<id>
#define CMD_PL                  "PL"        // $PL:<pulsos_litro>:<id>
#define CMD_TO                  "TO"        // $TO:<timeout_ms>:<id>

// ── Respostas enviadas ao Android ─────────────────────────────────────────
#define RESP_AUTH_OK            "AUTH:OK"
#define RESP_AUTH_FAIL          "AUTH:FAIL"
#define RESP_ML_ACK             "ML:ACK"
#define RESP_ML_DUPLICATE       "ML:DUPLICATE"
#define RESP_VALVE_OPEN         "VALVE:OPEN"
#define RESP_VALVE_CLOSED       "VALVE:CLOSED"
#define RESP_DONE               "DONE"
#define RESP_STOP_OK            "STOP:OK"
#define RESP_PONG               "PONG"
#define RESP_STATUS_IDLE        "STATUS:IDLE"
#define RESP_STATUS_RUNNING     "STATUS:RUNNING"
#define RESP_STATUS_ERROR       "STATUS:ERROR"
#define RESP_ERROR_AUTH         "ERROR:NOT_AUTHENTICATED"
#define RESP_ERROR_BUSY         "ERROR:BUSY"
#define RESP_ERROR_INVALID      "ERROR:INVALID_CMD"
#define RESP_PL_ACK             "PL:ACK"
#define RESP_TO_ACK             "TO:ACK"

// ── Prefixos de resposta dinâmica ─────────────────────────────────────────
#define RESP_VP_PREFIX          "VP:"       // VP:<ml_parcial>
#define RESP_QP_PREFIX          "QP:"       // QP:<pulsos_totais>

// ── Timeouts do protocolo ─────────────────────────────────────────────────
#define PROTO_ACK_TIMEOUT_MS        100UL   // ACK obrigatório em 100ms
#define PROTO_WATCHDOG_MS          5000UL   // Watchdog: fecha válvula após 5s sem cmd
#define PROTO_HEARTBEAT_INTERVAL_MS 2000UL  // Intervalo de VP durante dispensação
#define PROTO_CMD_ID_HISTORY        16      // Histórico de CMD_IDs para deduplicação
#define PROTO_CMD_ID_MAX_LEN        32      // Tamanho máximo de um CMD_ID

// ── Tamanhos de buffer ────────────────────────────────────────────────────
#define PROTO_RX_BUFFER_SIZE    256
#define PROTO_TX_BUFFER_SIZE    64
#define PROTO_CMD_QUEUE_SIZE    8           // Fila FreeRTOS de comandos

// ── Estados do sistema ────────────────────────────────────────────────────
typedef enum {
    SYS_IDLE        = 0,    // Aguardando comando
    SYS_RUNNING     = 1,    // Dispensando
    SYS_ERROR       = 2,    // Erro (watchdog, sensor, etc.)
    SYS_STOPPING    = 3,    // Parando (STOP recebido)
} SystemState;

// ── Estrutura de um comando parseado ─────────────────────────────────────
typedef struct {
    char cmd[16];                       // Nome do comando (ex: "ML", "AUTH")
    char value[64];                     // Valor do comando (ex: "300", "259087")
    char cmd_id[PROTO_CMD_ID_MAX_LEN];  // ID único do comando (ex: "CMD123")
    bool has_value;                     // true se possui campo de valor
    bool has_cmd_id;                    // true se possui CMD_ID
} ParsedCommand;

// ── Estrutura de estado da operação ──────────────────────────────────────
typedef struct {
    volatile SystemState state;
    volatile uint32_t    mlSolicitado;
    volatile uint32_t    mlLiberado;
    volatile uint32_t    pulsosAlvo;
    volatile uint32_t    pulsosContados;
    volatile bool        bleAutenticado;
    volatile bool        bleConectado;
    volatile uint32_t    pulsosLitro;
    volatile uint32_t    timeoutSensor;
    volatile uint64_t    ultimoComandoMs;   // Timestamp do último comando (watchdog)
    volatile uint64_t    ultimoPulsoMs;     // Timestamp do último pulso (sensor timeout)
} OperationState;

// ── Instância global de estado (definida em main.cpp) ────────────────────
extern OperationState g_opState;

// ── Mutex BLE (definido em ble_protocol.cpp) ─────────────────────────────
extern SemaphoreHandle_t g_bleMutex;

// ── Fila de comandos (definida em ble_protocol.cpp) ──────────────────────
extern QueueHandle_t g_cmdQueue;

// ── Tarefa de processamento de comandos (definida em command_parser.cpp) ──
void taskCommandProcessor(void* param);

// ── Tarefa de watchdog (definida em watchdog.cpp) ─────────────────────────
void taskWatchdog(void* param);

// ── Tarefa de dispensação (definida em valve_controller.cpp) ──────────────
void taskDispensacao(void* param);
