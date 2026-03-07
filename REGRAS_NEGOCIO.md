# Análise das Regras de Negócio - Projeto CHOPPE

## 1. Visão Geral do Projeto

O projeto **CHOPPE** é um sistema embarcado baseado em **ESP32** que controla uma máquina de distribuição de chopp (cerveja). O sistema funciona como um **dispensador automático** que libera quantidades específicas de líquido através de uma válvula controlada, com autenticação via RFID e comunicação via Bluetooth Low Energy (BLE).

---

## 2. Arquitetura do Sistema

### 2.1 Componentes Principais

| Componente | Descrição |
|-----------|-----------|
| **ESP32** | Microcontrolador principal que executa toda a lógica |
| **Sensor de Fluxo (YF-S401)** | Sensor Hall Effect que mede o volume dispensado através de pulsos |
| **Válvula Solenoide (Relé)** | Controla a abertura/fechamento do fluxo de líquido |
| **Leitor RFID (RC522)** | Autentica usuários através de tags RFID |
| **BLE (Bluetooth Low Energy)** | Comunica com aplicativo móvel para controle remoto |
| **LED de Status** | Indica estado de conexão BLE |
| **EEPROM** | Armazena configurações persistentes |

### 2.2 Arquitetura de Software

```
main.cpp
├── setup() - Inicialização dos componentes
├── loop() - Deletado (não utilizado)
└── Tarefas FreeRTOS
    ├── taskLiberaML - Controla liberação de volume
    └── taskRFID - Monitora leituras RFID
```

---

## 3. Regras de Negócio Principais

### 3.1 Controle de Volume (Dispensação)

**Objetivo:** Liberar quantidades precisas de líquido baseado em pulsos do sensor de fluxo.

#### Configurações Padrão
- **Pulsos por Litro:** 5880 pulsos/L (sensor YF-S401)
- **Timeout do Sensor:** 2000ms (aguarda início do fluxo)
- **Intervalo de Status:** 2 segundos (atualiza aplicativo)

#### Processo de Dispensação

1. **Recebimento de Comando**
   - Comando `ML:<quantidade>` via BLE ou RFID
   - Quantidade em mililitros (ML)
   - Enfileirado em fila FreeRTOS para processamento

2. **Cálculo de Pulsos**
   ```
   pulsoML = pulsosLitro / 1000.0
   quantidadePulso = pulsoML × quantidade_ml
   ```
   - Exemplo: Para 500ml com 5880 pulsos/L
   - pulsoML = 5.88
   - quantidadePulso = 5.88 × 500 = 2940 pulsos

3. **Acionamento da Válvula**
   - Ativa relé (GPIO) para abrir válvula
   - Inicia interrupção no sensor de fluxo
   - Conta pulsos até atingir quantidade desejada

4. **Monitoramento em Tempo Real**
   - A cada 2 segundos envia volume parcial dispensado
   - Calcula: `mlLiberado = contadorPulso / pulsoML`
   - Envia comando `VP:<volume_parcial>` para aplicativo

5. **Finalização**
   - Desativa relé quando atinge quantidade de pulsos
   - OU timeout expira (sem fluxo detectado)
   - Envia confirmação final com volume total dispensado
   - Envia quantidade de pulsos contados

#### Tipos de Dispensação

| Comando | Parâmetro | Comportamento |
|---------|-----------|---------------|
| `ML:500` | Volume em ML | Libera exatamente 500ml |
| `LB:` | Sem parâmetro | Liberação contínua (0xFFFFFFFF pulsos) |
| `VP:` | Resposta | Envia volume parcial a cada 2s |
| `QP:` | Resposta | Envia quantidade total de pulsos |

### 3.2 Autenticação e Controle de Acesso

#### Sistema RFID

**Objetivo:** Controlar acesso e permitir liberação automática para usuários autorizados.

#### Configuração de Administrador
- Comando: `RI:<rfid_code>` (8 caracteres hexadecimais)
- Armazena ID RFID do administrador na EEPROM
- Exemplo: `RI:A1B2C3D4`

#### Processo de Leitura RFID

1. **Monitoramento Contínuo**
   - Task RFID executa a cada 500ms
   - Verifica presença de nova tag
   - Debounce de 2 segundos entre leituras

