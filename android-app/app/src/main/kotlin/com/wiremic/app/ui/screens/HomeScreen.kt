package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.DeviceInfo
import com.wiremic.app.ui.components.GlassCard
import com.wiremic.app.ui.components.MicBadge
import com.wiremic.app.ui.theme.Success
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary

@Composable
fun HomeScreen(
    connectionState: String,
    activeDevice: DeviceInfo?,
    deviceCount: Int
) {
    val connected = activeDevice != null

    Column(modifier = Modifier.fillMaxSize().padding(20.dp)) {
        Text("WireMic", color = TextPrimary, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text(
            "Turn this phone into a wireless microphone",
            color = TextSecondary,
            fontSize = 13.sp
        )

        Spacer(modifier = Modifier.height(28.dp))

        GlassCard(modifier = Modifier.fillMaxWidth().height(260.dp)) {
            Column(
                modifier = Modifier.fillMaxSize().padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Spacer(modifier = Modifier.height(8.dp))
                MicBadge(active = connected, size = 108.dp)
                Spacer(modifier = Modifier.height(18.dp))
                Text(
                    text = if (connected) "Streaming to ${activeDevice?.name}" else "Not Connected",
                    color = if (connected) Success else TextPrimary,
                    fontWeight = FontWeight.Bold,
                    fontSize = 17.sp,
                    textAlign = TextAlign.Center
                )
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    text = if (connected) connectionState else "Open Nearby Devices to connect to a computer",
                    color = TextSecondary,
                    fontSize = 12.sp,
                    textAlign = TextAlign.Center
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        Row(modifier = Modifier.fillMaxWidth()) {
            GlassCard(modifier = Modifier.weight(1f).height(84.dp)) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Devices Found", color = TextSecondary, fontSize = 11.sp)
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(deviceCount.toString(), color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}
