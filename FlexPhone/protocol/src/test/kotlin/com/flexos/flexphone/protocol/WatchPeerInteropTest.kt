package com.flexos.flexphone.protocol

import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.io.Writer
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * DE EXTREMO A EXTREMO, SIN RADIO: el PhoneLinkPeer de Kotlin (el que usa el
 * GattServer de Flex Phone) contra el MOTOR EN C DEL RELOJ (components/flexlink,
 * el mismo codigo que va en el firmware), compilado como proceso de PC por
 * `make -C tests/host peer`. Cada trama que cruza es la de verdad, en los dos
 * sentidos: emparejar con el codigo, sesion, notificaciones, relay y su
 * credencial, respuestas con acuse, mensajes troceados, latido y reconexion.
 *
 * Si el binario no esta, la prueba se salta con el motivo; con
 * -DflexInteropRequired=true un binario ausente es un fallo.
 */
class WatchPeerInteropTest {

    private val binary: File? = run {
        val prop = System.getProperty("flexWatchPeer") ?: System.getenv("FLEX_WATCH_PEER")
        val candidates = listOfNotNull(prop, "../../tests/host/build/flexlink_watch_peer")
        candidates.map { File(it) }.firstOrNull { it.canExecute() }
    }

    private fun needBinary(): File? {
        if (binary != null) return binary
        if (System.getProperty("flexInteropRequired") == "true") {
            fail("falta tests/host/build/flexlink_watch_peer: make -C tests/host peer")
        }
        println("WatchPeerInteropTest: SALTADA (falta el reloj de prueba; make -C tests/host peer)")
        return null
    }

    /** El reloj: un proceso con el motor C. */
    private class Watch(bin: File) {
        val proc: Process = ProcessBuilder(bin.absolutePath).redirectErrorStream(true).start()
        private val w: Writer = proc.outputStream.bufferedWriter()
        private val r: BufferedReader = BufferedReader(InputStreamReader(proc.inputStream))
        val toPhone = ArrayDeque<ByteArray>()
        var lastCode: String? = null

        fun cmd(line: String): List<String> {
            w.write(line); w.write("\n"); w.flush()
            val out = ArrayList<String>()
            while (true) {
                val l = r.readLine() ?: error("el reloj de prueba termino: ${out.joinToString("\n")}")
                if (l == "ok") break
                out += l
                when {
                    l.startsWith("frame ") -> toPhone += hex(l.substring(6))
                    l.startsWith("code ") -> lastCode = l.substring(5)
                    l.startsWith("==") || l.contains("ERROR: AddressSanitizer") || l.contains("runtime error") ->
                        error("el motor C fallo: $l")
                }
            }
            return out
        }

        fun state(): Map<String, String> {
            val s = cmd("state").first { it.startsWith("state ") }
            // "err=" va el ultimo y puede llevar espacios.
            val body = s.removePrefix("state ")
            val cut = body.indexOf(" err=")
            val m = Regex("(\\w+)=(\\S*)").findAll(body.substring(0, cut)).associate { it.groupValues[1] to it.groupValues[2] }
            return m + ("err" to body.substring(cut + 5))
        }

        fun close() { runCatching { cmd("quit") }; proc.destroy() }

