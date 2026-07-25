package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.DeviceInfo
import com.wiremic.app.ui.components.DeviceRow
import com.wiremic.app.ui.components.SecondaryButton
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary

@Composable
fun NearbyDevicesScreen(
    devices: List<DeviceInfo>,
    connectingDeviceId: String?,
    onConnect: (String) -> Unit,
    onRefresh: () -> Unit
) {
    Column(modifier = Modifier.fillMaxSize().padding(20.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text("Nearby Devices", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Text("Computers found on your network", color = TextSecondary, fontSize = 12.sp)
            }
            SecondaryButton(text = "Refresh", onClick = onRefresh)
        }

        androidx.compose.foundation.layout.Spacer(modifier = Modifier.padding(top = 10.dp))

        if (devices.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("No devices found yet", color = TextSecondary, fontSize = 13.sp)
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize().padding(top = 16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                items(devices, key = { it.id }) { device ->
                    DeviceRow(
                        device = device,
                        busy = connectingDeviceId == device.id,
                        onConnect = { onConnect(device.id) }
                    )
                }
            }
        }
    }
}
