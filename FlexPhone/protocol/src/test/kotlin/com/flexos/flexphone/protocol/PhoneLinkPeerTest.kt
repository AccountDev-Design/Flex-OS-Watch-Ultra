package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * El lado telefono de Flex Link v2 sin transporte (PhoneLinkPeer), contra un
 * Flex OS escrito aqui con las MISMAS primitivas que el firmware (FlexAuth,
 * FlexLink). La prueba de extremo a extremo contra el motor en C del reloj
 * esta en WatchPeerInteropTest.
 */
class PhoneLinkPeerTest {

    private val hostId = "flexos-0123456789abcdef"
    private val phoneId = "phone-fedcba9876543210"

    private class Clock { var t = 10_000L }

    private inner class Rig(bonded: ByteArray? = null) {
        val clock = Clock()
        val bonds = HashMap<String, ByteArray>().apply { if (bonded != null) put(hostId, bonded) }
        val saved = ArrayList<Pair<String, HelloInfo?>>()
        val events = ArrayList<PhoneLinkPeer.Event>()
        val msgs = ArrayList<Pair<Int, ByteArray>>()
        val wire = ArrayList<ByteArray>()     // telefono -> Flex OS
        val slot = PairingSlot()
        var outOk = true
        var counter = 0L
        var packet = 1
        val host = object : PhoneLinkPeer.Host {
            override val phoneId = this@PhoneLinkPeerTest.phoneId
            override val phoneName = "Pixel 8"
            override fun bondKey(flexosId: String) = bonds[flexosId]
            override fun savePairing(key: ByteArray, flexosId: String, hello: HelloInfo?) {
                bonds[flexosId] = key; saved += flexosId to hello
            }
            override fun now() = clock.t
        }
        val listener = object : PhoneLinkPeer.Listener {
            override fun onEvent(peer: PhoneLinkPeer, ev: PhoneLinkPeer.Event) { events += ev }
            override fun onMessage(peer: PhoneLinkPeer, type: Int, payload: ByteArray) { msgs += type to payload }
        }
        val peer = PhoneLinkPeer(host, slot, { f -> if (outOk) { wire += f; true } else false }, listener, "prueba")

        /** Flex OS manda un mensaje (troceado si hace falta). */
        fun fromHost(type: Int, payload: ByteArray = ByteArray(0), session: Int = 0) {
            val p = packet++
            for (f in FlexLink.fragment(type, session, p, payload, counterStart = counter + 1)) {
                counter++
                peer.onFrame(f)
            }
        }
        fun taken(): List<FlexLink.ReadResult.Ok> = wire.map { FlexLink.readFrame(it) as FlexLink.ReadResult.Ok }.also { wire.clear() }
        fun hello(withModel: Boolean = true) = fromHost(
            FlexLink.T_HELLO,
            HelloInfo(2, hostId, "Flex OS Watch Ultra", HelloInfo.MODEL_WATCH_ULTRA).encode(withModel),
        )
    }

    private fun key(b: Int) = ByteArray(FlexAuth.KEY_SIZE) { (b + it).toByte() }
    private fun nonce(b: Int) = ByteArray(FlexAuth.NONCE_SIZE) { (b + it).toByte() }

    @Test
    fun `HELLO con modelo y nombre, y sin ellos como el P4`() {
        val full = HelloInfo(2, hostId, "Flex OS Watch Ultra", HelloInfo.MODEL_WATCH_ULTRA)
        assertEquals(full, HelloInfo.decode(full.encode()))
        // El HELLO del P4: solo version e id. Se lee igual, sin modelo.
        val p4 = PayloadWriter().u8(2).str(hostId, 31).build()
        assertEquals(HelloInfo(2, hostId), HelloInfo.decode(p4))
        // El lector antiguo (WifiLinkServer.onHello) solo lee u8 + str: los campos
        // nuevos van detras y no le estorban.
        val rd = PayloadReader(full.encode())
        assertEquals(2, rd.u8()); assertEquals(hostId, rd.str()); assertTrue(rd.ok)
        assertNull(HelloInfo.decode(PayloadWriter().u8(2).str("", 31).build()), "id vacio")
        assertNull(HelloInfo.decode(byteArrayOf(2, 40)), "cadena cortada")
        // Un nombre mal formado detras del id no tira el saludo.
        val trunc = PayloadWriter().u8(2).str(hostId, 31).u8(9).build()
        assertEquals(HelloInfo(2, hostId), HelloInfo.decode(trunc))
    }

