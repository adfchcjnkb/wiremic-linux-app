package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.DeviceInfo
import com.wiremic.app.ui.components.GlassCard
import com.wiremic.app.ui.components.MicBadge
import com.wiremic.app.ui.components.SecondaryButton
import com.wiremic.app.ui.theme.Danger
import com.wiremic.app.ui.theme.Success
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary

@Composable
fun ConnectedDeviceScreen(
    activeDevice: DeviceInfo?,
    connectionState: String,
    onDisconnect: () -> Unit
) {
    // Scrolls rather than clipping: short screens, landscape, and large system
    // font sizes all make this content taller than the window it is given.
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp)
    ) {
        Text("Connected Device", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)

        androidx.compose.foundation.layout.Spacer(modifier = Modifier.height(20.dp))

        if (activeDevice == null) {
            // Sized, not stretched: this sits inside a scrolling column now, and
            // a child that asks to fill the remaining height has no height to
            // fill when that height is unbounded.
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 48.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = androidx.compose.foundation.layout.Arrangement.Center
                ) {
                    MicBadge(active = false)
                    androidx.compose.foundation.layout.Spacer(modifier = Modifier.height(14.dp))
                    Text("No device is currently connected", color = TextSecondary, fontSize = 13.sp)
                }
            }
        } else {
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    MicBadge(active = true, size = 100.dp)
                    androidx.compose.foundation.layout.Spacer(modifier = Modifier.height(16.dp))
                    Text(activeDevice.name, color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                    Text(activeDevice.model, color = TextSecondary, fontSize = 13.sp)
                    androidx.compose.foundation.layout.Spacer(modifier = Modifier.height(6.dp))
                    Row2(connectionState)
                    androidx.compose.foundation.layout.Spacer(modifier = Modifier.height(20.dp))
                    SecondaryButton(text = "Disconnect", tint = Danger, onClick = onDisconnect, modifier = Modifier.fillMaxWidth())
                }
            }
        }
    }
}

@Composable
private fun Row2(state: String) {
    Box {
        Text(state, color = Success, fontSize = 12.sp)
    }
}
