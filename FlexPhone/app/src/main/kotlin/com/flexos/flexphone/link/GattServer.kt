package com.flexos.flexphone.link

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.content.Context
import android.os.Build
import com.flexos.flexphone.protocol.FlexLink
import com.flexos.flexphone.protocol.LinkDiag
import com.flexos.flexphone.protocol.PairSubmit
import com.flexos.flexphone.protocol.PairingSlot
import com.flexos.flexphone.protocol.PhoneLinkPeer
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

/**
 * TRANSPORTE BLE DE FLEX LINK v2: el telefono es el PERIFERICO y Flex OS
 * Watch Ultra (ESP32-S3, NimBLE) el central.
 *
 * Antes este fichero estaba escrito y apagado: el P4 no tiene radio
 * Bluetooth. El reloj si, y su canal con el telefono es este (decision D5 del
 * reloj). El P4 sigue por [WifiLinkServer]; los dos transportes corren a la
 * vez y la lista de dispositivos los une.
 *
 * QUE HACE CADA PIEZA
 * -------------------
 *   · Este fichero: anuncio, servicio GATT, una [Conn] por reloj conectado,
 *     MTU, suscripcion (CCCD) y una cola de salida acotada que manda las
 *     notificaciones DE UNA EN UNA (BLE no admite otra hasta que la anterior
 *     sale; mandarlas de golpe hace que la pila las tire en silencio).
 *   · [PhoneLinkPeer] (:protocol, probado en el PC y contra el motor C del
 *     reloj): TODO el protocolo -- saludo, codigo, reto mutuo, latido, ACK,
 *     reensamblado, anti-repeticion y plazos.
 *
 * HILOS
 * -----
 * Las llamadas de Android llegan por hilos del binder. Todo lo que toca un
 * par o una cola pasa por UN hilo propio (`exec`): ni cerrojos repartidos ni
 * dos hilos escribiendo en la misma conexion. La interfaz (teclear el codigo)
 * tambien entra por ahi.
 *
 * REGLAS DEL CANAL (las que el reloj exige, docs/F5-CONECTIVIDAD.md §3.2)
 *   · una escritura en RX = una trama; una notificacion de TX = una trama;
 *   · nada sale antes de que el reloj se suscriba a TX ni con MTU < 247
 *     (tramas de 244 B): se espera, no se trocea distinto;
 *   · caracteristicas con PERMISOS CIFRADOS: Android exige emparejar a nivel
 *     BLE antes de entregar un byte. La autenticacion de verdad la pone v2.
 *   · el anuncio lleva SOLO el UUID del servicio, sin nombre del telefono.
 */
