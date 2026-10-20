package com.flexos.flexphone.protocol

/**
 * LOS DISPOSITIVOS FLEX, UNA ENTRADA POR DISPOSITIVO.
 *
 * Un mismo Flex OS puede aparecer a la vez por varios caminos: su vinculo
 * guardado, una respuesta a la busqueda Wi-Fi (el P4), un canal BLE abierto
 * (el reloj) que todavia no se ha presentado, una sesion... La lista que ve
 * la persona tiene que tener UNA fila por aparato, no una por camino.
 *
 * IDENTIDAD
 * ---------
 *   · La identidad es el id de Flex OS (`flexos-…`) del HELLO o de la busqueda.
 *   · Un canal que aun no se ha presentado solo tiene direccion (IP o BLE): se
 *     atribuye al aparato que ya se conozca por esa direccion (el vinculo
 *     guarda la ultima direccion BLE y la busqueda la IP). Si no hay ninguno,
 *     sale como "Flex OS conectando" hasta su HELLO, y entonces se funde.
 *
 * MODELO
 * ------
 * El reloj lo declara en su HELLO (HelloInfo.model). El P4 de siempre no lo
 * declara; se deduce del camino con lo que es CIERTO del hardware: el P4 no
 * tiene radio Bluetooth, asi que lo que llega por la busqueda Wi-Fi es un
 * Flex OS Ultra, y un HELLO por BLE sin modelo solo puede ser un reloj.
 *
 * Kotlin puro y sin hilos propios: el servicio lo llama con su cerrojo.
 */
enum class DeviceModel(val label: String) {
    UNKNOWN("Flex OS"),
    ULTRA_P4("Flex OS Ultra"),
    WATCH_ULTRA("Flex OS Watch Ultra");

    companion object {
        fun fromHello(code: Int): DeviceModel = when (code) {
            HelloInfo.MODEL_ULTRA_P4 -> ULTRA_P4
            HelloInfo.MODEL_WATCH_ULTRA -> WATCH_ULTRA
            else -> UNKNOWN
        }
        fun fromName(s: String?): DeviceModel = values().firstOrNull { it.name == s } ?: UNKNOWN
    }
}

enum class LinkTransport(val label: String) { WIFI("Wi-Fi"), BLE("Bluetooth") }

/** Un vinculo guardado, tal como lo da el almacen. */
data class BondRecord(
    val flexosId: String,
    val name: String,
    val model: DeviceModel,
    val bleAddress: String? = null,
    val pairedAt: Long = 0L,
)

/** Una fila de la lista. */
data class FlexDevice(
    /** Clave de la fila: el id de Flex OS, o "canal:…" mientras no se presenta. */
    val key: String,
    val flexosId: String?,
    val name: String,
    val model: DeviceModel,
    /** El modelo se dedujo del camino (no lo declaro el aparato). */
    val modelInferred: Boolean,
    val paired: Boolean,
    /** Caminos por los que se le ve AHORA. */
    val seenBy: Set<LinkTransport>,
    /** Camino de la sesion abierta, o null. */
    val sessionOn: LinkTransport?,
    /** Hay un canal abierto (con o sin sesion). */
    val channelOpen: Boolean,
    /** Esta ensenando un codigo de emparejamiento. */
    val showingCode: Boolean,
    val ip: String?,
    val bleAddress: String?,
    val pairedAt: Long,
    val lastSeen: Long,
) {
    val online: Boolean get() = sessionOn != null
}

class DeviceRegistry(private val now: () -> Long = System::currentTimeMillis) {

    companion object {
        /** Una respuesta de busqueda deja de contar pasado esto. */
        const val SEEN_TTL_MS = 20_000L
    }

    private data class Seen(val id: String, val name: String, val ip: String, val pairing: Boolean, val at: Long)
    private data class Channel(
        val key: String, val transport: LinkTransport, val address: String?,
        var hello: HelloInfo? = null, var flexosId: String? = null,
        var session: Boolean = false, var pairing: Boolean = false, val openedAt: Long,
    )

    private val bonds = LinkedHashMap<String, BondRecord>()
    private val seen = LinkedHashMap<String, Seen>()
    private val channels = LinkedHashMap<String, Channel>()
    /** Lo ultimo que se supo de cada id aunque ya no este (nombre y modelo declarados). */
    private val known = HashMap<String, Pair<String, DeviceModel>>()

    /** Sube con cada cambio: la pantalla se recompone solo cuando cambia. */
    var generation = 0L
        private set

    private fun changed() { generation++ }

    fun setBonds(list: List<BondRecord>) {
        bonds.clear()
        for (b in list) bonds[b.flexosId] = b
        changed()
    }

    /** Respuestas a la busqueda Wi-Fi (el P4 contesta con id, nombre y si ensena codigo). */
    fun setWifiSeen(list: List<Triple<String, String, String>>, pairing: Set<String> = emptySet()) {
        val t = now()
        seen.clear()
        for ((id, name, ip) in list) if (id.isNotEmpty()) seen[id] = Seen(id, name, ip, id in pairing, t)
        changed()
    }

    fun channelOpened(key: String, transport: LinkTransport, address: String?) {
        channels[key] = Channel(key, transport, address, openedAt = now())
        changed()
    }

    fun channelHello(key: String, hello: HelloInfo) {
        val c = channels[key] ?: return
        c.hello = hello
        c.flexosId = hello.flexosId
        if (hello.name.isNotEmpty() || hello.model != HelloInfo.MODEL_UNKNOWN) {
            known[hello.flexosId] = hello.name to DeviceModel.fromHello(hello.model)
        }
        changed()
    }

