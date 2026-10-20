# Flex Phone (Android)

Esta carpeta es la app Android **Flex Phone**. Es un proyecto Gradle **independiente** del firmware del reloj y no
comparte ni compila nada con `Flex OS Ultra Watch/`. Hablan solo por los protocolos: Flex Link v2 y FBP/1.

Sirve a la vez a dos dispositivos:

- **Flex OS Ultra** (ESP32-P4/C6), por Wi-Fi (TCP 47820), igual que antes;
- **Flex OS Watch Ultra** (ESP32-S3), por Bluetooth LE.

Les da notificaciones, música, estado del teléfono y el **Browser Relay**: el navegador de los dos usa Internet a
través del teléfono.

## Procedencia

- Importada **sin cambios** de `Flex-OS-Ecosistema/android/FlexPhone` en el commit `5dbc949` (commit `d504090` de este
  repositorio).
- Los cambios para el reloj están encima, en commits propios.
- El repositorio de origen y el firmware del P4/C6 no se han modificado.
- Documentación original: [`docs/FLEX-PHONE.md`](docs/FLEX-PHONE.md).
- Integración con el reloj: [`../docs/FLEX-PHONE-INTEGRACION.md`](../docs/FLEX-PHONE-INTEGRACION.md).

## Módulos

| Módulo | Qué es | Necesita el SDK de Android |
|---|---|---|
| `:protocol` | Flex Link v2 (cabecera, troceo, autenticación, `PhoneLinkPeer`, registro de dispositivos) y FBP/1. Kotlin puro | No |
| `:storage` | Flex Cloud y almacenamiento. Kotlin puro | No |
| `:app` | La app: servicio, servidores Wi-Fi y BLE, relay, interfaz Compose | Sí: solo se incluye si hay `local.properties` o `ANDROID_HOME` |
| `:typecheck` | Compila el código de `:app` contra el `android.jar` real (Robolectric android-all) sin el Android Gradle Plugin | No (`-PflexTypecheck`) |

## Construir e instalar

En un equipo con Android Studio (o con el SDK y JDK 17):

```bash
cd FlexPhone
cp local.properties.example local.properties   # y pon tu sdk.dir
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

En Android 12 o superior, la portada avisa si faltan los permisos de Bluetooth («dispositivos cercanos») y el botón
«Permitir Bluetooth» los pide; al concederlos, el servidor BLE arranca. Sin ellos, el Wi-Fi con Flex OS Ultra sigue
funcionando.

## Pruebas

```bash
./gradlew :protocol:test :storage:test
```

La prueba de interoperabilidad con el reloj (`WatchPeerInteropTest`) arranca el motor C del firmware. Antes hay que
compilarlo desde la raíz del repositorio con `make -C tests/host peer`. Si no está compilado, la prueba se salta y lo
dice. Con `-DflexInteropRequired=true` en la JVM de pruebas, un binario ausente es un fallo.

Comprobar que el código de la app compila sin el Android Gradle Plugin:

```bash
./gradlew -PflexTypecheck :typecheck:compileKotlin
```

## Límites de este entorno de desarrollo

En el entorno donde se hizo la integración del reloj (2026-10-20) no se podía descargar el Android Gradle Plugin
(`dl.google.com` devolvía 403), así que **el APK no se construyó ahí**. Lo que sí se ejecutó:

- `:protocol:test` y `:storage:test`: 244 pruebas, 0 fallos;
- la prueba de interoperabilidad con el motor C real del reloj;
- `:typecheck`.

La primera construcción del APK y las pruebas con el teléfono y el reloj reales están pendientes:
`../docs/PRUEBAS-RELOJ.md`, §10 y §13.