    @Test
    fun `saludo y sesion con un vinculo guardado`() {
        val k = key(1)
        val r = Rig(bonded = k)
        r.hello()
        val welcome = r.taken().single()
        assertEquals(FlexLink.T_WELCOME, welcome.header.type)
        val rd = PayloadReader(welcome.payload)
        assertEquals(FlexLink.VERSION, rd.u8()); assertEquals(phoneId, rd.str()); assertEquals("Pixel 8", rd.str())
        assertTrue(r.events.first() is PhoneLinkPeer.Event.Hello)
        assertEquals(HelloInfo.MODEL_WATCH_ULTRA, r.peer.hello?.model)

        val n = nonce(0x40); val sess = 0x1235
        r.fromHost(FlexLink.T_AUTH_CHALLENGE, PayloadWriter().bytes(n).u16(sess).build())
        val resp = r.taken().single()
        assertEquals(FlexLink.T_AUTH_RESPONSE, resp.header.type)
        assertEquals(sess, resp.header.session)
        assertContentEquals(FlexAuth.proof(k, FlexAuth.ROLE_PHONE, n, sess), resp.payload)
        assertFalse(r.peer.authed)
        r.fromHost(FlexLink.T_AUTH_OK, FlexAuth.proof(k, FlexAuth.ROLE_HOST, n, sess), sess)
        assertTrue(r.peer.authed)
        val open = r.events.filterIsInstance<PhoneLinkPeer.Event.SessionOpen>().single()
        assertEquals(hostId, open.flexosId); assertFalse(open.newlyPaired)
        assertTrue(r.saved.isEmpty(), "un vinculo conocido no se vuelve a guardar")

        // Mensajes de la app con la sesion buena.
        assertTrue(r.peer.send(FlexLink.T_PHONE_STATE, byteArrayOf(1, 2, 3)))
        val st = r.taken().single()
        assertEquals(sess, st.header.session)
        assertContentEquals(byteArrayOf(1, 2, 3), st.payload)
    }

    @Test
    fun `sin sesion no se entrega nada y una prueba falsa cierra`() {
        val r = Rig(bonded = key(1))
        r.hello(); r.taken()
        r.fromHost(FlexLink.T_REPLY_REQ, byteArrayOf(1, 2, 3, 4, 5))
        assertTrue(r.msgs.isEmpty(), "entrego una orden sin sesion")
        assertFalse(r.peer.send(FlexLink.T_NOTIF_ADD, ByteArray(4)), "mando sin sesion")

        val n = nonce(9); val sess = 7
        r.fromHost(FlexLink.T_AUTH_CHALLENGE, PayloadWriter().bytes(n).u16(sess).build())
        r.taken()
        // Quien contesta no tiene la clave: prueba con otra.
        r.fromHost(FlexLink.T_AUTH_OK, FlexAuth.proof(key(99), FlexAuth.ROLE_HOST, n, sess), sess)
        assertFalse(r.peer.authed)
        assertTrue(r.peer.isClosed)
        assertTrue(r.events.any { it is PhoneLinkPeer.Event.CloseRequested })
    }

    @Test
    fun `reto sin vinculo contesta NOTPAIRED`() {
        val r = Rig()
        r.hello(); r.taken()
        r.fromHost(FlexLink.T_AUTH_CHALLENGE, PayloadWriter().bytes(nonce(1)).u16(3).build())
        val err = r.taken().single()
        assertEquals(FlexLink.T_ERR, err.header.type)
        assertEquals(FlexLink.E_NOTPAIRED, err.payload[0].toInt())
    }

    private fun pairCode(salt: ByteArray, n: ByteArray) =
        PayloadWriter().bytes(salt).bytes(n).str(hostId, 31).build()

