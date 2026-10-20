@file:Suppress("unused")
package androidx.compose.ui.res

import androidx.compose.runtime.Composable

/** En Android lo da androidx.compose.ui; en escritorio, otra biblioteca. Solo la firma. */
@Composable
fun stringResource(id: Int): String = ""

/** painterResource(R.drawable.x) de androidx.compose.ui (Android). Solo la firma. */
@Composable
fun painterResource(id: Int): androidx.compose.ui.graphics.painter.Painter =
    androidx.compose.ui.graphics.painter.ColorPainter(androidx.compose.ui.graphics.Color.Transparent)
