package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.ui.components.GlassCard
import com.wiremic.app.ui.theme.AccentEnd
import com.wiremic.app.ui.theme.AccentStart
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary
import androidx.compose.foundation.background

@Composable
fun AboutScreen() {
    Column(modifier = Modifier.fillMaxSize().padding(20.dp)) {
        Text("About", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)

        androidx.compose.foundation.layout.Spacer(modifier = Modifier.padding(top = 20.dp))

        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(22.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    androidx.compose.foundation.layout.Box(
                        modifier = Modifier
                            .size(52.dp)
                            .background(Brush.linearGradient(listOf(AccentStart, AccentEnd)), RoundedCornerShape(16.dp)),
                        contentAlignment = Alignment.Center
                    ) {
                        Text("W", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 24.sp)
                    }
                    Column(modifier = Modifier.padding(start = 14.dp)) {
                        Text("WireMic", color = TextPrimary, fontSize = 19.sp, fontWeight = FontWeight.Bold)
                        Text("Version 1.0.0", color = TextSecondary, fontSize = 12.sp)
                    }
                }

                androidx.compose.foundation.layout.Spacer(modifier = Modifier.padding(top = 16.dp))

                Text(
                    "WireMic turns this phone into a wireless microphone for a computer on your local network. Audio is streamed with the Opus codec over an encrypted, authenticated connection with no internet server involved.",
                    color = TextSecondary,
                    fontSize = 13.sp,
                    lineHeight = 19.sp
                )
            }
        }
    }
}
