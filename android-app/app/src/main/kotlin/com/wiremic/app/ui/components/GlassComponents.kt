package com.wiremic.app.ui.components

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.ui.theme.AccentEnd
import com.wiremic.app.ui.theme.AccentStart
import com.wiremic.app.ui.theme.CardFill
import com.wiremic.app.ui.theme.GlassBorder
import com.wiremic.app.ui.theme.GlassFill
import com.wiremic.app.ui.theme.GlassFillHover
import com.wiremic.app.ui.theme.TextPrimary

@Composable
fun GlassCard(
    modifier: Modifier = Modifier,
    cornerRadius: androidx.compose.ui.unit.Dp = 20.dp,
    content: @Composable () -> Unit
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(cornerRadius))
            .background(CardFill)
            .border(1.dp, GlassBorder, RoundedCornerShape(cornerRadius))
    ) {
        content()
    }
}

@Composable
fun PrimaryButton(
    text: String,
    modifier: Modifier = Modifier,
    busy: Boolean = false,
    enabled: Boolean = true,
    onClick: () -> Unit
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val scale by animateDpAsState(if (pressed) 44.dp else 46.dp, label = "scale")

    Box(
        modifier = modifier
            .height(scale)
            .clip(RoundedCornerShape(12.dp))
            .background(Brush.horizontalGradient(listOf(AccentStart, AccentEnd)))
            .clickable(interactionSource = interaction, indication = null, enabled = enabled && !busy) { onClick() }
            .padding(horizontal = 20.dp),
        contentAlignment = Alignment.Center
    ) {
        if (busy) {
            CircularProgressIndicator(color = Color.White, modifier = Modifier.height(18.dp).padding(2.dp), strokeWidth = 2.dp)
        } else {
            Text(text, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp)
        }
    }
}

@Composable
fun SecondaryButton(
    text: String,
    modifier: Modifier = Modifier,
    tint: Color = TextPrimary,
    onClick: () -> Unit
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()

    Box(
        modifier = modifier
            .height(46.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(if (pressed) GlassFillHover else GlassFill)
            .border(1.dp, GlassBorder, RoundedCornerShape(12.dp))
            .clickable(interactionSource = interaction, indication = null) { onClick() }
            .padding(horizontal = 20.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text, color = tint, fontWeight = FontWeight.Bold, fontSize = 14.sp)
    }
}
