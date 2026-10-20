package com.flexos.flexphone.storage

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.flexos.flexphone.protocol.BondRecord
import com.flexos.flexphone.protocol.DeviceModel
import com.flexos.flexphone.protocol.FlexAuth
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * ALMACEN DE LOS VINCULOS.
 *
 * Guarda la clave de cada emparejamiento y el identificador estable de
 * este telefono. Va aparte de [SettingsStore] a proposito: los
 * ajustes son preferencias y esto es MATERIAL DE CLAVE, y las dos
 * cosas no se borran ni se respaldan igual.
 *
 * UN VINCULO POR DISPOSITIVO
 * --------------------------
 * Antes habia UNO: el telefono solo conocia "el Flex OS". Con Flex OS
 * Ultra (P4, por Wi-Fi) y Flex OS Watch Ultra (S3, por Bluetooth) a la vez,
 * cada uno tiene su propia clave, su nombre y su modelo, indexados por su
 * id de Flex OS (`flexos-…`). El vinculo unico de antes se MIGRA solo, la
 * primera vez, a la entrada de su id: quien ya tenia el P4 emparejado no
 * tiene que volver a emparejar.
 *
 * POR QUE NO EN CLARO
 * -------------------
 * Cada clave se envuelve con una clave del **Android Keystore**, que no sale
 * del almacen de claves del sistema: lo que acaba en disco es un cifrado, no
 * la clave. AES/GCM del propio sistema, sin dependencias.
 *
 * QUE NO SE GUARDA AQUI: el codigo de seis digitos. Es de un solo
 * uso, caduca en dos minutos y no hace falta despues.
 */
class BondStore(ctx: Context) {

    private val prefs = ctx.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    companion object {
        private const val FILE = "flexphone_bond"
        private const val KEY_ALIAS = "flexphone.bond.v2"
        // ---- Formato antiguo (un solo vinculo): solo se lee para migrar ----
        private const val P_BLOB = "bond_blob"
        private const val P_IV = "bond_iv"
        private const val P_PEER = "bond_peer"
        private const val P_NAME = "bond_name"
        private const val P_AT = "bond_at"
        // ---- Formato actual ----
        private const val P_SELF = "self_id"          // id estable de ESTE telefono
        private const val P_IDS = "bond_ids"          // ids separados por '\n'
        private fun k(id: String, f: String) = "dev.$id.$f"
        private const val GCM_TAG_BITS = 128
        /** Tope de vinculos: es una lista de aparatos de una persona, no un directorio. */
        const val MAX_BONDS = 8
    }

    private val lock = Any()

