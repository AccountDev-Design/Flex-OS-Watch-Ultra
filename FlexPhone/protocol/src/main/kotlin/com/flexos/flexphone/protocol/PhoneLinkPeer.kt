package com.flexos.flexphone.protocol

/**
 * EL LADO TELEFONO DE FLEX LINK v2, SIN TRANSPORTE.
 *
 * Es la logica por conexion de [com.flexos.flexphone.link.WifiLinkServer]
 * (saludo, emparejamiento con codigo, reto-respuesta mutuo, latido, ACK con
 * descarte de reenvios, plazos), sacada del socket para que la use tambien el
 * transporte BLE. Kotlin puro: se prueba en el PC y, ademas, contra el MOTOR
 * EN C DEL RELOJ (tests/host/flexlink_watch_peer.c), byte a byte.
 *
 * QUIEN HABLA PRIMERO
 * -------------------
 * Flex OS (P4 o reloj) es el que se presenta: HELLO -> WELCOME. Despues:
 *   · vinculo conocido   -> AUTH_CHALLENGE -> AUTH_RESPONSE -> AUTH_OK;
 *   · emparejamiento     -> PAIR_CODE (sal) ... el usuario teclea el codigo ...
 *                           PAIR_CONFIRM -> AUTH_OK.
 * Solo tras AUTH_OK verificado hay sesion: antes no se entrega ni un mensaje.
 *
 * LO QUE ANADE RESPECTO AL SERVIDOR Wi-Fi
 * ---------------------------------------
 *   · REENSAMBLADO: por GATT una trama es una escritura, y un mensaje largo
 *     (una respuesta de 400 B) llega en varios fragmentos. Por TCP el P4 no
 *     los mandaba nunca; el reloj si puede.
 *   · ANTI-REPETICION por contador, reiniciada en cada HELLO (el reloj
 *     reinicia su contador al reabrir la sesion).
 *   · El hueco de "una sola sesion" y el emparejamiento que sobrevive a los
 *     cortes los lleva quien crea los pares (el transporte), con
 *     [PairingSlot]: aqui solo hay UNA conexion.
 *
 * HILOS: todas las llamadas de una misma instancia tienen que venir
 * serializadas (el transporte las mete por una sola cola). La interfaz solo
 * toca [submitCode], que tambien se serializa alli.
 */
