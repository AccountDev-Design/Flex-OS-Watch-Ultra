package com.flexos.flexphone.link

import android.app.*
import android.bluetooth.BluetoothAdapter
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.flexos.flexphone.MainActivity
import com.flexos.flexphone.R
import com.flexos.flexphone.device.DeviceAdapter
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.media.MediaBridge
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.notifications.ReplyManager
import com.flexos.flexphone.protocol.*
import com.flexos.flexphone.relay.BrowserRelayService
import com.flexos.flexphone.storage.BondStore
import kotlinx.coroutines.*

/**
 * SERVICIO DEL ENLACE: el telefono con TODOS sus Flex OS a la vez.
 *
 *   · Flex OS Ultra (ESP32-P4, sin radio Bluetooth) llega por Wi-Fi:
 *     [WifiLinkServer], TCP 47820 y busqueda UDP 47821. Ese camino NO cambia.
 *   · Flex OS Watch Ultra (ESP32-S3) llega por Bluetooth LE: [GattServer], con
 *     el lado telefono de Flex Link v2 en [PhoneLinkPeer].
 *
 * Los dos transportes corren a la vez y son independientes: perder la Wi-Fi
 * no corta el BLE del reloj y apagar el Bluetooth no corta el P4.
 *
 * UNA LISTA, NO UNA POR CAMINO
 * ----------------------------
 * Cada canal (socket o conexion GATT) se anota en [DeviceRegistry] con lo que
 * se sabe de el. La pantalla ve una fila por aparato aunque se le vea por
 * varios caminos (vinculo + busqueda + sesion).
 *
 * MENSAJES
 * --------
 *   · Lo que nace en el telefono (notificaciones, multimedia, relay, estado)
 *     sale por TODAS las sesiones abiertas (`state.sender`).
 *   · La respuesta a una orden sale por EL MISMO canal por el que llego.
 *
 * Es un servicio en primer plano porque eso es lo que Android pide para un
 * enlace persistente con la pantalla apagada. La notificacion dice la verdad:
 * cuantos Flex OS hay conectados, no "Conectado" a secas.
 */
class FlexLinkService : Service() {