    @Test
    fun `emparejar con el codigo y reenviar la prueba por un canal nuevo`() {
        val r = Rig()
        r.hello(); r.taken()
        val salt = ByteArray(16) { it.toByte() }; val n = nonce(0xA0)
        r.fromHost(FlexLink.T_PAIR_CODE, pairCode(salt, n))
        assertEquals(PhoneLinkPeer.Event.PairingRequested(true), r.events.last())
        assertTrue(r.slot.awaitingCode(r.clock.t))
        assertEquals(PairSubmit.BAD_FORMAT, r.peer.submitCode("12a456"))
        assertEquals(PairSubmit.SENT, r.peer.submitCode("012345"))
        val confirm = r.taken().single()
        assertEquals(FlexLink.T_PAIR_CONFIRM, confirm.header.type)
        val k = FlexAuth.deriveKey("012345", salt, hostId, phoneId)
        assertContentEquals(FlexAuth.proof(k, FlexAuth.ROLE_PHONE, n, 0), confirm.payload)

        // El canal se corta antes del AUTH_OK. Flex OS reconecta y reenvia LA MISMA sal:
        // la prueba sale sola por el canal nuevo, sin volver a teclear.
        r.peer.close("corte")
        val r2peerWire = ArrayList<ByteArray>()
        val events2 = ArrayList<PhoneLinkPeer.Event>()
        val p2 = PhoneLinkPeer(r.host, r.slot, { r2peerWire += it; true }, object : PhoneLinkPeer.Listener {
            override fun onEvent(peer: PhoneLinkPeer, ev: PhoneLinkPeer.Event) { events2 += ev }
            override fun onMessage(peer: PhoneLinkPeer, type: Int, payload: ByteArray) {}
        })
        var c2 = 0L
        fun send2(type: Int, pl: ByteArray, s: Int = 0) =
            FlexLink.fragment(type, s, 1, pl, counterStart = ++c2).forEach { p2.onFrame(it) }
        send2(FlexLink.T_HELLO, HelloInfo(2, hostId).encode(false))
        send2(FlexLink.T_PAIR_CODE, pairCode(salt, n))
        assertTrue(events2.contains(PhoneLinkPeer.Event.ProofResent))
        val again = r2peerWire.map { FlexLink.readFrame(it) as FlexLink.ReadResult.Ok }.last()
        assertEquals(FlexLink.T_PAIR_CONFIRM, again.header.type)
        assertContentEquals(confirm.payload, again.payload)
        // Flex OS acepta: AUTH_OK con su prueba. Ahora SI se guarda el vinculo.
        send2(FlexLink.T_AUTH_OK, FlexAuth.proof(k, FlexAuth.ROLE_HOST, n, 0x55), 0x55)
        assertTrue(p2.authed)
        assertEquals(hostId, r.saved.single().first)
        assertContentEquals(k, r.bonds[hostId])
        assertTrue(events2.any { it is PhoneLinkPeer.Event.SessionOpen && it.newlyPaired })
        assertNull(r.slot.session, "el emparejamiento cerrado no se queda vivo")
    }

    @Test
    fun `codigo rechazado se conserva la sal y caducado la tira`() {
        val r = Rig()
        r.hello(); r.taken()
        r.fromHost(FlexLink.T_PAIR_CODE, pairCode(ByteArray(16), nonce(3)))
        r.peer.submitCode("111111"); r.taken()
        r.fromHost(FlexLink.T_ERR, byteArrayOf(FlexLink.E_AUTH.toByte()))
        val f = r.events.filterIsInstance<PhoneLinkPeer.Event.PairingFailed>().last()
        assertEquals(PairFailure.CODE_REJECTED, f.kind)
        assertNotNull(r.slot.session)
        assertEquals(PairSubmit.SENT, r.peer.submitCode("222222"), "no deja reintentar contra el mismo codigo")
        r.fromHost(FlexLink.T_ERR, byteArrayOf(FlexLink.E_TIMEOUT.toByte()))
        assertEquals(PairFailure.CODE_EXPIRED, r.events.filterIsInstance<PhoneLinkPeer.Event.PairingFailed>().last().kind)
        assertNull(r.slot.session)
        assertEquals(PairSubmit.NO_SESSION, r.peer.submitCode("333333"))
    }

    private fun session(r: Rig, sess: Int = 0x77): Int {
        val k = r.bonds[hostId]!!
        r.hello(); r.taken()
        val n = nonce(5)
        r.fromHost(FlexLink.T_AUTH_CHALLENGE, PayloadWriter().bytes(n).u16(sess).build()); r.taken()
        r.fromHost(FlexLink.T_AUTH_OK, FlexAuth.proof(k, FlexAuth.ROLE_HOST, n, sess), sess)
        assertTrue(r.peer.authed)
        return sess
    }