class PhoneLinkPeer(
    private val host: Host,
    private val pairing: PairingSlot,
    /** Entrega UNA trama ya enmarcada al canal. false = no cupo o el canal murio. */
    private val out: (ByteArray) -> Boolean,
    private val listener: Listener,
    /** Nombre corto del canal para el registro ("ble#3"). Nunca una direccion MAC. */
    val tag: String = "peer",
) {
    /** Lo que el motor necesita de fuera. */
    interface Host {
        val phoneId: String
        val phoneName: String
        /** Clave del vinculo con ESE Flex OS, o null si no esta vinculado. */
        fun bondKey(flexosId: String): ByteArray?
        /** Guarda el vinculo recien demostrado. Si lanza, no se abre sesion. */
        fun savePairing(key: ByteArray, flexosId: String, hello: HelloInfo?)
        fun now(): Long
    }

    interface Listener {
        fun onEvent(peer: PhoneLinkPeer, ev: Event)
        fun onMessage(peer: PhoneLinkPeer, type: Int, payload: ByteArray)
    }

    sealed class Event {
        /** Flex OS se presento. Aun no hay sesion. */
        data class Hello(val info: HelloInfo) : Event()
        /** Llego la sal: hay que ensenar el campo del codigo. */
        data class PairingRequested(val freshSession: Boolean) : Event()
        object ProofResent : Event()
        data class ProofNotSent(val why: String) : Event()
        data class PairingFailed(val why: String, val kind: PairFailure) : Event()
        /** Sesion autenticada en los dos sentidos. */
        data class SessionOpen(val flexosId: String, val newlyPaired: Boolean) : Event()
        /** Un fallo del enlace (no de la app). */
        data class Error(val message: String) : Event()
        /** El canal debe cerrarse (BYE, prueba falsa, plazos...). */
        data class CloseRequested(val reason: String) : Event()
    }

    companion object {
        /** Un extremo que no dice nada ni empareja se va en este tiempo. */
        const val AUTH_TIMEOUT_MS = 15_000L
        /** Sin recibir nada en este tiempo la sesion se da por muerta (el reloj late cada 8 s). */
        const val IDLE_TIMEOUT_MS = 40_000L
        /** Desde que se manda el codigo hasta que Flex OS lo acepta. */
        const val PAIR_CONFIRM_TIMEOUT_MS = 12_000L
        /** Parcial de un mensaje que no se completa. */
        const val REASM_TIMEOUT_MS = 5_000L
        private const val SEEN_N = 32

        /** Ordenes que Flex OS manda "con acuse". */
        val ACKED_TYPES = intArrayOf(
            FlexLink.T_RELAY_START, FlexLink.T_RELAY_STOP,
            FlexLink.T_FIND_START, FlexLink.T_FIND_STOP,
            FlexLink.T_REPLY_REQ, FlexLink.T_ACTION_REQ,
        )
        val HANDSHAKE_TYPES = intArrayOf(
            FlexLink.T_HELLO, FlexLink.T_WELCOME, FlexLink.T_BYE, FlexLink.T_ERR,
            FlexLink.T_PAIR_REQ, FlexLink.T_PAIR_CODE, FlexLink.T_PAIR_CONFIRM,
            FlexLink.T_AUTH_CHALLENGE, FlexLink.T_AUTH_RESPONSE, FlexLink.T_AUTH_OK,
            FlexLink.T_PING, FlexLink.T_PONG,
        )
    }

    // ---- Estado de esta conexion ----
    private val openedAt = host.now()
    private var lastRx = openedAt
    @Volatile var authed = false
        private set
    @Volatile var session = 0
        private set
    @Volatile var hello: HelloInfo? = null
        private set
    /** Id del Flex OS al otro lado ("" hasta el HELLO o la sal). */
    @Volatile var flexosId: String = ""
        private set
    private var sessionKey: ByteArray? = null
    private var nonce: ByteArray? = null
    private var closed = false
    private var txCounter = 0L
    private var txPacket = 1
    var pings = 0
        private set
    var badFrames = 0
        private set
    var dropped = 0
        private set

    private val reasm = Reassembler()
    private val anti = AntiReplay()
    private val seen = IntArray(SEEN_N)
    private var seenAt = 0
    private var seenFill = 0

    // =========================================================
    //  Entrada
    // =========================================================
    /** Una trama entera tal como llego del canal. */
    fun onFrame(frame: ByteArray) {
        if (closed) return
        val r = FlexLink.readFrame(frame)
        if (r is FlexLink.ReadResult.BadVersion) {
            badFrames++
            sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_VERSION.toByte()), 0)
            emit(Event.Error("Flex OS habla otra version del protocolo"))
            requestClose("version incompatible")
            return
        }
        if (r !is FlexLink.ReadResult.Ok) { badFrames++; return }
        val h = r.header
        lastRx = host.now()
        // Un HELLO es una presentacion NUEVA: el otro extremo reinicio sus
        // contadores. Se olvida la anti-repeticion y cualquier parcial.
        if (h.type == FlexLink.T_HELLO) { anti.reset(); reasm.reset() }
        if (!anti.check(h.counter)) { dropped++; return }
        // Fuera de sesion SOLO pasa el apreton de manos.
        if (!authed && h.type !in HANDSHAKE_TYPES) { dropped++; return }
        // Con sesion, la sesion tiene que ser la nuestra (salvo BYE).
        if (authed && session != 0 && h.session != session && h.type != FlexLink.T_BYE &&
            h.type != FlexLink.T_PING && h.type != FlexLink.T_HELLO) { dropped++; return }

        val body: ByteArray = if (h.fragCount > 1) {
            when (reasm.feed(h, r.payload, host.now())) {
                Reassembler.Result.DONE -> reasm.message()
                Reassembler.Result.NEED_MORE -> return
                Reassembler.Result.DROP -> { dropped++; return }
            }
        } else r.payload
        handle(h, body)
    }

    private fun handle(h: FlexLink.Header, body: ByteArray) {
        when (h.type) {
            FlexLink.T_HELLO -> onHello(body)
            FlexLink.T_PAIR_CODE -> onPairCode(body)
            FlexLink.T_AUTH_CHALLENGE -> onAuthChallenge(body)
            FlexLink.T_AUTH_OK -> onAuthOk(body, h.session)
            // EL LATIDO: se contesta siempre, con o sin sesion.
            FlexLink.T_PING -> { pings++; sendRaw(FlexLink.T_PONG, ByteArray(0), h.session) }
            FlexLink.T_PONG -> Unit
            FlexLink.T_ERR -> onPeerError(body)
            FlexLink.T_BYE -> requestClose("BYE de Flex OS")
            else -> if (authed) {
                // Se ACUSA siempre (tambien un reenvio) y se ENTREGA una vez.
                if (h.type in ACKED_TYPES) {
                    sendRaw(FlexLink.T_ACK, PayloadWriter().u16(h.packet).build(), h.session)
                }
                if (firstSight(h.type, h.packet)) {
                    try { listener.onMessage(this, h.type, body) } catch (e: Exception) {
                        LinkDiag.w("FlexPhone/Peer", "$tag: la orden 0x%02x fallo: ${e.javaClass.simpleName}".format(h.type))
                    }
                }
            }
        }
    }

    private fun firstSight(type: Int, packet: Int): Boolean {
        val key = ((type and 0xFF) shl 24) or (packet and 0xFFFF)
        for (i in 0 until seenFill) if (seen[i] == key) return false
        seen[seenAt] = key
        seenAt = (seenAt + 1) % SEEN_N
        if (seenFill < SEEN_N) seenFill++
        return true
    }

    private fun onHello(body: ByteArray) {
        val info = HelloInfo.decode(body)
        if (info == null) { badFrames++; return }
        if (info.version < FlexLink.VERSION_MIN) {
            sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_VERSION.toByte()), 0)
            emit(Event.Error("Flex OS habla una version anterior del protocolo"))
            return
        }
        hello = info
        flexosId = info.flexosId
        // Una presentacion nueva en el mismo canal: lo de antes no vale.
        if (authed) { authed = false; session = 0 }
        val w = PayloadWriter()
        w.u8(FlexLink.VERSION)
        w.str(host.phoneId, FlexAuth.ID_MAX - 1)
        w.str(host.phoneName, 31)
        sendRaw(FlexLink.T_WELCOME, w.build(), 0)
        emit(Event.Hello(info))
    }

    private fun onPairCode(body: ByteArray) {
        val rd = PayloadReader(body)
        val salt = rd.bytes(FlexAuth.SALT_SIZE)
        val n = rd.bytes(FlexAuth.NONCE_SIZE)
        val hostId = rd.str()
        if (!rd.ok || hostId.isEmpty()) { badFrames++; return }
        val now = host.now()
        val cur = pairing.session
        val same = cur != null && cur.hasSalt(salt) && !cur.isExpired(now)
        if (!same) {
            pairing.session = PairingSession(salt, n, hostId, host.phoneId, now)
            pairing.sentAt = 0L
        }
        nonce = n
        flexosId = hostId
        pairing.peer = this
        val sess = pairing.session ?: return
        if (same) {
            val again = sess.proofForTypedCode(now)
            if (again != null) {
                if (sendRaw(FlexLink.T_PAIR_CONFIRM, again.proof, 0)) {
                    sessionKey = again.key
                    pairing.sentAt = now
                    emit(Event.ProofResent)
                } else {
                    sess.onSendFailed()
                    emit(Event.ProofNotSent("se corto la conexion al enviar el codigo"))
                }
                return
            }
        }
        emit(Event.PairingRequested(freshSession = !same))
    }

    /** El usuario tecleo el codigo. Solo vale en el par que trajo la sal. */
    fun submitCode(code: String): PairSubmit {
        val sess = pairing.session ?: return PairSubmit.NO_SESSION
        if (closed) return PairSubmit.LINK_DOWN
        val now = host.now()
        return when (val r = sess.submit(code, now)) {
            is PairingSession.Submit.Ready -> {
                if (sendRaw(FlexLink.T_PAIR_CONFIRM, r.proof, 0)) {
                    sessionKey = r.key
                    pairing.sentAt = now
                    PairSubmit.SENT
                } else {
                    sess.onSendFailed()
                    PairSubmit.LINK_DOWN
                }
            }
            PairingSession.Submit.BadFormat -> PairSubmit.BAD_FORMAT
            PairingSession.Submit.Expired -> PairSubmit.EXPIRED
            PairingSession.Submit.AlreadyInFlight -> PairSubmit.SENT
            PairingSession.Submit.AlreadyDone -> PairSubmit.SENT
        }
    }

    private fun onAuthChallenge(body: ByteArray) {
        val rd = PayloadReader(body)
        val n = rd.bytes(FlexAuth.NONCE_SIZE)
        val sess = rd.u16()
        if (!rd.ok) { badFrames++; return }
        val key = if (flexosId.isNotEmpty()) host.bondKey(flexosId) else null
        if (key == null) {
            sendRaw(FlexLink.T_ERR, byteArrayOf(FlexLink.E_NOTPAIRED.toByte()), sess)
            emit(Event.PairingFailed("este telefono ya no tiene el vinculo guardado", PairFailure.LINK))
            return
        }
        nonce = n
        session = sess
        sessionKey = key
        sendRaw(FlexLink.T_AUTH_RESPONSE, FlexAuth.proof(key, FlexAuth.ROLE_PHONE, n, sess), sess)
    }

    private fun onAuthOk(body: ByteArray, sess: Int) {
        val key = sessionKey ?: (if (flexosId.isNotEmpty()) host.bondKey(flexosId) else null) ?: return
        val n = nonce ?: return
        val rd = PayloadReader(body)
        val got = rd.bytes(FlexAuth.PROOF_SIZE)
        if (!rd.ok) { badFrames++; return }
        if (!FlexAuth.verify(key, FlexAuth.ROLE_HOST, n, sess, got)) {
            emit(Event.PairingFailed("quien contesta no es el Flex OS emparejado", PairFailure.LINK))
            requestClose("la prueba de Flex OS no cuadra")
            return
        }
        val stored = host.bondKey(flexosId)
        val newly = stored == null || !FlexAuth.equalsConstantTime(stored, key)
        if (newly) {
            try { host.savePairing(key, flexosId, hello) } catch (e: Exception) {
                emit(Event.PairingFailed("no se pudo guardar el vinculo en este telefono", PairFailure.LINK))
                requestClose("no se pudo guardar el vinculo")
                return
            }
        }
        session = sess
        authed = true
        if (pairing.peer === this || newly) {
            pairing.session?.onAccepted()
            pairing.session = null
            pairing.sentAt = 0L
            pairing.peer = null
        }
        emit(Event.SessionOpen(flexosId, newly))
    }

    private fun onPeerError(body: ByteArray) {
        val code = if (body.isNotEmpty()) body[0].toInt() and 0xFF else FlexLink.E_INTERNAL
        pairing.sentAt = 0L
        val why = when (code) {
            FlexLink.E_AUTH -> "el codigo no coincide con el que ensena Flex OS"
            FlexLink.E_TIMEOUT -> "el emparejamiento caduco en Flex OS; vuelve a pulsar \"Emparejar telefono\""
            FlexLink.E_NOTPAIRED -> "Flex OS ya no tiene guardado el vinculo con este telefono"
            FlexLink.E_VERSION -> "Flex OS habla otra version del protocolo"
            FlexLink.E_BUSY -> "Flex OS ya tiene otro telefono conectado"
            else -> "Flex OS rechazo la conexion (codigo $code)"
        }
        val kind = when (code) {
            FlexLink.E_AUTH -> PairFailure.CODE_REJECTED
            FlexLink.E_TIMEOUT -> PairFailure.CODE_EXPIRED
            else -> PairFailure.LINK
        }
        when (kind) {
            PairFailure.CODE_REJECTED -> pairing.session?.onRejected()
            PairFailure.CODE_EXPIRED -> pairing.session = null
            PairFailure.LINK -> Unit
        }
        if (authed) emit(Event.Error(why)) else emit(Event.PairingFailed(why, kind))
    }

    // =========================================================
    //  Plazos
    // =========================================================
    /**
     * Mira los plazos. Llamar a menudo (cada segundo basta). Devuelve el
     * motivo si el canal debe cerrarse; null si sigue.
     */
    fun tick(): String? {
        if (closed) return null
        val now = host.now()
        if (reasm.expire(now, REASM_TIMEOUT_MS)) dropped++
        val pair = pairing.session
        val pairingHere = pair != null && !pair.isExpired(now) && (pairing.peer === this || pairing.peer == null)
        if (!authed) {
            val deadline = when {
                pairingHere -> pair!!.expiresAt
                flexosId.isNotEmpty() -> openedAt + PairingSession.WINDOW_MS
                else -> openedAt + AUTH_TIMEOUT_MS
            }
            if (now > deadline) {
                val expired = pair != null && pair.isExpired(now)
                emit(Event.PairingFailed(
                    if (expired) "el codigo de Flex OS caduco; vuelve a pulsar \"Emparejar telefono\""
                    else "la otra parte no se autentico",
                    if (expired) PairFailure.CODE_EXPIRED else PairFailure.LINK,
                ))
                return requestClose(if (expired) "el codigo caduco" else "no se autentico a tiempo")
            }
            val sentAt = pairing.sentAt
            if (pairing.peer === this && sentAt != 0L && now - sentAt > PAIR_CONFIRM_TIMEOUT_MS) {
                pairing.sentAt = 0L
                pair?.onSendFailed()
                emit(Event.ProofNotSent("Flex OS no contesto al codigo; vuelve a intentarlo"))
            }
        }
        if (!pairingHere && now - lastRx > IDLE_TIMEOUT_MS) {
            emit(Event.Error("sin respuesta de Flex OS"))
            return requestClose("sin respuesta en ${IDLE_TIMEOUT_MS / 1000} s")
        }
        return null
    }

    // =========================================================
    //  Salida
    // =========================================================
    /** Mensaje de aplicacion. Exige sesion autenticada. Todo o nada. */
    fun send(type: Int, payload: ByteArray): Boolean {
        if (!authed || closed) return false
        return sendRaw(type, payload, session)
    }

    private fun sendRaw(type: Int, payload: ByteArray, sess: Int): Boolean {
        if (closed) return false
        if (payload.size > FlexLink.MAX_MESSAGE) return false
        val packet = txPacket.also { txPacket = if (it >= 0xFFFF) 1 else it + 1 }
        val frames = try {
            FlexLink.fragment(type, sess, packet, payload, counterStart = txCounter + 1)
        } catch (e: IllegalArgumentException) { return false }
        txCounter += frames.size
        for (f in frames) if (!out(f)) return false
        return true
    }

    /** Despedida limpia (si hay sesion) y fin de este par. */
    fun close(reason: String, sayBye: Boolean = true) {
        if (closed) return
        if (sayBye && authed) sendRaw(FlexLink.T_BYE, ByteArray(0), session)
        closed = true
        authed = false
        if (pairing.peer === this) pairing.peer = null
        LinkDiag.d("FlexPhone/Peer", "$tag: cerrado ($reason)")
    }

    val isClosed: Boolean get() = closed

    private fun requestClose(reason: String): String {
        emit(Event.CloseRequested(reason))
        close(reason, sayBye = false)
        return reason
    }

    private fun emit(ev: Event) {
        try { listener.onEvent(this, ev) } catch (e: Exception) {
            LinkDiag.w("FlexPhone/Peer", "$tag: el oyente de ${ev::class.simpleName} fallo: ${e.javaClass.simpleName}")
        }
    }
}