    companion object {
        private const val TAG = "FlexPhone/LinkSvc"
        private const val CHANNEL = "flexlink"
        private const val NOTIF_ID = 1001
        const val ACTION_START = "com.flexos.flexphone.START_LINK"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_LINK"
        /** Cada cuanto se manda el estado del telefono si NADA cambio. */
        private const val STATE_HEARTBEAT_MS = 60_000L

        fun start(ctx: Context) {
            val i = Intent(ctx, FlexLinkService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, FlexLinkService::class.java).setAction(ACTION_STOP))
        }
        /** El servicio vivo, para que la interfaz pueda entregar el codigo. */
        @Volatile var current: FlexLinkService? = null
    }

    private var server: WifiLinkServer? = null
    private var gatt: GattServer? = null
    private var media: MediaBridge? = null
    private lateinit var bonds: BondStore
    private lateinit var device: DeviceAdapter
    private val scope = CoroutineScope(
        SupervisorJob() + Dispatchers.Default +
            CoroutineExceptionHandler { _, e -> LinkDiag.w(TAG, "corrutina del servicio fallo: ${e.javaClass.simpleName}") },
    )
    private lateinit var state: FlexPhoneState

    // UNA SOLA MAQUINA DE FASES para la pantalla del enlace (LinkPhase.kt,
    // probada en el PC). Con varios aparatos, un canal que se cierra solo
    // cuenta como "enlace caido" si no queda NINGUNA otra sesion abierta.
    private lateinit var machine: LinkPhaseMachine
    private val phaseLock = Any()
    private val registry = DeviceRegistry()
    private var netCb: ConnectivityManager.NetworkCallback? = null
    @Volatile private var lastWifiIp: String? = null
    @Volatile private var started = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        LinkDiag.d(TAG, "SERVICE_CREATED")
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        bonds = BondStore(this)
        device = DeviceAdapter(this)
        machine = LinkPhaseMachine(bonded = bonds.isPaired())
        createChannel()
        current = this
        refreshDevices(bondsChanged = true)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        LinkDiag.d(TAG, "SERVICE_STARTED action=${intent?.action} ya=${started}")
        when (intent?.action) {
            ACTION_STOP -> { shutdown(); return START_NOT_STICKY }
        }
        startForegroundHonestly()
        if (!started) {
            started = true
            onLinkEvent(LinkEvent.Started)
            val wifiOk = startWifi()
            val bleWhy = startBle()
            if (!wifiOk && bleWhy != null) {
                // Ningun camino: se dice por que y se para (un servicio en primer
                // plano sin nada que escuchar solo gasta bateria).
                state.setLink(LinkState.UNAVAILABLE, "${getString(R.string.link_needs_wifi)} · Bluetooth: $bleWhy")
                updateNotification()
                started = false
                stopSelf()
                return START_NOT_STICKY
            }
            state.sender = { type, payload -> sendAll(type, payload) }
            BrowserRelayService.restoreIfWanted(this)
            media = MediaBridge(this, state).also { it.start() }
            lastWifiIp = NetAddress.wifiIpv4(this)
            state.setAddress(lastWifiIp)
            watchNetwork()
            watchBluetooth()
            loop()
        }
        return START_NOT_STICKY
    }

    // =========================================================
    //  Transportes
    // =========================================================
    /** El servidor Wi-Fi del P4. Solo con Wi-Fi: si llega despues, se arranca entonces. */
    private fun startWifi(): Boolean {
        if (server != null) return true
        if (!device.isOnWifi()) return false
        val s = WifiLinkServer(
            ctx = this,
            phoneId = bonds.selfId(),
            phoneName = device.displayName,
            bondKey = { id -> bonds.key(id) },
            onPaired = { key, flexosId ->
                // Se guarda AQUI, cuando Flex OS ya demostro quien es. commit():
                // nada de lo que pase despues puede deshacerlo.
                bonds.save(key, flexosId, null)
                refreshDevices(bondsChanged = true)
                onLinkEvent(LinkEvent.PairingAccepted)
            },
            onEvent = { ev -> onWifiEvent(ev) },
            onMessage = { type, payload -> onMessage(type, payload) { t, p -> server?.send(t, p) == true } },
            wifiAddress = { NetAddress.wifiIpv4(this) },
            wifiInterface = { NetAddress.wifiInterface(this) },
        )
        if (!s.start()) return false
        server = s
        publishTransports()
        return true
    }

    /** El servidor GATT del reloj. Devuelve el motivo si no se puede. */
    private fun startBle(): String? {
        if (gatt?.running == true) return null
        val g = GattServer(this, bleHost, onEvent = { ev -> onBleEvent(ev) }) { conn, _, type, payload ->
            onMessage(type, payload) { t, p -> gatt?.sendTo(conn, t, p) == true }
        }
        val why = g.start()
        gatt = if (why == null) g else null
        publishTransports(bleWhy = why)
        return why
    }

    private fun stopBle(why: String?) {
        gatt?.stop()
        gatt = null
        synchronized(phaseLock) { registry.setBonds(bonds.list()) }
        publishTransports(bleWhy = why)
        refreshDevices()
    }

    private fun publishTransports(bleWhy: String? = gatt?.lastError) {
        state.setTransports(FlexPhoneState.Transports(
            wifi = server?.isRunning() == true,
            ble = gatt?.running == true,
            bleWhy = if (gatt?.running == true) null else bleWhy,
        ))
    }

    /** Lo que el lado telefono de v2 necesita del almacen de vinculos. */
    private val bleHost = object : PhoneLinkPeer.Host {
        override val phoneId: String get() = bonds.selfId()
        override val phoneName: String get() = device.displayName
        override fun bondKey(flexosId: String) = bonds.key(flexosId)
        override fun savePairing(key: ByteArray, flexosId: String, hello: HelloInfo?) {
            bonds.save(key, flexosId, hello?.name, DeviceModel.fromHello(hello?.model ?: 0))
            refreshDevices(bondsChanged = true)
            onLinkEvent(LinkEvent.PairingAccepted)
        }
        override fun now() = System.currentTimeMillis()
    }

    /** Manda a TODAS las sesiones abiertas. true si alguna lo acepto. */
    private fun sendAll(type: Int, payload: ByteArray): Boolean {
        val a = server?.send(type, payload) == true
        val b = (gatt?.sendAll(type, payload) ?: 0) > 0
        if (a || b) state.countSent()
        return a || b
    }

    private fun sessionCount(): Int = (if (server?.hasSession() == true) 1 else 0) + (gatt?.sessionCount() ?: 0)

    // ---------------------------------------------------------
    //  Emparejamiento desde la interfaz
    // ---------------------------------------------------------
    /** El usuario tecleo el codigo que ensena Flex OS. Va al transporte que lo pidio. */
    fun submitPairingCode(code: String): WifiLinkServer.Result {
        val g = gatt
        if (g != null && g.isAwaitingCode()) {
            val r = g.submitPairingCode(code)
            if (r == PairSubmit.SENT) onLinkEvent(LinkEvent.ProofSent)
            return when (r) {
                PairSubmit.SENT -> WifiLinkServer.Result.SENT
                PairSubmit.BAD_FORMAT -> WifiLinkServer.Result.BAD_FORMAT
                PairSubmit.NO_SESSION -> WifiLinkServer.Result.NO_SESSION
                PairSubmit.EXPIRED -> WifiLinkServer.Result.EXPIRED
                PairSubmit.LINK_DOWN -> WifiLinkServer.Result.LINK_DOWN
            }
        }
        return server?.submitPairingCode(code) ?: WifiLinkServer.Result.NO_SESSION
    }

    fun pairingRemainingMs(): Long = maxOf(server?.pairingRemainingMs() ?: 0L, gatt?.pairingRemainingMs() ?: 0L)
    fun isAwaitingCode(): Boolean = server?.isAwaitingCode() == true || gatt?.isAwaitingCode() == true

    /** Revoca TODOS los vinculos: claves fuera y sesiones cerradas. */
    fun forgetBond() {
        for (b in bonds.list()) forgetDevice(b.flexosId)
        bonds.clear()
        refreshDevices(bondsChanged = true)
        onLinkEvent(LinkEvent.BondForgotten)
    }

    /** Olvida UN aparato. */
    fun forgetDevice(flexosId: String) {
        bonds.forget(flexosId)
        gatt?.dropSession(flexosId)
        server?.dropSession("vinculo olvidado")
        refreshDevices(bondsChanged = true)
        if (!bonds.isPaired()) onLinkEvent(LinkEvent.BondForgotten)
    }

    fun linkAddress(): String? = server?.localWifiAddress() ?: NetAddress.wifiIpv4(this)
    fun linkPort(): Int = server?.port ?: 0
    fun isPaired(): Boolean = bonds.isPaired()
    fun pairedPeer(): String? = bonds.peerId()
    fun bleRunning(): Boolean = gatt?.running == true

    // ---------------------------------------------------------
    //  Buscar el P4 por Wi-Fi
    // ---------------------------------------------------------
    fun searchWatches(): Int = server?.probeForWatches() ?: 0
    fun probeWatchAt(ip: String): Boolean = server?.probeHost(ip) ?: false

    // =========================================================
    //  Eventos -> maquina de fases -> pantalla
    // =========================================================
    private fun onLinkEvent(e: LinkEvent) {
        synchronized(phaseLock) {
            // Con otra sesion abierta, un cierre o un fallo de un canal no es
            // "enlace caido": la pantalla sigue en conectado.
            val ignore = when (e) {
                is LinkEvent.ChannelClosed, LinkEvent.LinkFailure -> sessionCount() > 0
                else -> false
            }
            if (!ignore) {
                val before = machine.phase
                val after = machine.on(e)
                if (before != after) LinkDiag.d(TAG, "FASE $before -> $after por ${e::class.simpleName}")
                state.setPhase(after, if (after == LinkPhase.ERROR) machine.fatalReason else null)
            }
            state.setPairing(isAwaitingCode(), pairingRemainingMs())
            val latest = bonds.list().firstOrNull()
            state.setBond(bonds.isPaired(), latest?.name?.takeIf { it.isNotBlank() })
        }
        updateNotification()
    }

    private fun refreshDevices(bondsChanged: Boolean = false) {
        val list = synchronized(phaseLock) {
            if (bondsChanged) registry.setBonds(bonds.list())
            registry.list()
        }
        state.setDevices(list)
    }

    private fun onWifiEvent(ev: WifiLinkServer.Event) {
        when (ev) {
            is WifiLinkServer.Event.Listening -> state.setAddress(NetAddress.wifiIpv4(this) ?: ev.address)
            is WifiLinkServer.Event.Error -> {
                state.setNotice(ev.message)
                onLinkEvent(if (ev.fatal) LinkEvent.Fatal(ev.message) else LinkEvent.LinkFailure)
            }
            WifiLinkServer.Event.ChannelOpened -> onLinkEvent(LinkEvent.ChannelOpened)
            is WifiLinkServer.Event.SessionOpen -> {
                onLinkEvent(LinkEvent.SessionOpened)
                state.setNotice(null)
                // Lo que Flex OS necesita para pintar la pantalla: capacidades
                // primero, estado despues y el relay si sigue arriba.
                server?.send(FlexLink.T_CAPS, currentCaps().encode())
                server?.send(FlexLink.T_PHONE_STATE, device.phoneState().encode())
                state.reannounceRelay()
            }
            is WifiLinkServer.Event.SessionClosed -> {
                if (ev.authenticated) state.countReconnect()
                onLinkEvent(LinkEvent.ChannelClosed(ev.authenticated))
            }
            is WifiLinkServer.Event.PairingRequested -> {
                if (ev.freshSession) { state.clearTypedCode(); state.setPairFailure(null) }
                onLinkEvent(LinkEvent.PairSalt)
            }
            WifiLinkServer.Event.ProofResent -> onLinkEvent(LinkEvent.ProofSent)
            is WifiLinkServer.Event.ProofNotSent -> { onLinkEvent(LinkEvent.ProofNotSent); state.setNotice(ev.why) }
            is WifiLinkServer.Event.WatchesFound -> {
                state.setWatches(ev.watches.map { FlexPhoneState.Watch(it.id, it.name, it.address, it.pairing) })
                synchronized(phaseLock) {
                    registry.setWifiSeen(
                        ev.watches.map { Triple(it.id, it.name, it.address) },
                        ev.watches.filter { it.pairing }.map { it.id }.toSet(),
                    )
                }
                refreshDevices()
                return
            }
            is WifiLinkServer.Event.PairingFailed -> onPairFailed(ev.why, ev.kind)
            is WifiLinkServer.Event.Presented -> {
                synchronized(phaseLock) {
                    registry.channelOpened("wifi#${ev.conn}", LinkTransport.WIFI, ev.address)
                    registry.channelHello("wifi#${ev.conn}", ev.hello)
                }
                bonds.updateInfo(ev.hello.flexosId, ev.hello.name, DeviceModel.fromHello(ev.hello.model), null)
                refreshDevices()
            }
            is WifiLinkServer.Event.ChannelSession -> {
                synchronized(phaseLock) { registry.channelSession("wifi#${ev.conn}", true) }
                refreshDevices()
            }
            is WifiLinkServer.Event.ChannelGone -> {
                synchronized(phaseLock) { registry.channelClosed("wifi#${ev.conn}") }
                refreshDevices()
            }
        }
    }

    private fun onPairFailed(why: String, kind: PairFailure) {
        when (kind) {
            PairFailure.CODE_REJECTED -> { state.clearTypedCode(); onLinkEvent(LinkEvent.CodeRejected) }
            PairFailure.CODE_EXPIRED -> { state.clearTypedCode(); onLinkEvent(LinkEvent.PairingEnded) }
            PairFailure.LINK -> onLinkEvent(LinkEvent.LinkFailure)
        }
        state.setPairFailure(kind)
        state.setNotice(why)
    }

    private fun onBleEvent(ev: GattServer.Event) {
        when (ev) {
            is GattServer.Event.Advertising -> publishTransports(bleWhy = ev.why.takeIf { !ev.on })
            is GattServer.Event.ChannelOpened -> {
                synchronized(phaseLock) { registry.channelOpened(ev.conn, LinkTransport.BLE, ev.address) }
                onLinkEvent(LinkEvent.ChannelOpened)
                refreshDevices()
            }
            is GattServer.Event.ChannelClosed -> {
                synchronized(phaseLock) { registry.channelClosed(ev.conn) }
                if (ev.authed) state.countReconnect()
                onLinkEvent(LinkEvent.ChannelClosed(ev.authed))
                refreshDevices()
            }
            is GattServer.Event.Peer -> onBlePeer(ev.conn, ev.address, ev.ev)
        }
    }

    private fun onBlePeer(conn: String, address: String, ev: PhoneLinkPeer.Event) {
        when (ev) {
            is PhoneLinkPeer.Event.Hello -> {
                synchronized(phaseLock) { registry.channelHello(conn, ev.info) }
                // El reloj declara nombre y modelo, y su direccion BLE sirve para
                // reconocerlo la proxima vez antes de que se presente.
                bonds.updateInfo(ev.info.flexosId, ev.info.name, DeviceModel.fromHello(ev.info.model), address)
                refreshDevices()
            }
            is PhoneLinkPeer.Event.PairingRequested -> {
                if (ev.freshSession) { state.clearTypedCode(); state.setPairFailure(null) }
                synchronized(phaseLock) { registry.channelPairing(conn, "", true) }
                onLinkEvent(LinkEvent.PairSalt)
                refreshDevices()
            }
            PhoneLinkPeer.Event.ProofResent -> onLinkEvent(LinkEvent.ProofSent)
            is PhoneLinkPeer.Event.ProofNotSent -> { onLinkEvent(LinkEvent.ProofNotSent); state.setNotice(ev.why) }
            is PhoneLinkPeer.Event.PairingFailed -> onPairFailed(ev.why, ev.kind)
            is PhoneLinkPeer.Event.SessionOpen -> {
                synchronized(phaseLock) { registry.channelSession(conn, true) }
                bonds.updateInfo(ev.flexosId, null, null, address)
                refreshDevices(bondsChanged = ev.newlyPaired)
                onLinkEvent(LinkEvent.SessionOpened)
                state.setNotice(null)
                gatt?.sendTo(conn, FlexLink.T_CAPS, currentCaps().encode())
                gatt?.sendTo(conn, FlexLink.T_PHONE_STATE, device.phoneState().encode())
                state.reannounceRelay()
                // Lo que ya esta sonando y las notificaciones activas: el reloj no
                // sabe lo que paso mientras estaba desconectado.
                state.media.value?.let { gatt?.sendTo(conn, FlexLink.T_MEDIA_STATE, it.encode()) }
                FlexNotificationListener.replayActive { p -> gatt?.sendTo(conn, FlexLink.T_NOTIF_ADD, p.encode()) == true }
            }
            is PhoneLinkPeer.Event.Error -> { state.setNotice(ev.message); onLinkEvent(LinkEvent.LinkFailure) }
            is PhoneLinkPeer.Event.CloseRequested -> Unit   // el cierre llega como ChannelClosed
        }
    }

    /**
     * Bucle de mantenimiento. NO es sondeo del enlace: los servidores avisan
     * por eventos. Refresca el estado del telefono (bateria y red cambian sin
     * que nadie avise), vuelve a mandar las capacidades si algo cambio y
     * busca el P4 por Wi-Fi mientras no haya sesion por ese camino.
     */
    private fun loop() = scope.launch {
        var lastCaps: CapsPayload? = null
        var lastState: PhoneState? = null
        var lastPush = 0L
        while (isActive) {
            delay(5_000)
            try {
                if (server == null && device.isOnWifi()) startWifi()
                if (server?.hasSession() != true) server?.probeForWatches()
                refreshDevices()
                if (sessionCount() == 0) continue
                val caps = currentCaps()
                if (caps != lastCaps) { lastCaps = caps; sendAll(FlexLink.T_CAPS, caps.encode()) }
                val st = device.phoneState()
                val now = System.currentTimeMillis()
                if (st != lastState || now - lastPush > STATE_HEARTBEAT_MS) {
                    lastState = st
                    lastPush = now
                    sendAll(FlexLink.T_PHONE_STATE, st.encode())
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                LinkDiag.w(TAG, "mantenimiento: ${e.javaClass.simpleName}")
            }
        }
    }

    // ---------------------------------------------------------
    //  La Wi-Fi y el Bluetooth cambian: se avisa, no se sondea
    // ---------------------------------------------------------
    private fun watchNetwork() {
        val cm = getSystemService(ConnectivityManager::class.java) ?: return
        val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { onWifiMaybeChanged("disponible") }
            override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) {
                onWifiMaybeChanged("propiedades")
            }
            override fun onLost(network: Network) { onWifiMaybeChanged("perdida") }
        }
        try {
            cm.registerNetworkCallback(req, cb)
            netCb = cb
        } catch (e: Exception) {
            LinkDiag.w(TAG, "sin avisos de red: ${e.javaClass.simpleName}")
        }
    }

    private fun unwatchNetwork() {
        val cb = netCb ?: return
        netCb = null
        runCatching { getSystemService(ConnectivityManager::class.java)?.unregisterNetworkCallback(cb) }
    }

    private fun onWifiMaybeChanged(why: String) {
        val now = NetAddress.wifiIpv4(this)
        val before = lastWifiIp
        if (now == before) return
        lastWifiIp = now
        LinkDiag.d(TAG, "WIFI $why: ip ${before ?: "ninguna"} -> ${now ?: "ninguna"}")
        state.setAddress(now)
        if (now != null && server == null) startWifi()
        server?.onNetworkChanged()
        BrowserRelayService.notifyAddressChanged()
        publishTransports()
        updateNotification()
    }

    private val btReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            when (i?.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1)) {
                BluetoothAdapter.STATE_ON -> scope.launch { startBle(); updateNotification() }
                BluetoothAdapter.STATE_TURNING_OFF, BluetoothAdapter.STATE_OFF ->
                    scope.launch { stopBle("el Bluetooth esta apagado"); updateNotification() }
            }
        }
    }
    @Volatile private var btWatched = false

    private fun watchBluetooth() {
        runCatching {
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(btReceiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED), Context.RECEIVER_NOT_EXPORTED)
            } else {
                @Suppress("UnspecifiedRegisterReceiverFlag")
                registerReceiver(btReceiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED))
            }
            btWatched = true
        }
    }

    private fun unwatchBluetooth() {
        if (btWatched) { btWatched = false; runCatching { unregisterReceiver(btReceiver) } }
    }

    /** La interfaz llama aqui tras conceder los permisos de Bluetooth. */
    fun retryBluetooth() { scope.launch { startBle(); updateNotification() } }

    private fun currentCaps(): CapsPayload = device.caps(
        notifAccess = FlexNotificationListener.hasAccess(this),
        relayRunning = BrowserRelayService.isRunning(),
        mediaActive = media?.hasActiveSession() == true,
        bleActive = gatt?.running == true,
    )

    // ---------------------------------------------------------
    //  Mensajes de Flex OS. `reply` es el canal por el que llego.
    // ---------------------------------------------------------
    private fun onMessage(type: Int, payload: ByteArray, reply: (Int, ByteArray) -> Boolean) {
        state.countReceived()
        when (type) {
            FlexLink.T_REPLY_REQ -> {
                val req = ReplyRequest.decode(payload)
                if (req == null) { state.countBadFrame(); return }
                // El resultado que se devuelve es el REAL.
                val res = ReplyManager.reply(this, req.notifId, req.action, req.text)
                state.countReply(res.ok)
                reply(FlexLink.T_REPLY_RESULT, res.encode())
            }
            FlexLink.T_ACTION_REQ -> {
                val r = PayloadReader(payload)
                val id = r.u32(); val idx = r.u8()
                if (!r.ok) { state.countBadFrame(); return }
                reply(FlexLink.T_ACTION_RESULT, ReplyManager.action(this, id, idx).encode())
            }
            FlexLink.T_NOTIF_REMOVE -> {
                val r = PayloadReader(payload)
                val id = r.u32()
                if (r.ok) FlexNotificationListener.dismiss(id)
            }
            FlexLink.T_MEDIA_CMD -> {
                val r = PayloadReader(payload)
                val cmd = r.u8()
                if (r.ok) media?.command(cmd)
            }
            FlexLink.T_FIND_START -> FindMyPhone.start(this)
            FlexLink.T_FIND_STOP -> FindMyPhone.stop()
            FlexLink.T_PHONE_STATE -> reply(FlexLink.T_PHONE_STATE, device.phoneState().encode())
            FlexLink.T_RELAY_START -> {
                runCatching { BrowserRelayService.start(this) }
                    .onFailure { LinkDiag.w(TAG, "RELAY_START fallo: ${it.javaClass.simpleName}") }
                sendAll(FlexLink.T_CAPS, currentCaps().encode())
                // Si ya estaba arriba, se repite su direccion a quien lo pidio.
                state.reannounceRelay()
            }
            FlexLink.T_RELAY_STOP -> {
                runCatching { BrowserRelayService.stop(this) }
                    .onFailure { LinkDiag.w(TAG, "RELAY_STOP fallo: ${it.javaClass.simpleName}") }
                sendAll(FlexLink.T_CAPS, currentCaps().encode())
            }
            FlexLink.T_TIME_SYNC -> Unit
        }
    }

    // ---------------------------------------------------------
    //  Notificacion persistente
    // ---------------------------------------------------------
    private fun createChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val ch = NotificationChannel(
            CHANNEL, getString(R.string.channel_link), NotificationManager.IMPORTANCE_LOW,
        ).apply { description = getString(R.string.channel_link_desc); setShowBadge(false) }
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        val online = state.devices.value.filter { it.online }
        val text = when {
            online.size == 1 -> getString(R.string.link_connected) + ": " + online[0].name
            online.size > 1 -> getString(R.string.link_connected) + ": " + online.size + " Flex OS"
            else -> when (state.link.value) {
                LinkState.READY -> getString(R.string.link_connected)
                LinkState.PAIRING -> getString(R.string.link_pairing)
                LinkState.CONNECTING -> getString(R.string.link_connecting)
                LinkState.RECONNECTING -> getString(R.string.link_reconnecting)
                LinkState.ADVERTISING -> getString(R.string.link_searching)
                LinkState.UNAVAILABLE -> state.error.value ?: getString(R.string.link_unavailable)
                LinkState.ERROR -> state.error.value ?: getString(R.string.link_error)
                LinkState.OFF -> getString(R.string.link_off)
            }
        }
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, FlexLinkService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_link)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .addAction(R.drawable.ic_stop, getString(R.string.stop), stop)
            .build()
    }

    private fun startForegroundHonestly() {
        val n = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIF_ID, n)
        }
    }

    private fun updateNotification() {
        runCatching {
            getSystemService(NotificationManager::class.java)?.notify(NOTIF_ID, buildNotification())
        }
    }

    private fun shutdown() {
        LinkDiag.d(TAG, "SERVICE_STOPPED (shutdown pedido)")
        scope.coroutineContext.cancelChildren()
        unwatchNetwork()
        unwatchBluetooth()
        state.sender = null
        onLinkEvent(LinkEvent.Stopped)
        media?.stop(); media = null
        gatt?.stop(); gatt = null
        server?.stop(); server = null
        started = false
        FindMyPhone.stop()
        state.clearTypedCode()
        state.setPairing(false, 0L)
        synchronized(phaseLock) { registry.setBonds(bonds.list()) }
        publishTransports(bleWhy = null)
        refreshDevices()
        current = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        LinkDiag.d(TAG, "SERVICE_DESTROYED")
        scope.cancel()
        unwatchNetwork()
        unwatchBluetooth()
        state.sender = null
        media?.stop()
        gatt?.stop()
        server?.stop()
        FindMyPhone.stop()
        if (current === this) current = null
        // EL ESTADO TIENE QUE DEJAR DE MENTIR: si Android mata el servicio, la
        // pantalla no puede seguir diciendo que se escucha.
        if (state.link.value != LinkState.OFF) {
            synchronized(phaseLock) { machine.on(LinkEvent.Stopped) }
            state.setPhase(LinkPhase.STOPPED, getString(R.string.link_stopped_by_system))
            state.setPairing(false, 0L)
        }
        super.onDestroy()
    }
}