    @Test
    fun `ordenes con acuse se acusan siempre y se entregan una vez`() {
        val r = Rig(bonded = key(2))
        val s = session(r)
        val frame = FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_FIND_START, session = s, packet = 900, counter = 500), ByteArray(0))
        r.peer.onFrame(frame)
        // Reenvio del MISMO paquete (el ACK se perdio): otro contador, mismo id.
        r.peer.onFrame(FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_FIND_START, session = s, packet = 900, counter = 501), ByteArray(0)))
        assertEquals(1, r.msgs.count { it.first == FlexLink.T_FIND_START }, "una orden repetida se ejecuto dos veces")
        val acks = r.taken().filter { it.header.type == FlexLink.T_ACK }
        assertEquals(2, acks.size)
        assertEquals(900, PayloadReader(acks[0].payload).u16())
        // La MISMA trama otra vez (mismo contador): anti-repeticion, ni acuse.
        r.peer.onFrame(frame)
        assertTrue(r.taken().isEmpty())
        assertTrue(r.peer.dropped > 0)
    }

    @Test
    fun `un mensaje largo llega troceado y se reensambla`() {
        val r = Rig(bonded = key(3))
        val s = session(r)
        val text = ByteArray(700) { (it % 251).toByte() }
        r.fromHost(FlexLink.T_REPLY_REQ, text, s)
        val got = r.msgs.single()
        assertEquals(FlexLink.T_REPLY_REQ, got.first)
        assertContentEquals(text, got.second)
        // Y lo que manda el telefono tambien se trocea con fragmentos llenos de 226 B.
        assertTrue(r.peer.send(FlexLink.T_NOTIF_ADD, ByteArray(500) { 7 }))
        val frames = r.taken().filter { it.header.type == FlexLink.T_NOTIF_ADD }
        assertEquals(3, frames.size)
        assertEquals(FlexLink.MAX_PAYLOAD, frames[0].payload.size)
        assertEquals(500 - 2 * FlexLink.MAX_PAYLOAD, frames[2].payload.size)
        assertTrue(frames.zipWithNext().all { (a, b) -> b.header.counter == a.header.counter + 1 })
    }

    @Test
    fun `latido con y sin sesion, y BYE cierra`() {
        val r = Rig(bonded = key(4))
        r.hello(); r.taken()
        r.fromHost(FlexLink.T_PING)
        assertEquals(FlexLink.T_PONG, r.taken().single().header.type)
        val s = session(r)
        r.fromHost(FlexLink.T_PING, session = s)
        val pong = r.taken().single()
        assertEquals(FlexLink.T_PONG, pong.header.type); assertEquals(s, pong.header.session)
        r.fromHost(FlexLink.T_BYE, session = s)
        assertTrue(r.peer.isClosed)
    }

    @Test
    fun `plazos - un desconocido mudo, una sesion muda y el emparejamiento no`() {
        val mute = Rig()
        mute.clock.t += PhoneLinkPeer.AUTH_TIMEOUT_MS - 1
        assertNull(mute.peer.tick())
        mute.clock.t += 2
        assertNotNull(mute.peer.tick(), "un canal que no se presenta se queda para siempre")

        val r = Rig(bonded = key(5))
        session(r)
        r.clock.t += PhoneLinkPeer.IDLE_TIMEOUT_MS - 1
        assertNull(r.peer.tick())
        r.fromHost(FlexLink.T_PING, session = r.peer.session)      // el latido renueva
        r.clock.t += PhoneLinkPeer.IDLE_TIMEOUT_MS - 1
        assertNull(r.peer.tick())
        r.clock.t += 2
        assertNotNull(r.peer.tick())

        // Con un codigo en pantalla, el silencio es normal: no se cierra a los 40 s.
        val p = Rig()
        p.hello(); p.taken()
        p.fromHost(FlexLink.T_PAIR_CODE, pairCode(ByteArray(16) { 1 }, nonce(2)))
        p.clock.t += PhoneLinkPeer.IDLE_TIMEOUT_MS + 5_000
        assertNull(p.peer.tick())
        // Prueba enviada sin respuesta: se avisa a los 12 s sin cerrar.
        p.peer.submitCode("123456")
        p.clock.t += PhoneLinkPeer.PAIR_CONFIRM_TIMEOUT_MS + 1
        assertNull(p.peer.tick())
        assertTrue(p.events.any { it is PhoneLinkPeer.Event.ProofNotSent })
        // Y la ventana del codigo si cierra.
        p.clock.t += PairingSession.WINDOW_MS
        assertNotNull(p.peer.tick())
    }

    @Test
    fun `canal lleno - el mensaje no sale y se dice`() {
        val r = Rig(bonded = key(6))
        session(r)
        r.outOk = false
        assertFalse(r.peer.send(FlexLink.T_PHONE_STATE, ByteArray(10)))
    }

    @Test
    fun `token del relay - mismo calculo que el P4 y el reloj`() {
        // HMAC-SHA256(clave 00..1f, "flexphone-relay-v2"), 32 primeros hex.
        val k = ByteArray(32) { it.toByte() }
        val mac = FlexAuth.hmacSha256(k, "flexphone-relay-v2".toByteArray())
        val tok = FlexAuth.relayToken(k)
        assertEquals(32, tok.length)
        assertEquals(mac.copyOf(16).joinToString("") { "%02x".format(it) }, tok)
        assertEquals(RELAY_TOKEN_VECTOR, tok)
    }

    companion object {
        /** Vector fijo: lo comprueba tambien tests/host/test_flexlink.c en el reloj. */
        const val RELAY_TOKEN_VECTOR = "5ae3f87bd1b53ec61eab7c39250623a4"
    }
}