    // ---------------------------------------------------------
    //  Clave envolvente (Android Keystore)
    // ---------------------------------------------------------
    private fun wrapKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        kg.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                // Sin exigir autenticacion del usuario: el enlace tiene
                // que poder reanudarse con la pantalla apagada.
                .setUserAuthenticationRequired(false)
                .build(),
        )
        return kg.generateKey()
    }

    // ---------------------------------------------------------
    //  Identidad de este telefono
    // ---------------------------------------------------------
    /**
     * Identificador ESTABLE de este telefono. Entra en la derivacion
     * de la clave, asi que tiene que sobrevivir a los reinicios.
     * Se genera al azar la primera vez (nunca ANDROID_ID).
     */
    fun selfId(): String {
        prefs.getString(P_SELF, null)?.let { if (it.isNotEmpty()) return it }
        val b = ByteArray(8).also { SecureRandom().nextBytes(it) }
        val id = "phone-" + b.joinToString("") { "%02x".format(it) }
        prefs.edit().putString(P_SELF, id).apply()
        return id
    }

    // ---------------------------------------------------------
    //  Indice y migracion
    // ---------------------------------------------------------
    private fun ids(): List<String> {
        migrate()
        return prefs.getString(P_IDS, "")!!.split('\n').filter { it.isNotEmpty() }
    }

    /** El vinculo unico de antes pasa a ser la entrada de su id. Una vez. */
    private fun migrate() = synchronized(lock) {
        val blob = prefs.getString(P_BLOB, null) ?: return@synchronized
        val iv = prefs.getString(P_IV, null)
        val peer = prefs.getString(P_PEER, null)
        val e = prefs.edit()
        if (iv != null && !peer.isNullOrEmpty()) {
            val list = prefs.getString(P_IDS, "")!!.split('\n').filter { it.isNotEmpty() }
            // El cifrado se COPIA tal cual: la clave envolvente es la misma.
            e.putString(k(peer, "blob"), blob).putString(k(peer, "iv"), iv)
                .putString(k(peer, "name"), prefs.getString(P_NAME, "") ?: "")
                .putLong(k(peer, "at"), prefs.getLong(P_AT, 0L))
                // Antes solo existia el P4 por Wi-Fi: el modelo se deja sin declarar
                // y la lista lo deduce del camino por el que vuelva.
                .putString(k(peer, "model"), DeviceModel.UNKNOWN.name)
                .putString(P_IDS, (list.filter { it != peer } + peer).joinToString("\n"))
        }
        e.remove(P_BLOB).remove(P_IV).remove(P_PEER).remove(P_NAME).remove(P_AT)
        // commit: la migracion no puede quedarse a medias si el proceso muere ahora.
        e.commit()
    }

    // ---------------------------------------------------------
    //  Los vinculos
    // ---------------------------------------------------------
    fun isPaired(): Boolean = ids().isNotEmpty()

    fun isPaired(flexosId: String): Boolean = flexosId in ids()

    /** Todos los vinculos, el mas reciente primero. */
    fun list(): List<BondRecord> = ids().map { id ->
        BondRecord(
            flexosId = id,
            name = prefs.getString(k(id, "name"), "") ?: "",
            model = DeviceModel.fromName(prefs.getString(k(id, "model"), null)),
            bleAddress = prefs.getString(k(id, "ble"), null),
            pairedAt = prefs.getLong(k(id, "at"), 0L),
        )
    }.sortedByDescending { it.pairedAt }

    /** El vinculo mas reciente (lo que antes era "el" vinculo). */
    private fun latest(): BondRecord? = list().firstOrNull()

    fun peerId(): String? = latest()?.flexosId
    fun peerName(): String? = latest()?.name
    fun pairedAt(): Long = latest()?.pairedAt ?: 0L

    /**
     * Guarda (o sustituye) el vinculo con [peerId]. commit(), NO apply(): el
     * emparejamiento tiene que estar en disco ANTES de que nada mas ocurra.
     */
    fun save(
        key: ByteArray, peerId: String, peerName: String?,
        model: DeviceModel = DeviceModel.UNKNOWN, bleAddress: String? = null,
    ) = synchronized(lock) {
        require(key.size == FlexAuth.KEY_SIZE) { "clave de tamano incorrecto" }
        require(peerId.isNotEmpty()) { "id de Flex OS vacio" }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, wrapKey())
        val blob = c.doFinal(key)
        var list = ids().filter { it != peerId }
        // Con el tope lleno se suelta el MAS ANTIGUO, no el que se esta guardando.
        while (list.size >= MAX_BONDS) {
            val oldest = list.minByOrNull { prefs.getLong(k(it, "at"), 0L) } ?: break
            removeFields(prefs.edit(), oldest).commit()
            list = list.filter { it != oldest }
        }
        val e = prefs.edit()
            .putString(k(peerId, "blob"), Base64.encodeToString(blob, Base64.NO_WRAP))
            .putString(k(peerId, "iv"), Base64.encodeToString(c.iv, Base64.NO_WRAP))
            .putLong(k(peerId, "at"), System.currentTimeMillis())
            .putString(P_IDS, (list + peerId).joinToString("\n"))
        // Un nombre o un modelo que no se conocen NO pisan lo que ya habia.
        if (!peerName.isNullOrEmpty()) e.putString(k(peerId, "name"), peerName)
        if (model != DeviceModel.UNKNOWN || !prefs.contains(k(peerId, "model"))) e.putString(k(peerId, "model"), model.name)
        if (bleAddress != null) e.putString(k(peerId, "ble"), bleAddress)
        if (!e.commit()) throw IllegalStateException("no se pudo guardar el vinculo")
    }

    /** Lo que el aparato declaro despues (nombre, modelo, direccion BLE). No toca la clave. */
    fun updateInfo(peerId: String, name: String?, model: DeviceModel?, bleAddress: String?) = synchronized(lock) {
        if (peerId !in ids()) return@synchronized
        val e = prefs.edit()
        if (!name.isNullOrEmpty()) e.putString(k(peerId, "name"), name)
        if (model != null && model != DeviceModel.UNKNOWN) e.putString(k(peerId, "model"), model.name)
        if (bleAddress != null) e.putString(k(peerId, "ble"), bleAddress)
        e.apply()
    }

    /**
     * La clave del vinculo con [peerId], o null si no hay o el almacen ya no
     * puede descifrarla (restauracion o reinstalacion: se limpia esa entrada
     * para que la app diga "vuelve a emparejar").
     */
    fun key(peerId: String): ByteArray? {
        val blob = prefs.getString(k(peerId, "blob"), null) ?: return null
        val iv = prefs.getString(k(peerId, "iv"), null) ?: return null
        return try {
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(
                Cipher.DECRYPT_MODE, wrapKey(),
                GCMParameterSpec(GCM_TAG_BITS, Base64.decode(iv, Base64.NO_WRAP)),
            )
            c.doFinal(Base64.decode(blob, Base64.NO_WRAP))
        } catch (e: javax.crypto.AEADBadTagException) {
            forget(peerId); null
        } catch (e: java.security.UnrecoverableKeyException) {
            forget(peerId); null
        } catch (e: Exception) {
            // Un fallo PASAJERO del Keystore NO borra el vinculo: se reintenta luego.
            com.flexos.flexphone.protocol.LinkDiag.w("FlexPhone/Bond", "Keystore no pudo descifrar ahora: ${e.javaClass.simpleName}")
            null
        }
    }

    /** La clave del vinculo mas reciente (compatibilidad). */
    fun key(): ByteArray? = peerId()?.let { key(it) }

    /** Credenciales del Browser Relay de TODOS los vinculos (cada aparato la suya). */
    fun relayTokens(): Set<String> = ids().mapNotNull { key(it)?.let(FlexAuth::relayToken) }.toSet()

    private fun removeFields(e: android.content.SharedPreferences.Editor, id: String) = e
        .remove(k(id, "blob")).remove(k(id, "iv")).remove(k(id, "name"))
        .remove(k(id, "model")).remove(k(id, "ble")).remove(k(id, "at"))

    /** Olvida UN dispositivo. */
    fun forget(peerId: String) = synchronized(lock) {
        val list = ids().filter { it != peerId }
        removeFields(prefs.edit(), peerId).putString(P_IDS, list.joinToString("\n")).commit()
        if (list.isEmpty()) deleteWrapKey()
    }

    /**
     * Revoca TODOS los vinculos. Borra los cifrados Y la clave envolvente del
     * Keystore: dejar la envolvente viva permitiria descifrar una copia de
     * seguridad antigua del fichero.
     */
    fun clear() = synchronized(lock) {
        val e = prefs.edit()
        for (id in prefs.getString(P_IDS, "")!!.split('\n').filter { it.isNotEmpty() }) removeFields(e, id)
        e.remove(P_IDS).remove(P_BLOB).remove(P_IV).remove(P_PEER).remove(P_NAME).remove(P_AT).commit()
        deleteWrapKey()
    }

    private fun deleteWrapKey() {
        runCatching {
            KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry(KEY_ALIAS)
        }
    }
}
