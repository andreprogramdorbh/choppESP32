# Guia de Gravação do Firmware CHOPPE — VS Code + PlatformIO

**Projeto:** choppESP32  
**Placa suportada:** Lolin C3 Mini (ESP32-C3), ESP32-S3 DevKit, ESP32 Dev  
**Ferramenta:** Visual Studio Code + PlatformIO IDE  

---

## 1. Pré-requisitos

Antes de iniciar, certifique-se de ter instalado:

| Ferramenta | Versão mínima | Download |
|---|---|---|
| Visual Studio Code | 1.85+ | [code.visualstudio.com](https://code.visualstudio.com/) |
| PlatformIO IDE (extensão VS Code) | 6.x | Marketplace do VS Code |
| Driver USB da placa | — | Veja seção 1.1 abaixo |
| Git | 2.x | [git-scm.com](https://git-scm.com/) |

### 1.1 Drivers USB por placa

| Placa | Chip USB | Driver necessário |
|---|---|---|
| **Lolin C3 Mini** | USB nativo ESP32-C3 | Nenhum (CDC nativo) |
| **ESP32-S3 DevKit** | USB nativo ESP32-S3 | Nenhum (CDC nativo) |
| **ESP32 Dev (30/38 pinos)** | CP2102 ou CH340 | [CP2102](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) / [CH340](https://www.wch-ic.com/downloads/CH341SER_EXE.html) |

> **Atenção para o Lolin C3 Mini:** o cabo USB deve ser do tipo **dados** (não apenas carregamento). Cabos somente de carga não expõem a porta serial.

---

## 2. Instalação do PlatformIO no VS Code

**Passo 1.** Abra o VS Code e acesse a aba **Extensions** (Ctrl+Shift+X).

**Passo 2.** Pesquise por `PlatformIO IDE` e clique em **Install**. Aguarde a instalação completa (pode levar alguns minutos na primeira vez).

**Passo 3.** Após a instalação, o ícone do PlatformIO (formiga) aparecerá na barra lateral esquerda. Reinicie o VS Code se solicitado.

---

## 3. Clonar o repositório do firmware

Abra o terminal integrado do VS Code (Ctrl+`) e execute:

```bash
git clone https://github.com/choppon24h-png/choppESP32.git
cd choppESP32
```

Em seguida, abra a pasta no VS Code:

```
File → Open Folder → selecione a pasta choppESP32
```

O PlatformIO detectará automaticamente o arquivo `platformio.ini` e instalará as dependências (ESP32 Arduino core, bibliotecas MFRC522 e ESPAsyncWebServer).

---

## 4. Configurar o ambiente de compilação correto

O arquivo `platformio.ini` contém três ambientes. Selecione o correto para a sua placa:

| Ambiente | Placa | Quando usar |
|---|---|---|
| `esplolin_c3_mini32dev` | **Lolin C3 Mini** | Padrão do projeto |
| `esp32-s3-devkitc-1` | ESP32-S3 DevKit C-1 | Se usar S3 |
| `esp32dev` | ESP32 Dev (30/38 pinos) | Se usar ESP32 clássico |

Para selecionar o ambiente, clique na barra inferior do VS Code onde aparece o nome do ambiente atual (ex: `env:esplolin_c3_mini32dev`) e escolha o desejado.

### 4.1 Personalizar o nome BLE da unidade

Antes de gravar, **edite o arquivo `src/config.h`** e altere o `BLE_NAME` para identificar unicamente cada placa. Use os últimos 4 dígitos do MAC BLE da placa:

```cpp
// Exemplo: se o MAC BLE da placa for AA:BB:CC:DD:EE:F1
#define BLE_NAME "CHOPP_EEF1"
```

> O MAC BLE pode ser obtido via Serial Monitor após a primeira gravação — o log exibirá `[BLE] Aguardando conexao — Nome: CHOPP_XXXX`.

---

## 5. Compilar o firmware

**Opção A — Via interface gráfica:**
Clique no ícone de **visto (✓)** na barra inferior do VS Code (Build).

**Opção B — Via terminal:**
```bash
pio run -e esplolin_c3_mini32dev
```

Uma compilação bem-sucedida exibirá ao final:
```
RAM:   [=         ]  12.3% (used 40312 bytes from 327680 bytes)
Flash: [===       ]  27.8% (used 364512 bytes from 1310720 bytes)
====== [SUCCESS] Took 18.23 seconds
```

---

## 6. Conectar a placa e identificar a porta serial

**Passo 1.** Conecte a placa ao computador via cabo USB.

**Passo 2.** Para o **Lolin C3 Mini**, pode ser necessário colocar a placa em modo de gravação manualmente:
1. Pressione e **segure** o botão **BOOT** (ou GPIO9)
2. Pressione e solte o botão **RESET** (ou RST)
3. Solte o botão **BOOT**

**Passo 3.** Verifique a porta serial detectada:

```bash
# Linux/macOS
ls /dev/tty*

# Windows
# Abra o Gerenciador de Dispositivos → Portas (COM e LPT)
```

O PlatformIO detecta a porta automaticamente na maioria dos casos.

---

## 7. Gravar o firmware na placa

**Opção A — Via interface gráfica:**
Clique na seta (→) **Upload** na barra inferior do VS Code.

**Opção B — Via terminal:**
```bash
pio run -e esplolin_c3_mini32dev --target upload
```

**Opção C — Especificando a porta manualmente** (se a detecção automática falhar):
```bash
pio run -e esplolin_c3_mini32dev --target upload --upload-port /dev/ttyACM0
# No Windows: --upload-port COM3
```

Uma gravação bem-sucedida exibirá:
```
Writing at 0x00010000... (100 %)
Wrote 364512 bytes (...)
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
====== [SUCCESS] Took 8.41 seconds
```

---

## 8. Monitorar o Serial (verificar a operação)

Após a gravação, abra o Monitor Serial para verificar os logs:

**Opção A — Via interface gráfica:**
Clique no ícone de **tomada** (Serial Monitor) na barra inferior.

**Opção B — Via terminal:**
```bash
pio device monitor -b 115200
```

Saída esperada na inicialização:
```
[BLE] Seguranca BLE configurada — PIN: 259087
[BLE] Aguardando conexao — Nome: CHOPP_0001
```

Quando o Android conectar:
```
[BLE] Conectado
[BLE] Aguardando autenticacao (bond nativo ou $AUTH:PIN)
[BLE-SEC] onAuthenticationComplete — Autenticacao BLE nativa concluida com SUCESSO
[BLE] Autenticacao OK via bond nativo
```

Quando um comando de dispensação for recebido:
```
[BLE] Recebido: ML:300
[OPERACIONAL] Iniciando dispensacao: 300ml (1764 pulsos)
[OPERACIONAL] Volume parcial: 150ml
[OPERACIONAL] Dispensacao concluida: 300ml
```

---

## 9. Fluxo completo de conexão Android ↔ ESP32

O diagrama abaixo descreve a sequência completa desde o scan BLE até a dispensação:

```
ANDROID                                    ESP32 (CHOPP_XXXX)
   |                                              |
   |── Scan BLE (filtra startsWith "CHOPP_") ────>|
   |<─ Advertising (nome: CHOPP_0001) ────────────|
   |                                              |
   |── createBond() ──────────────────────────────>|
   |<─ Solicitação de PIN (ACTION_PAIRING_REQUEST) |
   |── PIN 259087 (injetado automaticamente) ─────>|
   |<─ BOND_BONDED ────────────────────────────────|
   |                                              |
   |── connectGatt() ─────────────────────────────>|
   |<─ STATE_CONNECTED ────────────────────────────|
   |── requestMtu(512) ────────────────────────────>|
   |<─ onMtuChanged ───────────────────────────────|
   |── discoverServices() ─────────────────────────>|
   |<─ onServicesDiscovered (NUS OK) ──────────────|
   |                                              |
   |── $AUTH:259087 (600ms após services) ─────────>|
   |<─ AUTH:OK ────────────────────────────────────|
   |                                              |
   |   [Estado: READY — botões habilitados]        |
   |                                              |
   |── ML:300 (dispensar 300ml) ──────────────────>|
   |<─ VP:150 (volume parcial) ────────────────────|
   |<─ VP:250 (volume parcial) ────────────────────|
   |<─ ML:300 (dispensação concluída) ─────────────|
```

---

## 10. Solução de problemas comuns

| Problema | Causa provável | Solução |
|---|---|---|
| `A fatal error occurred: Failed to connect to ESP32` | Placa não está em modo bootloader | Segurar BOOT + pressionar RESET antes do upload |
| `No device found on port` | Cabo sem dados ou driver ausente | Trocar cabo; instalar driver CH340/CP2102 |
| `BLESecurityCallbacks not found` | Versão antiga do ESP32 Arduino core | Atualizar plataforma: `pio pkg update` |
| Android não encontra o dispositivo | Nome BLE não começa com `CHOPP_` | Verificar `BLE_NAME` no `config.h` |
| `AUTH:FAIL` no Serial | PIN incorreto ou timing | Verificar `BLE_AUTH_PIN` no `config.h`; aguardar bond completar |
| `ERROR:NOT_AUTHENTICATED` | Comando enviado antes do AUTH:OK | Aguardar estado READY no app antes de enviar ML: |
| Dispositivo não reconecta após desligar | Bond corrompido | Desparear o dispositivo no Android (Config → Bluetooth) e reconectar |

---

## 11. Atualizar o firmware via OTA (Over-The-Air) — Futuro

O `platformio.ini` atual não possui OTA configurado. Para habilitar atualizações sem fio no futuro, adicione ao ambiente desejado:

```ini
[env:esplolin_c3_mini32dev]
upload_protocol = espota
upload_port = 192.168.x.x  ; IP da placa na rede WiFi
```

Isso requer que o firmware tenha o módulo `ArduinoOTA` habilitado e a partição OTA ativa (remover `board_build.partitions = no_ota.csv`).

---

*Documento gerado automaticamente pelo sistema de análise do projeto CHOPPE.*
