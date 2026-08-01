package com.wiremic.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.DeviceInfo
import com.wiremic.app.ui.components.DeviceRow
import com.wiremic.app.ui.components.GlassCard
import com.wiremic.app.ui.components.PrimaryButton
import com.wiremic.app.ui.components.SecondaryButton
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.TextSecondary

@Composable
fun NearbyDevicesScreen(
    devices: List<DeviceInfo>,
    connectingDeviceId: String?,
    manualStatus: String?,
    onConnect: (String) -> Unit,
    onRefresh: () -> Unit,
    onConnectToAddress: (String) -> Unit
) {
    var address by remember { mutableStateOf("") }

    // Everything is one scrolling list, header included. On a short phone with
    // the keyboard up there is very little room left, and a fixed header above a
    // list would leave the address field with nowhere to go.
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Nearby Devices", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                    Text("Computers found on your network", color = TextSecondary, fontSize = 12.sp)
                }
                SecondaryButton(text = "Refresh", onClick = onRefresh)
            }
        }

        // Kept on the same screen rather than hidden in settings: when discovery
        // fails this is the only way through, and it is no help to anyone if it
        // is somewhere they would have to already know to look.
        item {
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(14.dp)) {
                    Text(
                        "Not showing up?",
                        color = TextPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        "Type the address shown on your computer's WireMic window.",
                        color = TextSecondary,
                        fontSize = 12.sp
                    )

                    Spacer(modifier = Modifier.height(10.dp))

                    OutlinedTextField(
                        value = address,
                        onValueChange = { address = it },
                        singleLine = true,
                        placeholder = { Text("192.168.1.20", color = TextSecondary) },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedTextColor = TextPrimary,
                            unfocusedTextColor = TextPrimary
                        ),
                        modifier = Modifier.fillMaxWidth()
                    )

                    Spacer(modifier = Modifier.height(10.dp))

                    PrimaryButton(
                        text = "Connect to this address",
                        onClick = { onConnectToAddress(address) },
                        modifier = Modifier.fillMaxWidth()
                    )

                    if (manualStatus != null) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(manualStatus, color = TextSecondary, fontSize = 12.sp)
                    }
                }
            }
        }

        if (devices.isEmpty()) {
            item {
                Box(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 28.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text("No devices found yet", color = TextSecondary, fontSize = 13.sp)
                }
            }
        } else {
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
