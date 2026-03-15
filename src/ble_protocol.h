#pragma once
#include "protocol.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>

// ═══════════════════════════════════════════════════════════════════════════
// MÓDULO: ble_protocol — Camada BLE (Nordic UART Service)
// ═══════════════════════════════════════════════════════════════════════════
//
// Responsabilidades:
//   - Inicializar o BLE com nome dinâmico CHOPP_XXXX (4 últimos dígitos MAC)
//   - Implementar o Nordic UART Service (NUS) para TX/RX
//   - Gerenciar conexão, desconexão e reconexão
//   - Receber dados do Android e enfileirar na g_cmdQueue
//   - Enviar respostas ao Android via notificação BLE (TX)
//   - Proteger o acesso à característica TX com mutex
//   - Configurar parâmetros de conexão para máxima estabilidade
//   - Implementar segurança BLE: bond + PIN 259087
//
// UUIDs Nordic UART Service (NUS):
//   Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
//   TX (ESP→APP): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (Notify)
//   RX (APP→ESP): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (Write)
// ═══════════════════════════════════════════════════════════════════════════

// ── UUIDs NUS ─────────────────────────────────────────────────────────────
#define NUS_SERVICE_UUID    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID         "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID         "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ── Inicialização ─────────────────────────────────────────────────────────
void bleProtocol_init();

// ── Envio de dados ao Android (thread-safe via mutex) ─────────────────────
void bleProtocol_send(const char* data);

// ── Reinicia o advertising após desconexão ────────────────────────────────
void bleProtocol_startAdvertising();

// ── Retorna o nome BLE atual (CHOPP_XXXX) ────────────────────────────────
const char* bleProtocol_getDeviceName();