class GattServer(
    private val ctx: Context,
    private val host: PhoneLinkPeer.Host,
    private val onEvent: (Event) -> Unit,
    private val onMessage: (conn: String, peer: PhoneLinkPeer, type: Int, payload: ByteArray) -> Unit,
) {
    companion object {
        private const val TAG = "FlexPhone/Gatt"

        /** Servicio Flex Link. UUID propio, no uno estandar reutilizado. */
        val SERVICE: UUID = UUID.fromString("f1580001-4c65-4f53-8f6c-6578f1584c4b")
        /** Flex OS ESCRIBE aqui (telefono <- Flex OS). */
        val RX: UUID = UUID.fromString("f1580002-4c65-4f53-8f6c-6578f1584c4b")
        /** Flex OS se SUSCRIBE aqui (telefono -> Flex OS). */
        val TX: UUID = UUID.fromString("f1580003-4c65-4f53-8f6c-6578f1584c4b")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        /** MTU minimo: tramas de 244 B + 3 de cabecera ATT. */
        const val NEEDED_MTU = FlexLink.MAX_FRAME + 3
        /** Tramas pendientes por conexion (un mensaje de 2 KB son 10). */
        private const val OUTBOX_MAX = 64
        /** Relojes a la vez. Es un reloj por persona; el tope protege de un vecino pesado. */
        private const val MAX_CONNS = 3
        /** Una notificacion sin confirmar en este tiempo se da por perdida y se sigue. */
        private const val NOTIFY_STUCK_MS = 2_000L
    }

    sealed class Event {
        data class Advertising(val on: Boolean, val why: String? = null) : Event()
        data class ChannelOpened(val conn: String, val address: String) : Event()
        data class Peer(val conn: String, val address: String, val ev: PhoneLinkPeer.Event) : Event()
        data class ChannelClosed(val conn: String, val authed: Boolean, val flexosId: String) : Event()
    }

    /** Un reloj conectado. Solo se toca desde `exec`. */
    private inner class Conn(val device: BluetoothDevice, val key: String) {
        val address: String = runCatching { device.address }.getOrDefault("")
        var mtu = 23
        var subscribed = false
        val outbox = ArrayDeque<ByteArray>()
        var inFlightSince = 0L
        val peer: PhoneLinkPeer = PhoneLinkPeer(host, pairing, { f -> enqueue(this, f) }, peerListener, key)
        var dropped = 0
    }

    private val pairing = PairingSlot()
    private val exec: ScheduledExecutorService = Executors.newSingleThreadScheduledExecutor { r ->
        Thread(r, "flex-gatt").apply { isDaemon = true }
    }
    private val conns = java.util.concurrent.ConcurrentHashMap<String, Conn>()   // por direccion BLE; se escribe solo en exec
    private val seq = AtomicLong(0)
    @Volatile private var server: BluetoothGattServer? = null
    @Volatile private var txChar: BluetoothGattCharacteristic? = null
    @Volatile private var advertiser: android.bluetooth.le.BluetoothLeAdvertiser? = null
    @Volatile var running = false
        private set
    @Volatile var lastError: String? = null
        private set

    private val peerListener = object : PhoneLinkPeer.Listener {
        override fun onEvent(peer: PhoneLinkPeer, ev: PhoneLinkPeer.Event) {
            val c = conns.values.firstOrNull { it.peer === peer } ?: return
            safe { this@GattServer.onEvent(Event.Peer(c.key, c.address, ev)) }
            if (ev is PhoneLinkPeer.Event.CloseRequested) disconnect(c)
        }
        override fun onMessage(peer: PhoneLinkPeer, type: Int, payload: ByteArray) {
            val c = conns.values.firstOrNull { it.peer === peer } ?: return
            safe { this@GattServer.onMessage(c.key, peer, type, payload) }
        }
    }

    // =========================================================
    //  Arranque y parada
    // =========================================================
    /** Devuelve el motivo si no se puede (sin Bluetooth, apagado, permisos). null = arrancado. */
    @SuppressLint("MissingPermission")
    fun start(): String? {
        if (running) return null
        val mgr = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            ?: return fail("este telefono no tiene Bluetooth")
        val adapter = mgr.adapter ?: return fail("este telefono no tiene Bluetooth")
        if (!hasPermissions(ctx)) return fail("faltan los permisos de Bluetooth (dispositivos cercanos)")
        if (!adapter.isEnabled) return fail("el Bluetooth esta apagado")
        return try {
            val srv = mgr.openGattServer(ctx, callback) ?: return fail("Android no dejo abrir el servidor GATT")
            val svc = BluetoothGattService(SERVICE, BluetoothGattService.SERVICE_TYPE_PRIMARY)
            val rx = BluetoothGattCharacteristic(
                RX,
                BluetoothGattCharacteristic.PROPERTY_WRITE or BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE,
                BluetoothGattCharacteristic.PERMISSION_WRITE_ENCRYPTED,
            )
            val tx = BluetoothGattCharacteristic(
                TX, BluetoothGattCharacteristic.PROPERTY_NOTIFY, BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED,
            ).apply {
                addDescriptor(BluetoothGattDescriptor(
                    CCCD, BluetoothGattDescriptor.PERMISSION_READ or BluetoothGattDescriptor.PERMISSION_WRITE_ENCRYPTED,
                ))
            }
            svc.addCharacteristic(rx)
            svc.addCharacteristic(tx)
            server = srv
            txChar = tx
            srv.addService(svc)
            advertise(adapter)
            running = true
            lastError = null
            exec.scheduleWithFixedDelay({ safe { tickAll() } }, 1, 1, TimeUnit.SECONDS)
            LinkDiag.d(TAG, "servidor GATT arriba")
            null
        } catch (e: SecurityException) {
            fail("faltan los permisos de Bluetooth (dispositivos cercanos)")
        } catch (e: Exception) {
            fail("no se pudo iniciar el Bluetooth: ${e.javaClass.simpleName}")
        }
    }

    private fun fail(why: String): String { lastError = why; return why }

    @SuppressLint("MissingPermission")
    private fun advertise(adapter: BluetoothAdapter) {
        val adv = adapter.bluetoothLeAdvertiser
        if (adv == null) {
            safe { onEvent(Event.Advertising(false, "este telefono no puede anunciarse por BLE")) }
            return
        }
        advertiser = adv
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_BALANCED)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM)
            .setConnectable(true)
            .setTimeout(0)
            .build()
        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .addServiceUuid(android.os.ParcelUuid(SERVICE))
            .build()
        runCatching { adv.startAdvertising(settings, data, advCallback) }
            .onFailure { safe { onEvent(Event.Advertising(false, "Android rechazo el anuncio")) } }
    }

    private val advCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) {
            safe { onEvent(Event.Advertising(true)) }
        }
        override fun onStartFailure(errorCode: Int) {
            val why = when (errorCode) {
                ADVERTISE_FAILED_DATA_TOO_LARGE -> "el anuncio no cabe"
                ADVERTISE_FAILED_TOO_MANY_ADVERTISERS -> "hay demasiados anuncios activos en el telefono"
                ADVERTISE_FAILED_ALREADY_STARTED -> return
                ADVERTISE_FAILED_FEATURE_UNSUPPORTED -> "este telefono no puede anunciarse por BLE"
                else -> "Android rechazo el anuncio"
            }
            safe { onEvent(Event.Advertising(false, why)) }
        }
    }

    /** Despedida limpia de todas las sesiones y fuera. Bloquea como mucho 1 s. */
    @SuppressLint("MissingPermission")
    fun stop() {
        if (!running) return
        running = false
        runCatching {
            exec.submit {
                for (c in conns.values.toList()) {
                    c.peer.close("el telefono apago el enlace")   // manda BYE si hay sesion
                    flush(c)
                }
            }.get(1, TimeUnit.SECONDS)
        }
        runCatching { advertiser?.stopAdvertising(advCallback) }
        runCatching { for (c in conns.values.toList()) server?.cancelConnection(c.device) }
        runCatching { server?.close() }
        server = null; txChar = null; advertiser = null
        exec.shutdownNow()
        conns.clear()
        pairing.clear()
    }

    // =========================================================
    //  Lo que pide el servicio
    // =========================================================
    /** Manda a TODOS los relojes con sesion. Cuantos lo aceptaron. */
    fun sendAll(type: Int, payload: ByteArray): Int {
        if (!running) return 0
        return runCatching {
            exec.submit<Int> { conns.values.count { it.peer.authed && it.peer.send(type, payload) } }
                .get(500, TimeUnit.MILLISECONDS)
        }.getOrDefault(0)
    }

    /** Manda por una conexion concreta (la respuesta a una orden de ese reloj). */
    fun sendTo(conn: String, type: Int, payload: ByteArray): Boolean {
        if (!running) return false
        return runCatching {
            exec.submit<Boolean> { conns[conn]?.peer?.send(type, payload) == true }.get(500, TimeUnit.MILLISECONDS)
        }.getOrDefault(false)
    }

    fun hasSession(): Boolean = conns.values.any { it.peer.authed }
    fun sessionCount(): Int = conns.values.count { it.peer.authed }

    fun isAwaitingCode(): Boolean = pairing.awaitingCode(host.now())
    fun pairingRemainingMs(): Long = pairing.remainingMs(host.now())

    /** El usuario tecleo el codigo que ensena el reloj. */
    fun submitPairingCode(code: String): PairSubmit {
        if (!running) return PairSubmit.LINK_DOWN
        return runCatching {
            exec.submit<PairSubmit> {
                val p = pairing.peer ?: conns.values.firstOrNull { !it.peer.authed }?.peer
                p?.submitCode(code) ?: if (pairing.session != null) PairSubmit.LINK_DOWN else PairSubmit.NO_SESSION
            }.get(2, TimeUnit.SECONDS)
        }.getOrDefault(PairSubmit.LINK_DOWN)
    }

    /** Cierra la sesion con ese Flex OS (olvidar el vinculo). */
    fun dropSession(flexosId: String) {
        if (!running) return
        exec.execute { conns.values.filter { it.peer.flexosId == flexosId }.forEach { it.peer.close("vinculo olvidado"); disconnect(it) } }
    }

    // =========================================================
    //  Salida: una notificacion cada vez
    // =========================================================
    private fun enqueue(c: Conn, frame: ByteArray): Boolean {
        if (c.outbox.size >= OUTBOX_MAX) { c.dropped++; return false }
        c.outbox.addLast(frame)
        pump(c)
        return true
    }

    @SuppressLint("MissingPermission")
    private fun pump(c: Conn) {
        if (c.inFlightSince != 0L) return
        if (!c.subscribed || c.mtu < NEEDED_MTU) return          // el reloj aun no esta listo
        val frame = c.outbox.firstOrNull() ?: return
        val srv = server ?: return
        val ch = txChar ?: return
        val ok = runCatching {
            if (Build.VERSION.SDK_INT >= 33) {
                srv.notifyCharacteristicChanged(c.device, ch, false, frame) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                ch.value = frame
                @Suppress("DEPRECATION")
                srv.notifyCharacteristicChanged(c.device, ch, false)
            }
        }.getOrDefault(false)
        if (ok) {
            c.outbox.removeFirst()
            c.inFlightSince = System.currentTimeMillis()
        }
        // Si Android no la acepto ahora (cola llena del controlador), se reintenta
        // en el proximo tick: la trama sigue la primera de la cola.
    }

    /** Intenta vaciar lo pendiente antes de cerrar (la despedida). */
    private fun flush(c: Conn) {
        repeat(4) { c.inFlightSince = 0L; pump(c) }
    }

    private fun tickAll() {
        val now = System.currentTimeMillis()
        for (c in conns.values.toList()) {
            if (c.inFlightSince != 0L && now - c.inFlightSince > NOTIFY_STUCK_MS) { c.inFlightSince = 0L }
            pump(c)
            c.peer.tick()?.let { disconnect(c) }
        }
    }

    @SuppressLint("MissingPermission")
    private fun disconnect(c: Conn) {
        runCatching { server?.cancelConnection(c.device) }
    }

    // =========================================================
    //  Llamadas de Android (hilos del binder -> exec)
    // =========================================================
    private val callback = object : BluetoothGattServerCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(device: BluetoothDevice?, status: Int, newState: Int) {
            if (device == null) return
            exec.execute {
                val addr = runCatching { device.address }.getOrDefault("")
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    if (conns.containsKey(addr)) return@execute
                    if (conns.size >= MAX_CONNS) {
                        LinkDiag.w(TAG, "demasiados centrales conectados: se rechaza uno")
                        runCatching { server?.cancelConnection(device) }
                        return@execute
                    }
                    val c = Conn(device, "ble#" + seq.incrementAndGet())
                    conns[addr] = c
                    LinkDiag.d(TAG, "${c.key} conectado")     // sin la direccion MAC en el registro
                    safe { onEvent(Event.ChannelOpened(c.key, addr)) }
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    val c = conns.remove(addr) ?: return@execute
                    val authed = c.peer.authed
                    c.peer.close("BLE desconectado", sayBye = false)
                    c.outbox.clear()
                    LinkDiag.d(TAG, "${c.key} desconectado (estado $status)")
                    safe { onEvent(Event.ChannelClosed(c.key, authed, c.peer.flexosId)) }
                }
            }
        }

        override fun onMtuChanged(device: BluetoothDevice?, mtu: Int) {
            if (device == null) return
            exec.execute {
                val c = conns[runCatching { device.address }.getOrDefault("")] ?: return@execute
                c.mtu = mtu
                LinkDiag.d(TAG, "${c.key} MTU $mtu")
                if (mtu < NEEDED_MTU) {
                    LinkDiag.w(TAG, "${c.key}: MTU $mtu < $NEEDED_MTU, el reloj no puede recibir tramas de 244 B")
                }
                pump(c)
            }
        }

        @SuppressLint("MissingPermission")
        override fun onCharacteristicWriteRequest(
            device: BluetoothDevice?, requestId: Int,
            characteristic: BluetoothGattCharacteristic?,
            preparedWrite: Boolean, responseNeeded: Boolean,
            offset: Int, value: ByteArray?,
        ) {
            val ok = characteristic?.uuid == RX && !preparedWrite && offset == 0 && value != null
            if (responseNeeded) {
                runCatching {
                    server?.sendResponse(device, requestId,
                        if (ok) BluetoothGatt.GATT_SUCCESS else BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null)
                }
            }
            if (!ok || device == null) return
            val frame = value!!.copyOf()
            exec.execute {
                val c = conns[runCatching { device.address }.getOrDefault("")] ?: return@execute
                c.peer.onFrame(frame)
            }
        }

        @SuppressLint("MissingPermission")
        override fun onDescriptorWriteRequest(
            device: BluetoothDevice?, requestId: Int,
            descriptor: BluetoothGattDescriptor?, preparedWrite: Boolean,
            responseNeeded: Boolean, offset: Int, value: ByteArray?,
        ) {
            if (responseNeeded) {
                runCatching { server?.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, null) }
            }
            if (descriptor?.uuid != CCCD || device == null) return
            val on = value != null && value.size >= 2 && (value[0].toInt() and 0x01) != 0
            exec.execute {
                val c = conns[runCatching { device.address }.getOrDefault("")] ?: return@execute
                c.subscribed = on
                LinkDiag.d(TAG, "${c.key} ${if (on) "suscrito" else "sin suscripcion"}")
                pump(c)
            }
        }

        override fun onNotificationSent(device: BluetoothDevice?, status: Int) {
            if (device == null) return
            exec.execute {
                val c = conns[runCatching { device.address }.getOrDefault("")] ?: return@execute
                c.inFlightSince = 0L
                pump(c)
            }
        }
    }

    private inline fun safe(block: () -> Unit) {
        try { block() } catch (e: Exception) { LinkDiag.w(TAG, "oyente: ${e.javaClass.simpleName}") }
    }
}

/** Permisos de Bluetooth que el periferico necesita en ESTA version de Android. */
fun bluetoothPermissions(): Array<String> = if (Build.VERSION.SDK_INT >= 31)
    arrayOf(android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_ADVERTISE)
else arrayOf(android.Manifest.permission.BLUETOOTH, android.Manifest.permission.BLUETOOTH_ADMIN)

fun hasPermissions(ctx: Context): Boolean = bluetoothPermissions().all {
    ctx.checkSelfPermission(it) == android.content.pm.PackageManager.PERMISSION_GRANTED
}
