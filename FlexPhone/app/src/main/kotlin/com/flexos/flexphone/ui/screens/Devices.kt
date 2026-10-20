package com.flexos.flexphone.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.flexos.flexphone.R
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.RelayClient
import com.flexos.flexphone.domain.RelayState
import com.flexos.flexphone.domain.relayStatusText
import com.flexos.flexphone.link.FlexLinkService
import com.flexos.flexphone.link.bluetoothPermissions
import com.flexos.flexphone.link.hasPermissions
import com.flexos.flexphone.protocol.DeviceModel
import com.flexos.flexphone.protocol.FlexDevice
import com.flexos.flexphone.protocol.LinkTransport
import com.flexos.flexphone.relay.BrowserRelayService
import com.flexos.flexphone.ui.FlexTopBar
import com.flexos.flexphone.ui.Routes
import kotlinx.coroutines.flow.MutableStateFlow
import java.text.DateFormat
import java.util.Date

/**
 * LOS DISPOSITIVOS FLEX, EN LA PORTADA Y UNO A UNO.
 *
 * Una fila por aparato (DeviceRegistry ya junta lo que llega por varios
 * caminos), con el MISMO dibujo que usa el reloj: design/devices/<modelo>.svg,
 * convertido a VectorDrawable por tools/devicons/gen_devicons.py. Tocar una
 * fila abre la pantalla de ESE aparato, que cambia segun lo que es: el reloj
 * por Bluetooth y su navegador, o el P4 por Wi-Fi.
 */

/** El dibujo de cada modelo (res/drawable/ic_dev_<modelo>.xml, generados desde SVG). */
fun deviceIcon(m: DeviceModel): Int = when (m) {
    DeviceModel.WATCH_ULTRA -> R.drawable.ic_dev_watch_ultra
    DeviceModel.ULTRA_P4 -> R.drawable.ic_dev_ultra_p4
    DeviceModel.UNKNOWN -> R.drawable.ic_dev_unknown
}

/** El estado de una fila, en palabras y con su color. */
fun deviceStatus(d: FlexDevice): Pair<String, FlexStatus> = when {
    d.sessionOn != null -> "Conectado por ${d.sessionOn?.label}" to FlexStatus.OK
    d.showingCode -> "Ensena un codigo: teclealo aqui" to FlexStatus.BUSY
    d.channelOpen -> "Conectando por ${d.seenBy.joinToString(" y ") { it.label }}" to FlexStatus.BUSY
    d.paired && d.seenBy.isNotEmpty() -> "Emparejado · visto por ${d.seenBy.joinToString(" y ") { it.label }}" to FlexStatus.BUSY
    d.paired -> "Emparejado · sin conexion ahora" to FlexStatus.OFF
    else -> "Visto por ${d.seenBy.joinToString(" y ") { it.label }} · sin emparejar" to FlexStatus.OFF
}