    /** La sal llego por este canal (el aparato ensena un codigo). */
    fun channelPairing(key: String, flexosId: String, on: Boolean) {
        val c = channels[key] ?: return
        if (c.flexosId == null && flexosId.isNotEmpty()) c.flexosId = flexosId
        c.pairing = on
        changed()
    }

    fun channelSession(key: String, open: Boolean) {
        val c = channels[key] ?: return
        c.session = open
        if (open) c.pairing = false
        changed()
    }

    fun channelClosed(key: String) {
        if (channels.remove(key) != null) changed()
    }

    fun forget(flexosId: String) {
        bonds.remove(flexosId)
        changed()
    }

    // =========================================================
    //  La lista
    // =========================================================
    private class Acc(val key: String) {
        var id: String? = null
        var name = ""
        var model = DeviceModel.UNKNOWN
        var declared = false
        var paired = false
        val seenBy = LinkedHashSet<LinkTransport>()
        var sessionOn: LinkTransport? = null
        var channelOpen = false
        var showingCode = false
        var ip: String? = null
        var ble: String? = null
        var pairedAt = 0L
        var lastSeen = 0L
    }

    fun list(): List<FlexDevice> {
        val t = now()
        val byKey = LinkedHashMap<String, Acc>()
        fun acc(id: String): Acc = byKey.getOrPut(id) { Acc(id).also { it.id = id } }

        // 1) Vinculos: siempre estan, aunque no se les vea.
        for (b in bonds.values) {
            val a = acc(b.flexosId)
            a.paired = true
            a.name = b.name
            if (b.model != DeviceModel.UNKNOWN) { a.model = b.model; a.declared = true }
            a.ble = b.bleAddress
            a.pairedAt = b.pairedAt
        }
        // 2) Busqueda Wi-Fi (solo lo reciente).
        for (s in seen.values) {
            if (t - s.at > SEEN_TTL_MS) continue
            val a = acc(s.id)
            if (a.name.isEmpty()) a.name = s.name
            a.ip = s.ip
            a.seenBy += LinkTransport.WIFI
            a.showingCode = a.showingCode || s.pairing
            a.lastSeen = maxOf(a.lastSeen, s.at)
            // Solo el P4 contesta a la busqueda Wi-Fi.
            if (!a.declared && a.model == DeviceModel.UNKNOWN) a.model = DeviceModel.ULTRA_P4
        }
        // 3) Canales: con id se funden; sin id se atribuyen por direccion.
        for (c in channels.values) {
            val id = c.flexosId ?: attribute(c, byKey)
            val a = if (id != null) acc(id) else byKey.getOrPut("canal:" + c.key) { Acc("canal:" + c.key) }
            a.seenBy += c.transport
            a.channelOpen = true
            a.lastSeen = maxOf(a.lastSeen, c.openedAt)
            if (c.session) a.sessionOn = c.transport
            if (c.pairing) a.showingCode = true
            when (c.transport) {
                LinkTransport.BLE -> if (c.address != null) a.ble = c.address
                LinkTransport.WIFI -> if (c.address != null) a.ip = c.address
            }
            val h = c.hello
            if (h != null) {
                if (h.name.isNotEmpty()) a.name = h.name
                if (h.model != HelloInfo.MODEL_UNKNOWN) { a.model = DeviceModel.fromHello(h.model); a.declared = true }
            }
            if (!a.declared && a.model == DeviceModel.UNKNOWN) {
                a.model = if (c.transport == LinkTransport.BLE) DeviceModel.WATCH_ULTRA else DeviceModel.ULTRA_P4
            }
        }
        // 4) Lo que el propio aparato declaro alguna vez manda sobre lo deducido.
        for (a in byKey.values) {
            val k = a.id?.let { known[it] } ?: continue
            if (k.second != DeviceModel.UNKNOWN) { a.model = k.second; a.declared = true }
            if (a.name.isEmpty() && k.first.isNotEmpty()) a.name = k.first
        }
        return byKey.values.map { a ->
            FlexDevice(
                key = a.key, flexosId = a.id,
                name = a.name.ifEmpty { if (a.id == null) "Flex OS (conectando)" else a.model.label },
                model = a.model, modelInferred = !a.declared,
                paired = a.paired, seenBy = a.seenBy.toSet(), sessionOn = a.sessionOn,
                channelOpen = a.channelOpen, showingCode = a.showingCode,
                ip = a.ip, bleAddress = a.ble, pairedAt = a.pairedAt, lastSeen = a.lastSeen,
            )
        }.sortedWith(
            compareByDescending<FlexDevice> { it.online }
                .thenByDescending { it.showingCode }
                .thenByDescending { it.paired }
                .thenByDescending { it.channelOpen }
                .thenBy { it.name.lowercase() },
        )
    }

    /** ¿De quien es este canal sin presentar? Por su direccion. */
    private fun attribute(c: Channel, byKey: Map<String, Acc>): String? {
        val addr = c.address ?: return null
        for (b in bonds.values) if (c.transport == LinkTransport.BLE && b.bleAddress == addr) return b.flexosId
        if (c.transport == LinkTransport.WIFI) {
            for (s in seen.values) if (s.ip == addr) return s.id
        }
        for (a in byKey.values) {
            if (c.transport == LinkTransport.BLE && a.ble == addr) return a.id
            if (c.transport == LinkTransport.WIFI && a.ip == addr) return a.id
        }
        return null
    }

    fun find(key: String): FlexDevice? = list().firstOrNull { it.key == key || it.flexosId == key }
}
