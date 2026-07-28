package com.wiremic.app.core

import android.app.Application
import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.util.UUID

class WireMicViewModel(application: Application) : AndroidViewModel(application), NativeListener {

    private val _devices = MutableStateFlow<List<DeviceInfo>>(emptyList())
    val devices: StateFlow<List<DeviceInfo>> = _devices.asStateFlow()

    private val _connectionState = MutableStateFlow("Idle")
    val connectionState: StateFlow<String> = _connectionState.asStateFlow()

    private val _activeDevice = MutableStateFlow<DeviceInfo?>(null)
    val activeDevice: StateFlow<DeviceInfo?> = _activeDevice.asStateFlow()

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    private val _connectingDeviceId = MutableStateFlow<String?>(null)
    val connectingDeviceId: StateFlow<String?> = _connectingDeviceId.asStateFlow()

    private var started = false
    private var multicastLock: WifiManager.MulticastLock? = null

    fun start() {
        if (started) return
        started = true

        acquireMulticastLock()
        NativeBridge.nativeSetListener(this)

        val prefs = getApplication<Application>().getSharedPreferences("wiremic", 0)
        var deviceId = prefs.getString("device_id", null)
        if (deviceId == null) {
            deviceId = UUID.randomUUID().toString()
            prefs.edit().putString("device_id", deviceId).apply()
        }

        val deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim()
        val deviceModel = Build.MODEL ?: "Android Device"
        val dataDir = getApplication<Application>().filesDir.absolutePath

        viewModelScope.launch {
            NativeBridge.nativeStart(deviceId, deviceName, deviceModel, dataDir)
        }
    }

    fun stop() {
        started = false
        NativeBridge.nativeStop()
        releaseMulticastLock()
    }

    private fun acquireMulticastLock() {
        if (multicastLock != null) return
        runCatching {
            val wifi = getApplication<Application>()
                .applicationContext
                .getSystemService(Context.WIFI_SERVICE) as WifiManager
            val lock = wifi.createMulticastLock("wiremic-discovery")
            lock.setReferenceCounted(false)
            lock.acquire()
            multicastLock = lock
        }
    }

    private fun releaseMulticastLock() {
        runCatching {
            multicastLock?.takeIf { it.isHeld }?.release()
        }
        multicastLock = null
    }

    fun connect(deviceId: String) {
        _connectingDeviceId.value = deviceId
        _lastError.value = null
        NativeBridge.nativeRequestConnection(deviceId)
    }

    fun disconnect() {
        NativeBridge.nativeDisconnect()
    }

    fun refreshDevices() {
        NativeBridge.nativeRefreshDiscovery()
    }

    override fun onDeviceListChanged(devicesJson: String) {
        _devices.value = DeviceInfo.listFromJson(devicesJson)
    }

    override fun onConnectionStateChanged(state: String) {
        _connectionState.value = state
        if (state != "RequestSent") {
            _connectingDeviceId.value = null
        }
    }

    override fun onConnectionEstablished(deviceJson: String) {
        _activeDevice.value = DeviceInfo.fromJson(JSONObject(deviceJson))
        _connectingDeviceId.value = null
    }

    override fun onConnectionClosed(reasonCode: String) {
        _activeDevice.value = null
    }

    override fun onConnectionFailed(reason: String) {
        _lastError.value = reason
        _connectingDeviceId.value = null
        _activeDevice.value = null
    }

    override fun onNativeError(message: String) {
        _lastError.value = message
    }

    override fun onCleared() {
        super.onCleared()
        NativeBridge.nativeSetListener(null)
        releaseMulticastLock()
    }
}
