#pragma once
#include "protocol.h"

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: command_parser — Parser e Processador de Comandos BLE
// ═══════════════════════════════════════════════════════════════════════════
//
// Responsabilidades:
//   - Parsear comandos ASCII recebidos via BLE (formato $CMD:VALOR:ID)
//   - Detectar e rejeitar comandos duplicados (mesmo CMD_ID)
//   - Despachar comandos para os handlers corretos
//   - Garantir resposta ACK em até 100ms
//   - Logar todos os comandos recebidos (RX:) e enviados (TX:)
//
// FORMATO DO COMANDO:
//   $<CMD>:<VALOR>:<CMD_ID>   — com valor e ID
//   $<CMD>:<CMD_ID>           — sem valor, com ID
//   $<CMD>:<VALOR>            — com valor, sem ID (legado)
//   $<CMD>                    — sem valor e sem ID (legado)
// ═══════════════════════════════════════════════════════════════════════════

// ── Inicialização ─────────────────────────────────────────────────────────
void commandParser_init();

// ── Parse de um comando recebido ─────────────────────────────────────────
// Retorna true se o parse foi bem-sucedido.
bool commandParser_parse(const char* raw, ParsedCommand* out);

// ── Verifica se um CMD_ID já foi processado (deduplicação) ───────────────
bool commandParser_isDuplicate(const char* cmd_id);

// ── Registra um CMD_ID como processado ───────────────────────────────────
void commandParser_registerCmdId(const char* cmd_id);

// ── Tarefa FreeRTOS: processa a fila de comandos ──────────────────────────
void taskCommandProcessor(void* param);
