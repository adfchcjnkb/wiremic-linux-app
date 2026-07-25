package com.wiremic.app.core

interface NativeListener {
    fun onDeviceListChanged(devicesJson: String)
    fun onConnectionStateChanged(state: String)
    fun onConnectionEstablished(deviceJson: String)
    fun onConnectionClosed(reasonCode: String)
    fun onConnectionFailed(reason: String)
    fun onNativeError(message: String)
}

object NativeBridge {
    init {
        System.loadLibrary("wiremic_native")
    }

    external fun nativeSetListener(listener: NativeListener?)
    external fun nativeStart(
        deviceId: String,
        deviceName: String,
        deviceModel: String,
        dataDir: String
    ): Boolean
    external fun nativeStop()
    external fun nativeRequestConnection(deviceId: String)
    external fun nativeDisconnect()
    external fun nativeRefreshDiscovery()
    external fun nativeGetDevices(): String
}