2. **Identificação de Tag**
   - Lê 4 bytes do UID da tag
   - Converte para hexadecimal (8 caracteres)
   - Exemplo: `A1 B2 C3 D4` → `a1b2c3d4`

3. **Validação de Administrador**
   - Compara código lido com `rfidMaster` armazenado
   - Se for administrador: adiciona sufixo `:MASTER`
   - Dispara liberação contínua automaticamente

4. **Envio de Dados**
   - Envia comando `ID:<rfid_code>` para BLE
   - Exemplo: `ID:a1b2c3d4` ou `ID:a1b2c3d4:MASTER`

### 3.3 Comunicação BLE

**Objetivo:** Permitir controle remoto via aplicativo móvel e feedback em tempo real.

#### Características BLE
- **Nome do Dispositivo:** CHOPPE
- **Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX Characteristic:** `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (recebe comandos)
- **TX Characteristic:** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (envia respostas)

#### Status de Conexão
- **Conectado:** LED de status aceso
- **Desconectado:** LED de status apagado
- **Reconexão:** Automática via advertising BLE

#### Protocolo de Comandos

Todos os comandos seguem o formato: `$<COMANDO>:<PARÂMETRO>`

| Comando | Formato | Descrição | Resposta |
|---------|---------|-----------|----------|
| ML | `$ML:500` | Libera 500ml | `OK` ou `ERRO` |
| PL | `$PL:5880` | Configura pulsos/litro | `OK` ou `PL:5880` |
| LB | `$LB:` | Liberação contínua | `OK` ou `ERRO` |
| RI | `$RI:A1B2C3D4` | Registra RFID master | `OK` ou `ERRO` |
| TO | `$TO:2000` | Configura timeout (ms) | `OK` ou `PL:5880` |
| ID | Resposta | Código RFID lido | Enviado automaticamente |
| VP | Resposta | Volume parcial dispensado | Enviado a cada 2s |
| VZ | Resposta | Vazão (L/min) | Comentado no código |
| QP | Resposta | Quantidade de pulsos | Enviado ao final |

### 3.4 Persistência de Configuração

**Objetivo:** Manter configurações mesmo após desligamento.

#### Estrutura de Configuração (EEPROM)

```c
typedef struct {
    uint16_t magicFlag;      // 0xF2F2 - Valida se dados foram gravados
    uint8_t modoAP;          // 0=WiFi normal, 1=Access Point
    char wifiSSID[30];       // SSID da rede WiFi
    char wifiPass[30];       // Senha da rede WiFi
    char rfidMaster[12];     // ID RFID do administrador
    uint32_t pulsosLitro;    // Pulsos por litro do sensor
    uint32_t timeOut;        // Timeout para sensor (ms)
} config_t;
```

#### Valores Padrão (Factory Reset)

| Parâmetro | Valor Padrão |
|-----------|-------------|
| magicFlag | 0xF2F2 |
| modoAP | 0 (WiFi) |
| wifiSSID | brisa-448561 |
| wifiPass | 9xmkuiw1 |
| rfidMaster | (vazio) |
| pulsosLitro | 5880 |
| timeOut | 2000ms |

#### Inicialização
- Na primeira inicialização (magicFlag ≠ 0xF2F2), carrega valores padrão
- Salva na EEPROM com `EEPROM.commit()`
- Nas inicializações subsequentes, carrega valores anteriormente salvos

### 3.5 Tratamento de Erros e Timeouts

#### Timeout do Sensor
- **Propósito:** Detectar falha no sensor ou válvula entupida
- **Valor:** 2000ms (configurável via `TO:`)
- **Comportamento:** Se não houver pulso no tempo limite, interrompe dispensação
- **Ação:** Desativa relé e encerra operação

#### Validação de Comandos
- Todos os comandos devem começar com `$`
- Se não começar com `$`, comando é ignorado (`!!!`)
- Parâmetros vazios ou inválidos retornam `ERRO`

#### Debounce RFID
- Intervalo mínimo entre leituras: 2 segundos
- Evita múltiplas leituras da mesma tag

---

## 4. Fluxo de Operação Completo

### 4.1 Inicialização do Sistema

```
1. setup()
   ├─ Desativa proteção brownout
   ├─ Configura pinos (relé, status, sensor)
   ├─ Inicia porta serial para debug
   ├─ Lê configuração da EEPROM
   ├─ Inicializa BLE
   ├─ Cria fila FreeRTOS para comandos
   └─ Inicia tasks (RFID e dispensação)