        companion object {
            fun hex(s: String) = ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }
            fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }
        }
    }

    private class Phone : PhoneLinkPeer.Host, PhoneLinkPeer.Listener {
        var t = 1_000L
        val bonds = HashMap<String, ByteArray>()
        val events = ArrayList<PhoneLinkPeer.Event>()
        val msgs = ArrayList<Pair<Int, ByteArray>>()
        val outbox = ArrayDeque<ByteArray>()
        val slot = PairingSlot()
        override val phoneId = "phone-0011223344556677"
        override val phoneName = "Pixel de prueba"
        override fun bondKey(flexosId: String) = bonds[flexosId]
        override fun savePairing(key: ByteArray, flexosId: String, hello: HelloInfo?) { bonds[flexosId] = key }
        override fun now() = t
        override fun onEvent(peer: PhoneLinkPeer, ev: PhoneLinkPeer.Event) { events += ev }
        override fun onMessage(peer: PhoneLinkPeer, type: Int, payload: ByteArray) { msgs += type to payload }
        fun newPeer() = PhoneLinkPeer(this, slot, { outbox += it; true }, this, "interop")
    }

    private var watch: Watch? = null
    @AfterTest fun tearDown() { watch?.close() }

    /** Mueve tramas hasta que ningun lado tenga nada pendiente. */
    private fun exchange(w: Watch, p: Phone, peer: PhoneLinkPeer, rounds: Int = 50) {
        repeat(rounds) {
            var moved = false
            while (w.toPhone.isNotEmpty()) { peer.onFrame(w.toPhone.removeFirst()); moved = true }
            while (p.outbox.isNotEmpty()) { w.cmd("frame " + Watch.hex(p.outbox.removeFirst())); moved = true }
            w.cmd("tick 40"); p.t += 40
            if (!moved && w.toPhone.isEmpty() && p.outbox.isEmpty()) return
        }
    }

    private fun pairNewWatch(): Triple<Watch, Phone, PhoneLinkPeer>? {
        val bin = needBinary() ?: return null
        val w = Watch(bin).also { watch = it }
        val p = Phone()
        val peer = p.newPeer()
        w.cmd("start")
        w.cmd("pair")
        val code = assertNotNull(w.lastCode?.takeIf { it.length == 6 }, "el reloj no enseno codigo")
        exchange(w, p, peer)
        // El reloj se presento con su modelo y su nombre.
        val hello = assertNotNull(peer.hello)
        assertEquals(HelloInfo.MODEL_WATCH_ULTRA, hello.model)
        assertEquals("Flex OS Watch Ultra", hello.name)
        assertEquals("flexos-00a1b2c3d4e5f607", hello.flexosId)
        assertTrue(p.events.contains(PhoneLinkPeer.Event.PairingRequested(true)), "no llego la sal: ${p.events}")
        assertEquals(PairSubmit.SENT, peer.submitCode(code))
        exchange(w, p, peer)
        w.cmd("confirm")
        exchange(w, p, peer)
        assertTrue(peer.authed, "sin sesion tras emparejar: ${p.events} / ${w.state()}")
        val st = w.state()
        assertEquals("6", st["link"], "el reloj no esta en READY: $st")
        assertEquals("1", st["bonded"])
        assertEquals(p.phoneId, st["peer"])
        return Triple(w, p, peer)
    }

    @Test
    fun `emparejar, sesion, notificacion, relay con credencial y respuesta con acuse`() {
        val (w, p, peer) = pairNewWatch() ?: return
        val key = assertNotNull(p.bonds[peer.flexosId])

        // Telefono -> reloj: capacidades, estado, una notificacion y el relay.
        assertTrue(peer.send(FlexLink.T_CAPS, CapsPayload(0xFF, 0xFF, 2, "Pixel 8", "Google", "15").encode()))
        assertTrue(peer.send(FlexLink.T_NOTIF_ADD, NotifPayload(
            id = 77, pkg = "com.whatsapp", app = "WhatsApp", title = "Ana", text = "¿Vienes a las 8?",
            whenMs = 1_700_000_000_000L, category = Category.MSG, priority = Priority.HIGH,
            hidden = false, sensitive = false, canReply = true, replyAction = 0, actions = listOf("Responder"),
        ).encode()))
        assertTrue(peer.send(FlexLink.T_RELAY_INFO, RelayInfo(
            ip = byteArrayOf(192.toByte(), 168.toByte(), 1, 50), port = 8443, protoVer = 1, tls = false, caps = 3,
        ).encode()))
        exchange(w, p, peer)
        val st = w.state()
        assertEquals("1", st["notifs"]); assertEquals("Ana", st["lastnotif"])
        assertEquals("1", st["caps"])
        assertEquals("2", st["relay"], "relay no UP: $st"); assertEquals("192.168.1.50", st["ip"]); assertEquals("8443", st["port"])
        // LA CREDENCIAL DEL RELAY: el reloj la deriva de SU copia de la clave y
        // coincide con la que el telefono acepta. Nunca viajo.
        assertEquals(FlexAuth.relayToken(key), st["token"])

        // Reloj -> telefono: una respuesta CON ACUSE, larga (troceada).
        val text = "Si, ahi estare. ".repeat(20)
        val req = PayloadWriter().u32(77).u8(0).str(text, 400).build()
        w.cmd("send 30 1 " + Watch.hex(req))
        exchange(w, p, peer)
        val got = p.msgs.filter { it.first == FlexLink.T_REPLY_REQ }
        assertEquals(1, got.size, "la orden llego ${got.size} veces")
        assertContentEquals(req, got.single().second)
        // Con el ACK recibido, el reloj NO la reenvia aunque pase el tiempo.
        w.cmd("tick 6000"); p.t += 6000
        exchange(w, p, peer)
        assertEquals(1, p.msgs.count { it.first == FlexLink.T_REPLY_REQ }, "el reloj reenvio una orden acusada")

        // Latido: 2 minutos de enlace con PING/PONG sin que nadie se caiga.
        repeat(30) { w.cmd("tick 4000"); p.t += 4000; exchange(w, p, peer); assertEquals(null, peer.tick()) }
        assertTrue(peer.pings > 0, "el reloj no late")
        assertEquals("6", w.state()["link"])
    }

    @Test
    fun `se cae el canal y vuelve sin codigo con el reto de sesion`() {
        val (w, p, peer) = pairNewWatch() ?: return
        w.cmd("drop")
        peer.close("BLE desconectado", sayBye = false)
        w.cmd("tick 2000"); p.t += 2000
        w.cmd("reopen")
        val peer2 = p.newPeer()
        exchange(w, p, peer2)
        assertTrue(peer2.authed, "no volvio la sesion: ${p.events.takeLast(5)} ${w.state()}")
        assertTrue(p.events.filterIsInstance<PhoneLinkPeer.Event.SessionOpen>().last().let { !it.newlyPaired })
        assertTrue(p.events.none { it is PhoneLinkPeer.Event.PairingRequested && p.events.indexOf(it) > 3 })
        assertEquals("6", w.state()["link"])
        // Y se sigue hablando por la sesion nueva.
        assertTrue(peer2.send(FlexLink.T_PHONE_STATE, PhoneState(name = "Pixel de prueba", battery = 64, charging = true, net = NetKind.WIFI).encode()))
        exchange(w, p, peer2)
        assertEquals("6", w.state()["link"])
    }

    @Test
    fun `codigo equivocado - el reloj lo rechaza y el bueno empareja despues`() {
        val bin = needBinary() ?: return
        val w = Watch(bin).also { watch = it }
        val p = Phone()
        val peer = p.newPeer()
        w.cmd("start")
        w.cmd("pair")
        val code = w.lastCode!!
        exchange(w, p, peer)
        val wrong = ((code.toInt() + 1) % 1_000_000).toString().padStart(6, '0')
        assertEquals(PairSubmit.SENT, peer.submitCode(wrong))
        exchange(w, p, peer)
        val fail = p.events.filterIsInstance<PhoneLinkPeer.Event.PairingFailed>().lastOrNull()
        assertEquals(PairFailure.CODE_REJECTED, fail?.kind, "eventos: ${p.events}")
        assertTrue(!peer.authed)
        assertEquals(PairSubmit.SENT, peer.submitCode(code))
        exchange(w, p, peer)
        w.cmd("confirm")
        exchange(w, p, peer)
        assertTrue(peer.authed, "con el codigo bueno no empareja: ${w.state()}")
    }
}
