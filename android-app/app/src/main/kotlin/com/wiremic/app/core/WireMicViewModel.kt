package com.wiremic.app.core

import android.app.Application
import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.delay
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
    private val networkBinder = NetworkBinder(application) {
        // Reopen the socket on the new network. The device list is deliberately
        // left alone: entries expire on their own when a computer stops
        // answering, and emptying the list here would blank the screen every
        // time a VPN reconnected, which is exactly when someone is most likely
        // to be staring at it waiting for something to appear.
        if (started) NativeBridge.nativeRefreshDiscovery()
    }

    fun start() {
        if (started) return
        started = true

        acquireMulticastLock()
        networkBinder.bind()
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
        networkBinder.unbind()
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

    private val _manualProbeStatus = MutableStateFlow<String?>(null)
    val manualProbeStatus: StateFlow<String?> = _manualProbeStatus.asStateFlow()

    /**
     * Reaches a computer at a typed-in address instead of waiting for it to be
     * discovered. Some access points drop broadcast traffic between clients and
     * some firewalls drop the inbound datagram, and on those networks automatic
     * discovery cannot work no matter how patient the person is. The computer
     * shows its address on its own screen; typing it in gets past all of that.
     */
    fun connectToAddress(host: String) {
        val trimmed = host.trim()
        if (!IPV4_PATTERN.matches(trimmed)) {
            _manualProbeStatus.value = "That does not look like an IP address."
            return
        }

        _manualProbeStatus.value = "Looking for a computer at $trimmed…"
        _lastError.value = null

        viewModelScope.launch {
            val known = _devices.value.map { it.id }.toSet()
            var found = false

            // Several rounds: the first datagram can arrive while the computer
            // is still starting up, and a single miss should not be reported as
            // a wrong address.
            repeat(6) {
                if (found) return@repeat
                NativeBridge.nativeProbeHost(trimmed)
                delay(500)
                val fresh = _devices.value.firstOrNull { it.id !in known }
                if (fresh != null) {
                    found = true
                    _manualProbeStatus.value = "Found ${fresh.name}."
                    connect(fresh.id)
                }
            }

            if (!found) {
                _manualProbeStatus.value =
                    "No computer answered at $trimmed. Check that WireMic is open " +
                        "there and that both devices are on the same network."
            }
        }
    }

    fun clearManualProbeStatus() {
        _manualProbeStatus.value = null
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
        networkBinder.unbind()
        releaseMulticastLock()
    }

    private companion object {
        val IPV4_PATTERN =
            Regex("^((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)\\.){3}(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)$")
    }
}