class DeviceRegistryTest {
    private var t = 1_000_000L
    private val reg = DeviceRegistry { t }

    @Test
    fun `un dispositivo visto por varios caminos es UNA fila`() {
        reg.setBonds(listOf(
            BondRecord("flexos-p4", "Salon", DeviceModel.UNKNOWN),
            BondRecord("flexos-watch", "", DeviceModel.WATCH_ULTRA, bleAddress = "AA:BB"),
        ))
        // El P4 contesta a la busqueda y ademas abre sesion por Wi-Fi.
        reg.setWifiSeen(listOf(Triple("flexos-p4", "Flex OS Ultra", "192.168.1.30")))
        reg.channelOpened("wifi#1", LinkTransport.WIFI, "192.168.1.30")
        // El reloj abre un canal BLE que aun no se ha presentado: se atribuye por la direccion.
        reg.channelOpened("ble#1", LinkTransport.BLE, "AA:BB")
        var l = reg.list()
        assertEquals(2, l.size, "salio duplicado: $l")
        val p4 = l.first { it.flexosId == "flexos-p4" }
        assertEquals(DeviceModel.ULTRA_P4, p4.model); assertTrue(p4.modelInferred)
        assertEquals(setOf(LinkTransport.WIFI), p4.seenBy)
        val w = l.first { it.flexosId == "flexos-watch" }
        assertTrue(w.channelOpen); assertEquals(LinkTransport.BLE, w.seenBy.single())

        reg.channelHello("ble#1", HelloInfo(2, "flexos-watch", "Flex OS Watch Ultra", HelloInfo.MODEL_WATCH_ULTRA))
        reg.channelSession("ble#1", true)
        reg.channelHello("wifi#1", HelloInfo(2, "flexos-p4"))
        reg.channelSession("wifi#1", true)
        l = reg.list()
        assertEquals(2, l.size)
        assertTrue(l.all { it.online })
        assertEquals("Flex OS Watch Ultra", l.first { it.flexosId == "flexos-watch" }.name)
        assertEquals("Salon", l.first { it.flexosId == "flexos-p4" }.name, "el nombre del vinculo se pierde")
        assertFalse(l.first { it.flexosId == "flexos-watch" }.modelInferred)
    }

    @Test
    fun `canal sin presentar y desconocido sale aparte y se funde al saludar`() {
        reg.channelOpened("ble#9", LinkTransport.BLE, "CC:DD")
        var l = reg.list()
        assertEquals(1, l.size)
        assertNull(l[0].flexosId)
        assertEquals(DeviceModel.WATCH_ULTRA, l[0].model, "por BLE solo puede ser un reloj")
        reg.channelHello("ble#9", HelloInfo(2, "flexos-nuevo"))
        reg.channelPairing("ble#9", "flexos-nuevo", true)
        l = reg.list()
        assertEquals(1, l.size)
        assertEquals("flexos-nuevo", l[0].flexosId)
        assertTrue(l[0].showingCode)
        assertFalse(l[0].paired)
        reg.channelClosed("ble#9")
        assertTrue(reg.list().isEmpty(), "un canal cerrado sin vinculo deja una fila fantasma")
    }

    @Test
    fun `la busqueda caduca y el orden pone primero lo conectado`() {
        reg.setBonds(listOf(BondRecord("flexos-a", "A", DeviceModel.ULTRA_P4)))
        reg.setWifiSeen(listOf(Triple("flexos-b", "B", "10.0.0.2")))
        reg.channelOpened("ble#1", LinkTransport.BLE, null)
        reg.channelHello("ble#1", HelloInfo(2, "flexos-c", "C", HelloInfo.MODEL_WATCH_ULTRA))
        reg.channelSession("ble#1", true)
        val l = reg.list()
        assertEquals(listOf("flexos-c", "flexos-a", "flexos-b"), l.map { it.flexosId })
        t += DeviceRegistry.SEEN_TTL_MS + 1
        assertEquals(listOf("flexos-c", "flexos-a"), reg.list().map { it.flexosId })
        val g = reg.generation
        reg.forget("flexos-a")
        assertTrue(reg.generation > g)
        assertEquals(listOf("flexos-c"), reg.list().map { it.flexosId })
    }
}
