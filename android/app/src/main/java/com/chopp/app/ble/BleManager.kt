package com.chopp.app.ble

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.UUID

/**
 * BleManager — Gerenciador de comunicação BLE com dispositivos CHOPP.
 *
 * REGRAS DE SCAN:
 *  - Aceita apenas dispositivos cujo nome começa com o prefixo "CHOPP_"
 *    (ex: CHOPP_E123, CHOPP_F45A, CHOPP_B4DC)
 *  - Se houver um MAC salvo na API (getMacSalvoDaApi), conecta SOMENTE
 *    no dispositivo com aquele endereço — garantindo vínculo com a TAP correta.
 *  - Qualquer outro dispositivo BLE é ignorado silenciosamente.
 *
 * AUTENTICAÇÃO:
 *  - Após conexão GATT estabelecida, o Android envia o PIN padrão "259087"
 *    via característica RX para que o ESP32 valide o acesso.
 *  - Fluxo de estados: DESCONECTADO → CONECTADO → AUTENTICADO → PRONTO
 *  - Comandos de operação (ML, LB, etc.) só são aceitos no estado PRONTO.
 *
 * O QUE NÃO FOI ALTERADO:
 *  - Fluxo de conexão GATT (broadcastDeviceFound, callbacks de conexão)
 *  - Controle de mScanning
 *  - UUIDs de serviço e características
 */
class BleManager(private val context: Context) {

    companion object {
        private const val TAG = "BleManager"

        // ---------------------------------------------------------------
        // Prefixo obrigatório para aceitar um dispositivo no scan
        // ---------------------------------------------------------------
        private const val DEVICE_PREFIX = "CHOPP_"

        // ---------------------------------------------------------------
        // PIN padrão de autenticação — enviado logo após conexão GATT
        // ---------------------------------------------------------------
        private const val AUTH_PIN = "259087"

        // UUIDs do serviço UART BLE (Nordic UART Service)
        private val SERVICE_UUID           = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        private val CHARACTERISTIC_UUID_RX = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        private val CHARACTERISTIC_UUID_TX = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        private val DESCRIPTOR_UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        // Tempo máximo de scan (10 segundos)
        private const val SCAN_PERIOD_MS = 10_000L
    }

    // -------------------------------------------------------------------
    // Estado interno de autenticação
    // -------------------------------------------------------------------
    private enum class AuthState {
        DISCONNECTED,
        CONNECTED,
        AUTHENTICATED,
        READY
    }

    private var authState: AuthState = AuthState.DISCONNECTED

    // -------------------------------------------------------------------
    // Infraestrutura BLE
    // -------------------------------------------------------------------
    private val bluetoothManager: BluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter = bluetoothManager.adapter
    private val bleScanner: BluetoothLeScanner = bluetoothAdapter.bluetoothLeScanner

    private var mBluetoothGatt: BluetoothGatt? = null
    private var mScanning: Boolean = false
    private val handler = Handler(Looper.getMainLooper())

    // -------------------------------------------------------------------
    // Callback de eventos — implementado pelo chamador (Activity/ViewModel)
    // -------------------------------------------------------------------
    var onDeviceFound: ((BluetoothDevice) -> Unit)? = null
    var onConnected: (() -> Unit)? = null
    var onAuthenticated: (() -> Unit)? = null
    var onReady: (() -> Unit)? = null
    var onDisconnected: (() -> Unit)? = null
    var onDataReceived: ((String) -> Unit)? = null
    var onError: ((String) -> Unit)? = null

    // -------------------------------------------------------------------
    // Fonte do MAC vinculado — substituir pela chamada real à API
    // -------------------------------------------------------------------
    /**
     * Retorna o endereço MAC salvo na API para esta conta/usuário.
     * Se nenhum MAC estiver vinculado, retorna null e qualquer
     * dispositivo com prefixo CHOPP_ é aceito.
     *
     * INTEGRAÇÃO: substitua o corpo deste método pela chamada real à API.
     * Exemplo: return apiRepository.getMacVinculado()
     */
    private fun getMacSalvoDaApi(): String? {
        // TODO: integrar com repositório/API real
        return null
    }