/** Lo que puede pasar al teclear el codigo (cada rama tiene su texto en la pantalla). */
enum class PairSubmit { SENT, BAD_FORMAT, NO_SESSION, EXPIRED, LINK_DOWN }

/**
 * El emparejamiento en curso de UN transporte. Sobrevive a los cortes del
 * canal: Flex OS reconecta y reenvia LA MISMA sal, y el codigo ya tecleado
 * se reenvia solo por el canal nuevo.
 */
class PairingSlot {
    @Volatile var session: PairingSession? = null
    /** Cuando salio el PAIR_CONFIRM (0 = ninguno en vuelo). */
    @Volatile var sentAt: Long = 0L
    /** El par por el que llego la sal: el codigo sale por ese. */
    @Volatile var peer: PhoneLinkPeer? = null

    fun awaitingCode(now: Long): Boolean = session?.acceptsCode(now) == true
    fun remainingMs(now: Long): Long = session?.remainingMs(now) ?: 0L
    fun clear() { session = null; sentAt = 0L; peer = null }
}

/**
 * HELLO de Flex OS: `u8 version · str id` y, desde Flex OS Watch Ultra,
 * `str nombre · u8 modelo` detras. Los campos nuevos van AL FINAL: un lector
 * antiguo (el del servidor Wi-Fi, el del P4) los ignora, y un HELLO sin ellos
 * (el P4 de siempre) se lee igual, con el modelo sin declarar.
 */
data class HelloInfo(
    val version: Int,
    val flexosId: String,
    val name: String = "",
    val model: Int = MODEL_UNKNOWN,
) {
    companion object {
        const val MODEL_UNKNOWN = 0
        /** Flex OS Ultra: ESP32-P4 (+ C6 para la radio Wi-Fi). */
        const val MODEL_ULTRA_P4 = 1
        /** Flex OS Watch Ultra: ESP32-S3, Waveshare AMOLED 2.06". */
        const val MODEL_WATCH_ULTRA = 2

        fun decode(body: ByteArray): HelloInfo? {
            val rd = PayloadReader(body)
            val ver = rd.u8()
            val id = rd.str()
            if (!rd.ok || id.isEmpty()) return null
            // Opcionales: si no estan, no es un error.
            if (rd.remaining() == 0) return HelloInfo(ver, id)
            val name = rd.str()
            val model = rd.u8()
            return if (rd.ok) HelloInfo(ver, id, name, model) else HelloInfo(ver, id)
        }
    }

    fun encode(withExtras: Boolean = true): ByteArray {
        val w = PayloadWriter().u8(version).str(flexosId, FlexAuth.ID_MAX - 1)
        if (withExtras) w.str(name, 31).u8(model)
        return w.build()
    }
}