2. Aguarda conexão BLE ou comando RFID
```

### 4.2 Fluxo de Dispensação via BLE

```
1. Aplicativo envia: $ML:500
2. BLE recebe e chama executaOperacao()
3. Valida comando e enfileira quantidade
4. taskLiberaML processa:
   ├─ Calcula pulsos necessários
   ├─ Ativa relé (abre válvula)
   ├─ Inicia contagem de pulsos
   ├─ A cada 2s envia VP:<volume_parcial>
   ├─ Aguarda atingir quantidade ou timeout
   ├─ Desativa relé (fecha válvula)
   └─ Envia ML:<volume_final> e QP:<pulsos>
5. Aplicativo recebe respostas em tempo real
```

### 4.3 Fluxo de Dispensação via RFID

```
1. Usuário aproxima tag RFID
2. taskRFID detecta e lê código
3. Envia ID:<rfid_code> via BLE
4. Se for administrador (rfidMaster):
   ├─ Adiciona ":MASTER" ao código
   └─ Dispara liberação contínua (LB)
5. Aplicativo recebe ID e pode processar
```

---

## 5. Configurações Técnicas

### 5.1 Pinagem (ESP32 DevKit)

| Função | GPIO |
|--------|------|
| Relé (Válvula) | 16 |
| LED Status | 2 |
| Sensor Fluxo | 17 |
| RFID SS | 5 |
| RFID Reset | 4 |
| RFID MOSI | 23 |
| RFID MISO | 19 |
| RFID SCLK | 18 |

### 5.2 Lógica de Pinos

| Pino | Estado | Significado |
|-----|--------|------------|
| RELE_ON | LOW | Relé ativado com nível baixo |
| SENSOR_ON | LOW | Sensor ativo com nível baixo |
| LED_STATUS_ON | HIGH | LED aceso com nível alto (ESP32) |

### 5.3 Sensor de Fluxo

- **Modelo:** YF-S401
- **Precisão:** 0.3 ~ 6L/min ± 3%
- **Pulsos por Litro:** 5880
- **Tempo para 1L:** ~10 segundos
- **Tipo de Saída:** Square wave (onda quadrada)

---

## 6. Módulos Compiláveis

| Módulo | Flag | Status | Descrição |
|--------|------|--------|-----------|
| BLE | `USAR_ESP32_UART_BLE` | ✅ Ativado | Comunicação Bluetooth |
| Página Web | `USAR_PAGINA_CONFIG` | ❌ Desativado | Interface web de configuração |
| RFID | `USAR_RFID` | ✅ Ativado | Autenticação por RFID |
| Debug | `debug_debug` | ✅ Ativado | Logs via serial |

---

## 7. Funcionalidades Implementadas

### ✅ Implementadas

1. **Dispensação por Volume**
   - Libera quantidade exata em ML
   - Monitoramento em tempo real
   - Feedback de volume parcial

2. **Autenticação RFID**
   - Leitura de tags RFID
   - Identificação de administrador
   - Liberação automática para admin

3. **Comunicação BLE**
   - Protocolo de comandos estruturado
   - Respostas em tempo real
   - Status de conexão

4. **Persistência de Dados**
   - Armazenamento em EEPROM
   - Factory reset automático
   - Configurações customizáveis

5. **Tratamento de Erros**
   - Timeout do sensor
   - Validação de comandos
   - Debounce de RFID

### ⏳ Parcialmente Implementadas

1. **Página Web de Configuração**
   - Interface HTML criada
   - Endpoints GET/POST definidos
   - Desativada por flag de compilação

2. **Cálculo de Vazão**
   - Código comentado no projeto
   - Estrutura pronta, não ativada

### ❌ Não Implementadas

1. **Autenticação WiFi**
   - Credenciais hardcoded
   - Modo AP não funcional
   - Página web desativada

2. **Controle de Acesso por Usuário**
   - Apenas um RFID master
   - Sem lista de usuários autorizados
   - Sem histórico de transações

3. **Proteção de Dados**
   - Sem criptografia de comunicação
   - Credenciais em texto plano
   - Sem autenticação BLE

---

## 8. Fluxo de Dados do Sensor

### Cálculo de Volume

```
1. Sensor emite pulsos (onda quadrada)
2. ISR (fluxoISR) incrementa contadorPulso
3. Cada pulso = 1/5880 de litro = ~0.170 ml

