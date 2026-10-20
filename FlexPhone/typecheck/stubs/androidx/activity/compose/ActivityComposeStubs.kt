@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.activity.compose

import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContract
import androidx.compose.runtime.Composable

/** activity-compose vive en Google Maven: aqui solo su firma. */
@Composable
fun <I, O> rememberLauncherForActivityResult(
    contract: ActivityResultContract<I, O>,
    onResult: (O) -> Unit,
): ActivityResultLauncher<I> = object : ActivityResultLauncher<I>() { override fun launch(input: I) {} }