@Composable
fun DeviceRow(d: FlexDevice, onClick: () -> Unit) {
    val (text, st) = deviceStatus(d)
    GlassCard(Modifier.clickable(onClick = onClick), accent = if (d.online) statusColor(FlexStatus.OK) else null) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Image(
                painter = painterResource(deviceIcon(d.model)),
                contentDescription = d.model.label,
                modifier = Modifier.size(52.dp),
            )
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1f)) {
                Text(d.name, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(
                    d.model.label + if (d.modelInferred && d.model != DeviceModel.UNKNOWN) " (por el camino)" else "",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    StatusDot(st, size = 10)
                    Spacer(Modifier.width(6.dp))
                    Text(text, style = MaterialTheme.typography.labelMedium, color = statusColor(st),
                        maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
            }
            Text("›", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** La seccion "Dispositivos" de la portada. */
@Composable
fun DevicesSection(nav: NavController) {
    val state = FlexPhoneState.instance
    val devices by (state?.devices ?: MutableStateFlow(emptyList())).collectAsState()
    val transports by (state?.transports ?: MutableStateFlow(FlexPhoneState.Transports())).collectAsState()
    SectionHeader("Dispositivos")
    if (devices.isEmpty()) {
        EmptyState(
            "Ningun Flex OS todavia",
            "Activa el enlace. Flex OS Watch Ultra se conecta por Bluetooth y Flex OS Ultra por la Wi-Fi de casa; " +
                "pulsa \"Emparejar telefono\" en el aparato y teclea aqui su codigo.",
        )
    }
    for (d in devices) {
        DeviceRow(d) {
            state?.selectDevice(d.key)
            nav.navigate(Routes.FLEX_DEVICE)
        }
    }
    if (!transports.ble && transports.bleWhy != null) BluetoothNotice(transports.bleWhy!!)
}

/** Sin Bluetooth no hay reloj: se dice por que y, si son permisos, se piden aqui. */
@Composable
fun BluetoothNotice(why: String) {
    val ctx = LocalContext.current
    var granted by remember { mutableStateOf(hasPermissions(ctx)) }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { res ->
        granted = res.values.all { it }
        if (granted) FlexLinkService.current?.retryBluetooth()
    }
    Notice(
        "Bluetooth no disponible",
        "Flex OS Watch Ultra habla con el telefono por Bluetooth: $why. Flex OS Ultra (Wi-Fi) no se ve afectado.",
        MaterialTheme.colorScheme.tertiary,
    )
    if (!granted) {
        Button(onClick = { ask.launch(bluetoothPermissions()) }, modifier = Modifier.fillMaxWidth()) {
            Text("Permitir Bluetooth (dispositivos cercanos)")
        }
    }
}

// =============================================================
//  La pantalla de un dispositivo
// =============================================================
@Composable
fun FlexDeviceScreen(nav: NavController) {
    val ctx = LocalContext.current
    val state = FlexPhoneState.instance
    val devices by (state?.devices ?: MutableStateFlow(emptyList())).collectAsState()
    val key by (state?.selectedDevice ?: MutableStateFlow<String?>(null)).collectAsState()
    val relay by (state?.relay ?: MutableStateFlow(RelayState.OFF)).collectAsState()
    val relayClient by (state?.relayClient ?: MutableStateFlow(RelayClient.NONE)).collectAsState()
    val relayByUser by (state?.relayStoppedByUser ?: MutableStateFlow(false)).collectAsState()
    val address by (state?.address ?: MutableStateFlow<String?>(null)).collectAsState()
    // La fila puede cambiar de clave al presentarse (canal -> id): se busca por las dos.
    val d = devices.firstOrNull { it.key == key || (it.flexosId != null && it.flexosId == key) }
    var confirmForget by remember { mutableStateOf(false) }

    Scaffold(topBar = { FlexTopBar(d?.name ?: "Dispositivo") { nav.popBackStack() } }) { pad ->
        Column(
            Modifier.padding(pad).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (d == null) {
                EmptyState("Este dispositivo ya no esta", "Se olvido o dejo de verse. Vuelve a la portada.")
                return@Column
            }
            val (text, st) = deviceStatus(d)
            GlassCard(accent = statusColor(st)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Image(painterResource(deviceIcon(d.model)), d.model.label, Modifier.size(96.dp))
                    Spacer(Modifier.width(16.dp))
                    Column(Modifier.weight(1f)) {
                        Text(d.name, style = MaterialTheme.typography.titleLarge, maxLines = 2)
                        Text(d.model.label, style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            StatusDot(st); Spacer(Modifier.width(8.dp))
                            Text(text, style = MaterialTheme.typography.bodyMedium, color = statusColor(st))
                        }
                    }
                }
            }
            if (d.showingCode) {
                Button(onClick = { nav.navigate(Routes.PAIR) }, modifier = Modifier.fillMaxWidth()) {
                    Text("Teclear el codigo que ensena")
                }
            }

            // ---- Lo comun ----
            SectionCard("Identidad") {
                KeyValue("Modelo", d.model.label + if (d.modelInferred) " (deducido del camino)" else " (declarado)")
                KeyValue("Identificador", d.flexosId ?: "aun no se ha presentado")
                KeyValue("Caminos", if (d.seenBy.isEmpty()) "ninguno ahora" else d.seenBy.joinToString(", ") { it.label })
                if (d.paired && d.pairedAt > 0) {
                    KeyValue("Emparejado", DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(d.pairedAt)))
                }
            }

            // ---- Lo de cada modelo ----
            when (d.model) {
                DeviceModel.WATCH_ULTRA -> {
                    SectionCard("Bluetooth") {
                        KeyValue("Enlace", if (d.sessionOn == LinkTransport.BLE) "Flex Link v2 con sesion" else "sin sesion")
                        Text(
                            "El reloj recibe las notificaciones, la musica y el estado del telefono por Bluetooth " +
                                "LE, aunque ninguno de los dos tenga Internet.",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    SectionCard("Navegador del reloj") {
                        KeyValue("Servidor del telefono", relayStatusText(relay, relayClient, relayByUser))
                        KeyValue("Direccion", if (address != null) "$address (red Wi-Fi)" else "sin Wi-Fi en el telefono")
                        Text(
                            "El reloj abre la web a traves de este telefono: el telefono la carga y se la manda " +
                                "como imagenes. Las imagenes viajan por Wi-Fi (no caben por Bluetooth), asi que el reloj " +
                                "tiene que estar en la MISMA red que el telefono o en su punto de acceso. La credencial " +
                                "sale del emparejamiento y nunca viaja.",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        if (relay == RelayState.UP || relay == RelayState.STARTING) {
                            OutlinedButton(onClick = { BrowserRelayService.stop(ctx) }, modifier = Modifier.fillMaxWidth()) {
                                Text("Detener el servidor del navegador")
                            }
                        } else {
                            Button(onClick = { BrowserRelayService.start(ctx) }, modifier = Modifier.fillMaxWidth()) {
                                Text("Iniciar el servidor del navegador")
                            }
                        }
                    }
                    NavRow("Notificaciones que llegan al reloj", "Que apps se reflejan") { nav.navigate(Routes.APPS) }
                }
                DeviceModel.ULTRA_P4 -> {
                    SectionCard("Wi-Fi") {
                        KeyValue("Direccion de Flex OS", d.ip ?: "--")
                        KeyValue("Este telefono", address?.let { "$it:${FlexLinkService.current?.linkPort() ?: 0}" } ?: "sin Wi-Fi")
                    }
                    NavRow("Servidor del navegador", relayStatusText(relay, relayClient, relayByUser)) { nav.navigate(Routes.RELAY) }
                    NavRow("Flex Cloud", "Espacio de este telefono para Flex OS Ultra") { nav.navigate(Routes.CLOUD) }
                    NavRow("Conexion", "Buscar y conectar por la red") { nav.navigate(Routes.CONNECTION) }
                }
                DeviceModel.UNKNOWN -> Notice(
                    "Aun no se sabe que es",
                    "El aparato no ha dicho su modelo. Cuando termine de presentarse se ensena su pantalla.",
                )
            }

            val forgetId = d.flexosId
            if (d.paired && forgetId != null) {
                if (!confirmForget) {
                    OutlinedButton(onClick = { confirmForget = true }, modifier = Modifier.fillMaxWidth()) {
                        Text("Olvidar este dispositivo")
                    }
                } else {
                    Notice(
                        "¿Olvidar ${d.name}?",
                        "Se borra su clave de este telefono y se cierra su sesion. Para volver a usarlo hay que " +
                            "emparejar de nuevo con el codigo.",
                        MaterialTheme.colorScheme.error,
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { confirmForget = false }, modifier = Modifier.weight(1f)) { Text("Cancelar") }
                        Button(
                            onClick = {
                                FlexLinkService.current?.forgetDevice(forgetId)
                                    ?: com.flexos.flexphone.storage.BondStore(ctx).forget(forgetId)
                                confirmForget = false
                                nav.popBackStack()
                            },
                            modifier = Modifier.weight(1f),
                        ) { Text("Olvidar") }
                    }
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}