Exemplo:
- 500 ml desejado
- pulsoML = 5880 / 1000 = 5.88
- quantidadePulso = 5.88 × 500 = 2940 pulsos
- Quando contadorPulso ≥ 2940, desativa relé
```

### Monitoramento de Vazão

```
Fórmula (comentada):
vazao = (mlLiberado / tempoDecorridoS) × 60 / 1000
= ML/seg convertido para L/min

Atualmente apenas VP (volume parcial) é enviado
```

---

## 9. Sequência de Inicialização FreeRTOS

```
1. setup() cria fila de comandos
2. Cria taskRFID (prioridade 3)
   ├─ Aguarda 2 segundos
   ├─ Inicializa SPI e RC522
   └─ Loop infinito lendo tags

3. Cria taskLiberaML (prioridade 3)
   ├─ Aguarda comandos na fila
   ├─ Processa dispensação
   └─ Envia feedback via BLE

4. loop() é deletado (não utilizado)
```

---

## 10. Protocolo de Comunicação BLE

### Formato de Mensagem

```
Entrada (RX):
$<COMANDO>:<PARÂMETRO>\n

Saída (TX):
<RESPOSTA>\n
```

### Exemplos de Comunicação

```
Cliente → Dispositivo: $ML:500
Dispositivo → Cliente: OK

Cliente → Dispositivo: $ML:500
Dispositivo → Cliente (2s): VP:125.5
Dispositivo → Cliente (4s): VP:250.3
Dispositivo → Cliente (6s): VP:375.8
Dispositivo → Cliente (8s): VP:500.0
Dispositivo → Cliente: QP:2940
Dispositivo → Cliente: ML:500

Cliente → Dispositivo: $LB:
Dispositivo → Cliente: OK
(Liberação contínua até novo comando)

Cliente → Dispositivo: $RI:A1B2C3D4
Dispositivo → Cliente: OK
```

---

## 11. Resumo das Regras de Negócio

| Regra | Descrição | Implementação |
|-------|-----------|----------------|
| **Precisão de Volume** | Liberar exatamente a quantidade solicitada | Via contagem de pulsos do sensor |
| **Feedback em Tempo Real** | Informar volume dispensado a cada 2s | Via BLE (comando VP) |
| **Autenticação RFID** | Identificar usuários por tag | RC522 com comparação de UID |
| **Admin Automático** | Administrador libera sem limite | Detecção de rfidMaster dispara LB |
| **Persistência** | Manter configurações após reset | EEPROM com magic flag |
| **Timeout de Segurança** | Interromper se sensor falhar | 2000ms sem pulsos |
| **Validação de Comandos** | Rejeitar comandos inválidos | Prefixo $ obrigatório |
| **Debounce RFID** | Evitar múltiplas leituras | 2 segundos entre leituras |
| **Status Visual** | Indicar conexão BLE | LED aceso quando conectado |

---

## 12. Conclusão

O sistema CHOPPE é um **dispensador automático de chopp** bem estruturado que combina:

- **Hardware:** Sensor de fluxo preciso, válvula solenoide, autenticação RFID
- **Comunicação:** BLE para controle remoto, RFID para autenticação local
- **Lógica:** Cálculo preciso de volume, feedback em tempo real, tratamento de erros
- **Persistência:** Configurações armazenadas em EEPROM

As regras de negócio implementadas garantem **precisão na dispensação**, **segurança via autenticação** e **flexibilidade de controle** tanto local (RFID) quanto remoto (BLE).

Pontos de melhoria identificados:
- Implementar autenticação BLE
- Adicionar suporte a múltiplos usuários RFID
- Ativar página web de configuração
- Implementar histórico de transações
- Criptografar credenciais WiFi
