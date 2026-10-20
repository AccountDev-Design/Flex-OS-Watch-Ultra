@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.activity.result

/** Firma de androidx.activity.result.ActivityResultLauncher. */
abstract class ActivityResultLauncher<I> {
    abstract fun launch(input: I)
}
