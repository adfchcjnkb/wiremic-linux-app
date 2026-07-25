package com.wiremic.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wiremic.app.core.WireMicViewModel
import com.wiremic.app.service.StreamingForegroundService
import com.wiremic.app.ui.screens.AboutScreen
import com.wiremic.app.ui.screens.ConnectedDeviceScreen
import com.wiremic.app.ui.screens.HomeScreen
import com.wiremic.app.ui.screens.NearbyDevicesScreen
import com.wiremic.app.ui.screens.SettingsScreen
import com.wiremic.app.ui.theme.AccentEnd
import com.wiremic.app.ui.theme.AccentStart
import com.wiremic.app.ui.theme.BgBottom
import com.wiremic.app.ui.theme.BgTop
import com.wiremic.app.ui.theme.SidebarFill
import com.wiremic.app.ui.theme.TextPrimary
import com.wiremic.app.ui.theme.WireMicTheme

class MainActivity : ComponentActivity() {

    private val viewModel: WireMicViewModel by viewModels()

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { viewModel.start() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val permissions = mutableListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        val allGranted = permissions.all {
            checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
        }
        if (allGranted) {
            viewModel.start()
        } else {
            permissionLauncher.launch(permissions.toTypedArray())
        }

        setContent {
            WireMicTheme {
                WireMicApp(viewModel = viewModel, onConnectionEstablished = { deviceName ->
                    val intent = Intent(this, StreamingForegroundService::class.java)
                    intent.putExtra(StreamingForegroundService.EXTRA_DEVICE_NAME, deviceName)
                    startForegroundService(intent)
                }, onConnectionEnded = {
                    stopService(Intent(this, StreamingForegroundService::class.java))
                })
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        viewModel.stop()
    }
}

private data class NavTab(val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector)

@Composable
private fun WireMicApp(
    viewModel: WireMicViewModel,
    onConnectionEstablished: (String) -> Unit,
    onConnectionEnded: () -> Unit
) {
    var currentTab by remember { mutableIntStateOf(0) }

    val devices by viewModel.devices.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val activeDevice by viewModel.activeDevice.collectAsState()
    val connectingDeviceId by viewModel.connectingDeviceId.collectAsState()

    androidx.compose.runtime.LaunchedEffect(activeDevice) {
        if (activeDevice != null) {
            onConnectionEstablished(activeDevice!!.name)
        } else {
            onConnectionEnded()
        }
    }

    val tabs = listOf(
        NavTab("Home", Icons.Filled.Home),
        NavTab("Nearby", Icons.Filled.Search),
        NavTab("Connected", Icons.Filled.Link),
        NavTab("Settings", Icons.Filled.Settings),
        NavTab("About", Icons.Filled.Info)
    )

    var autoConnect by remember { mutableIntStateOf(0) }
    var rememberTrusted by remember { mutableIntStateOf(1) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(BgTop, BgBottom)))
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            Box(modifier = Modifier.weight(1f)) {
                when (currentTab) {
                    0 -> HomeScreen(connectionState, activeDevice, devices.size)
                    1 -> NearbyDevicesScreen(devices, connectingDeviceId, viewModel::connect, viewModel::refreshDevices)
                    2 -> ConnectedDeviceScreen(activeDevice, connectionState, viewModel::disconnect)
                    3 -> SettingsScreen(
                        autoConnect = autoConnect == 1,
                        onAutoConnectChanged = { autoConnect = if (it) 1 else 0 },
                        rememberTrusted = rememberTrusted == 1,
                        onRememberTrustedChanged = { rememberTrusted = if (it) 1 else 0 }
                    )
                    4 -> AboutScreen()
                }
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .navigationBarsPadding()
                    .padding(12.dp)
                    .height(64.dp)
                    .clip(RoundedCornerShape(20.dp))
                    .background(SidebarFill),
                horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                tabs.forEachIndexed { index, tab ->
                    val selected = currentTab == index
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .clip(RoundedCornerShape(14.dp))
                            .then(
                                if (selected) Modifier.background(
                                    Brush.horizontalGradient(listOf(AccentStart, AccentEnd))
                                ) else Modifier
                            )
                            .clickable(
                                indication = null,
                                interactionSource = remember { MutableInteractionSource() }
                            ) { currentTab = index }
                            .padding(horizontal = 14.dp, vertical = 8.dp)
                    ) {
                        Icon(
                            imageVector = tab.icon,
                            contentDescription = tab.label,
                            tint = if (selected) Color.White else TextPrimary.copy(alpha = 0.6f),
                            modifier = Modifier.height(20.dp)
                        )
                        Text(
                            tab.label,
                            color = if (selected) Color.White else TextPrimary.copy(alpha = 0.6f),
                            fontSize = 10.sp
                        )
                    }
                }
            }
        }
    }
}
