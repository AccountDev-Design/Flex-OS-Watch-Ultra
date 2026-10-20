@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.activity.result.contract

/** Firmas de androidx.activity.result.contract (Google Maven). */
abstract class ActivityResultContract<I, O>

class ActivityResultContracts {
    class RequestMultiplePermissions : ActivityResultContract<Array<String>, Map<String, Boolean>>()
    class RequestPermission : ActivityResultContract<String, Boolean>()
}