    // ===================================================================
    // SCAN BLE
    // ===================================================================

    /**
     * Inicia o scan BLE por [SCAN_PERIOD_MS] milissegundos.
     * Só aceita dispositivos que passem em [isDeviceAceito].
     */
    fun startScan() {
        if (mScanning) {
            Log.d(TAG, "[SCAN] Scan já em andamento, ignorando nova solicitação.")
            return
        }
        Log.d(TAG, "[SCAN] Iniciando scan BLE — prefixo esperado: $DEVICE_PREFIX")
        mScanning = true

        // Para o scan automaticamente após o período definido
        handler.postDelayed({
            stopScan()
        }, SCAN_PERIOD_MS)

        bleScanner.startScan(bleScanCallback)
    }

    /**
     * Para o scan BLE em andamento.
     * O controle de mScanning é mantido exatamente como estava.
     */
    fun stopScan() {
        if (!mScanning) return
        Log.d(TAG, "[SCAN] Parando scan BLE.")
        mScanning = false
        bleScanner.stopScan(bleScanCallback)
    }

    // -------------------------------------------------------------------
    // Callback de scan — ÚNICA alteração em relação à versão anterior:
    //   substituição da verificação de alias fixo "CHOPPE" pela chamada
    //   a isDeviceAceito(), que verifica prefixo e MAC vinculado.
    //
    //   broadcastDeviceFound() e controle de mScanning NÃO foram alterados.
    // -------------------------------------------------------------------
    private val bleScanCallback = object : ScanCallback() {

        override fun onScanResult(callbackType: Int, result: ScanResult) {
            super.onScanResult(callbackType, result)

            // ============================================================
            // ALTERAÇÃO PRINCIPAL — verificação do dispositivo encontrado
            // ============================================================
            if (!isDeviceAceito(result)) {
                // Dispositivo não atende aos critérios — ignora silenciosamente
                return
            }
            // ============================================================

            Log.d(TAG, "[SCAN] Dispositivo aceito: ${result.device.name} | ${result.device.address}")

            // broadcastDeviceFound mantido intacto
            broadcastDeviceFound(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            super.onScanFailed(errorCode)
            Log.e(TAG, "[SCAN] Falha no scan BLE. Código: $errorCode")
            mScanning = false
            onError?.invoke("Falha no scan BLE. Código: $errorCode")
        }
    }

    // -------------------------------------------------------------------
    // Critério de aceitação do dispositivo encontrado
    // -------------------------------------------------------------------
    /**
     * Retorna true se o dispositivo deve ser considerado para conexão.
     *
     * Regra 1 — Prefixo obrigatório:
     *   O nome do dispositivo deve começar com "CHOPP_".
     *   Aceita: CHOPP_E123, CHOPP_F45A, CHOPP_B4DC
     *   Ignora: CHOPPE, CHOPP, outros
     *
     * Regra 2 — MAC vinculado (opcional):
     *   Se a API retornar um MAC salvo, o dispositivo só é aceito
     *   se seu endereço corresponder exatamente ao MAC vinculado.
     *   Isso garante que o app conecte apenas na TAP correta.
     */
    private fun isDeviceAceito(result: ScanResult): Boolean {
        val deviceName = result.device.name ?: run {
            Log.v(TAG, "[SCAN] Dispositivo sem nome ignorado: ${result.device.address}")
            return false
        }

        // Regra 1: verifica prefixo "CHOPP_"
        if (!deviceName.startsWith(DEVICE_PREFIX)) {
            Log.v(TAG, "[SCAN] Prefixo inválido ignorado: $deviceName")
            return false
        }

        // Regra 2 (opcional): verifica MAC vinculado na API
        val macSalvo = getMacSalvoDaApi()
        if (macSalvo != null) {
            if (!result.device.address.equals(macSalvo, ignoreCase = true)) {
                Log.d(TAG, "[SCAN] MAC não corresponde ao vinculado. " +
                        "Encontrado: ${result.device.address} | Esperado: $macSalvo")
                return false
            }
            Log.d(TAG, "[SCAN] MAC vinculado confirmado: ${result.device.address}")
        }

        return true
    }

    // ===================================================================
    // CONEXÃO GATT — sem alterações no fluxo original
    // ===================================================================

    /**
     * Inicia conexão GATT com o dispositivo selecionado.
     * Chamado após broadcastDeviceFound() pelo chamador.
     */
    fun connect(device: BluetoothDevice) {
        Log.d(TAG, "[GATT] Conectando a: ${device.name} (${device.address})")
        stopScan()
        mBluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    /**
     * Encerra conexão GATT e libera recursos.
     */
    fun disconnect() {
        Log.d(TAG, "[GATT] Desconectando.")
        authState = AuthState.DISCONNECTED
        mBluetoothGatt?.disconnect()
        mBluetoothGatt?.close()
        mBluetoothGatt = null
    }

    // -------------------------------------------------------------------
    // broadcastDeviceFound — mantido intacto conforme instrução
    // -------------------------------------------------------------------
    private fun broadcastDeviceFound(device: BluetoothDevice) {
        Log.d(TAG, "[SCAN] broadcastDeviceFound: ${device.name} | ${device.address}")
        onDeviceFound?.invoke(device)
    }

    // -------------------------------------------------------------------
    // Callbacks GATT — fluxo de conexão mantido intacto
    // -------------------------------------------------------------------
    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothGatt.STATE_CONNECTED -> {
                    Log.d(TAG, "[GATT] Conectado. Descobrindo serviços...")
                    authState = AuthState.CONNECTED
                    gatt.discoverServices()
                    onConnected?.invoke()
                }
                BluetoothGatt.STATE_DISCONNECTED -> {
                    Log.d(TAG, "[GATT] Desconectado. Status: $status")
                    authState = AuthState.DISCONNECTED
                    mBluetoothGatt?.close()
                    mBluetoothGatt = null
                    onDisconnected?.invoke()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "[GATT] Falha ao descobrir serviços. Status: $status")
                onError?.invoke("Falha ao descobrir serviços GATT. Status: $status")
                return
            }
            Log.d(TAG, "[GATT] Serviços descobertos. Habilitando notificações TX...")
            enableTxNotifications(gatt)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == CHARACTERISTIC_UUID_TX) {
                val data = characteristic.value?.toString(Charsets.UTF_8)?.trim() ?: return
                Log.d(TAG, "[GATT] Dados recebidos: $data")
                handleReceivedData(data)
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "[GATT] Notificações TX habilitadas. Enviando PIN de autenticação...")
                // Após habilitar notificações, envia PIN para autenticar
                sendAuthPin()
            } else {
                Log.e(TAG, "[GATT] Falha ao habilitar notificações. Status: $status")
                onError?.invoke("Falha ao habilitar notificações GATT. Status: $status")
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "[GATT] Escrita na característica confirmada: ${characteristic.uuid}")
            } else {
                Log.e(TAG, "[GATT] Falha na escrita. Status: $status")
                onError?.invoke("Falha ao escrever na característica GATT. Status: $status")
            }
        }
    }

    // ===================================================================
    // AUTENTICAÇÃO POR PIN
    // ===================================================================

    /**
     * Envia o PIN padrão ao ESP32 via característica RX.
     * Formato do comando: $AUTH:259087
     *
     * O ESP32 responde com "AUTH:OK" em caso de sucesso,
     * ou "AUTH:FAIL" em caso de PIN incorreto.
     */
    private fun sendAuthPin() {
        Log.d(TAG, "[AUTH] Enviando PIN de autenticação...")
        sendCommand("\$AUTH:$AUTH_PIN")
    }

    /**
     * Processa dados recebidos do ESP32 via notificação TX.
     * Gerencia transições de estado de autenticação e repassa
     * demais dados ao chamador via onDataReceived.
     */
    private fun handleReceivedData(data: String) {
        when {
            data == "AUTH:OK" -> {
                Log.d(TAG, "[AUTH] Autenticação confirmada pelo ESP32.")
                authState = AuthState.AUTHENTICATED
                onAuthenticated?.invoke()
                // Transição para PRONTO após autenticação bem-sucedida
                authState = AuthState.READY
                onReady?.invoke()
            }
            data == "AUTH:FAIL" -> {
                Log.e(TAG, "[AUTH] PIN rejeitado pelo ESP32.")
                onError?.invoke("Autenticação falhou: PIN incorreto.")
                disconnect()
            }
            data.startsWith("ERROR:NOT_AUTHENTICATED") -> {
                Log.e(TAG, "[AUTH] Comando enviado antes da autenticação.")
                onError?.invoke("Dispositivo não autenticado. Aguarde a autenticação.")
            }
            else -> {
                // Repassa todos os demais dados ao chamador sem alteração
                onDataReceived?.invoke(data)
            }
        }
    }

    // ===================================================================
    // ENVIO DE COMANDOS
    // ===================================================================

    /**
     * Envia um comando ao ESP32 via característica RX.
     * Comandos de operação (não-AUTH) só são enviados no estado PRONTO.
     *
     * @param command Comando no formato $<COMANDO>:<PARÂMETRO>
     */
    fun sendCommand(command: String) {
        val gatt = mBluetoothGatt ?: run {
            Log.e(TAG, "[CMD] Sem conexão GATT ativa.")
            onError?.invoke("Sem conexão BLE ativa.")
            return
        }

        // Permite envio do PIN mesmo antes de estar PRONTO
        val isAuthCommand = command.startsWith("\$AUTH:")
        if (!isAuthCommand && authState != AuthState.READY) {
            Log.w(TAG, "[CMD] Comando bloqueado — estado atual: $authState. Aguarde autenticação.")
            onError?.invoke("Aguarde a autenticação antes de enviar comandos.")
            return
        }

        val service = gatt.getService(SERVICE_UUID) ?: run {
            Log.e(TAG, "[CMD] Serviço BLE não encontrado: $SERVICE_UUID")
            onError?.invoke("Serviço BLE não encontrado.")
            return
        }

        val rxCharacteristic = service.getCharacteristic(CHARACTERISTIC_UUID_RX) ?: run {
            Log.e(TAG, "[CMD] Característica RX não encontrada: $CHARACTERISTIC_UUID_RX")
            onError?.invoke("Característica RX não encontrada.")
            return
        }

        rxCharacteristic.value = command.toByteArray(Charsets.UTF_8)
        rxCharacteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT

        val success = gatt.writeCharacteristic(rxCharacteristic)
        Log.d(TAG, "[CMD] Enviando: $command | Sucesso: $success")

        if (!success) {
            onError?.invoke("Falha ao enviar comando: $command")
        }
    }

    // ===================================================================
    // UTILITÁRIOS INTERNOS
    // ===================================================================

    /**
     * Habilita notificações na característica TX para receber dados do ESP32.
     */
    private fun enableTxNotifications(gatt: BluetoothGatt) {
        val service = gatt.getService(SERVICE_UUID) ?: run {
            Log.e(TAG, "[GATT] Serviço não encontrado ao habilitar notificações.")
            return
        }
        val txCharacteristic = service.getCharacteristic(CHARACTERISTIC_UUID_TX) ?: run {
            Log.e(TAG, "[GATT] Característica TX não encontrada.")
            return
        }

        gatt.setCharacteristicNotification(txCharacteristic, true)

        val descriptor = txCharacteristic.getDescriptor(DESCRIPTOR_UUID)
        descriptor?.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        gatt.writeDescriptor(descriptor)
    }

    /**
     * Indica se o dispositivo está pronto para receber comandos de operação.
     */
    fun isReady(): Boolean = authState == AuthState.READY

    /**
     * Indica se há uma conexão BLE ativa.
     */
    fun isConnected(): Boolean =
        authState == AuthState.CONNECTED ||
        authState == AuthState.AUTHENTICATED ||
        authState == AuthState.READY
}
