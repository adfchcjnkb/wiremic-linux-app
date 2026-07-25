package com.wiremic.app.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.PhoneAndroid
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.DeviceInfo
import com.wiremic.app.ui.theme.AccentEnd
import com.wiremic.app.ui.theme.AccentStart
import com.wiremic.app.ui.theme.Success
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary
import com.wiremic.app.ui.theme.TextTertiary

@Composable
fun DeviceRow(device: DeviceInfo, busy: Boolean, onConnect: () -> Unit) {
    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(46.dp)
                    .background(Brush.linearGradient(listOf(AccentStart, AccentEnd)), shape = androidx.compose.foundation.shape.RoundedCornerShape(14.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = if (device.platform == "linux") Icons.Filled.Computer else Icons.Filled.PhoneAndroid,
                    contentDescription = null,
                    tint = Color.White
                )
            }

            Column(modifier = Modifier.weight(1f).padding(start = 12.dp, end = 8.dp)) {
                Text(device.name, color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                Text("${device.model} · ${device.ip}", color = TextSecondary, fontSize = 11.sp)
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .background(if (device.status == "Online") Success else TextTertiary, CircleShape)
                )
            }

            PrimaryButton(
                text = if (busy) "" else "Connect",
                busy = busy,
                modifier = Modifier.padding(start = 12.dp),
                onClick = onConnect
            )
        }
    }
}

@Composable
fun MicBadge(active: Boolean, size: androidx.compose.ui.unit.Dp = 96.dp) {
    val infiniteTransition = rememberInfiniteTransition(label = "pulse")
    val pulseScale by infiniteTransition.animateFloat(
        initialValue = 0.75f,
        targetValue = 1.15f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "pulseScale"
    )
    val pulseAlpha by infiniteTransition.animateFloat(
        initialValue = 0.5f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "pulseAlpha"
    )

    Box(contentAlignment = Alignment.Center, modifier = Modifier.size(size)) {
        if (active) {
            Box(
                modifier = Modifier
                    .size(size)
                    .scale(pulseScale)
                    .background(Color.Transparent, CircleShape)
                    .then(Modifier.border(1.5.dp, AccentStart.copy(alpha = pulseAlpha), CircleShape))
            )
        }

        Box(
            modifier = Modifier
                .size(size * 0.76f)
                .background(
                    brush = if (active) Brush.linearGradient(listOf(AccentStart, AccentEnd))
                            else SolidColor(Color(0xFF232633)) as Brush,
                    shape = CircleShape
                ),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Filled.Mic,
                contentDescription = null,
                tint = if (active) Color.White else TextTertiary,
                modifier = Modifier.size(size * 0.34f)
            )
        }
    }
}
