package com.wiremic.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val WireMicColorScheme = darkColorScheme(
    primary = AccentStart,
    secondary = AccentEnd,
    background = BgBottom,
    surface = CardFill,
    error = Danger,
    onPrimary = TextPrimary,
    onBackground = TextPrimary,
    onSurface = TextPrimary
)

@Composable
fun WireMicTheme(content: @Composable () -> Unit) {
    val view = LocalView.current
    if (!view.isInEditMode) {
        val window = (view.context as? android.app.Activity)?.window
        window?.let {
            it.statusBarColor = BgTop.toArgb()
            it.navigationBarColor = BgTop.toArgb()
            WindowCompat.getInsetsController(it, view).isAppearanceLightStatusBars = false
        }
    }

    MaterialTheme(
        colorScheme = WireMicColorScheme,
        typography = MaterialTheme.typography,
        content = content
    )
}
