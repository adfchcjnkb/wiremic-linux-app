package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.ui.components.GlassCard
import com.wiremic.app.ui.theme.AccentStart
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary

@Composable
fun SettingsScreen(
    autoConnect: Boolean,
    onAutoConnectChanged: (Boolean) -> Unit,
    rememberTrusted: Boolean,
    onRememberTrustedChanged: (Boolean) -> Unit
) {
    // Scrolls rather than clipping: short screens, landscape, and large system
    // font sizes all make this content taller than the window it is given.
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp)
    ) {
        Text("Settings", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)

        androidx.compose.foundation.layout.Spacer(modifier = Modifier.padding(top = 20.dp))

        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(18.dp)) {
                SettingRow("Auto Connect", "Accept trusted devices automatically", autoConnect, onAutoConnectChanged)
                androidx.compose.foundation.layout.Spacer(modifier = Modifier.padding(top = 16.dp))
                SettingRow("Remember Trusted Devices", "Save approved computers for faster reconnects", rememberTrusted, onRememberTrustedChanged)
            }
        }
    }
}

@Composable
private fun SettingRow(title: String, subtitle: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, color = TextPrimary, fontSize = 14.sp)
            Text(subtitle, color = TextSecondary, fontSize = 11.sp)
        }
        Switch(
            checked = checked,
            onCheckedChange = onChange,
            colors = SwitchDefaults.colors(checkedTrackColor = AccentStart)
        )
    }
}
