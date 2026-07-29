package com.wiremic.app.core

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest

class NetworkBinder(context: Context) {
    private val connectivityManager =
        context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE)
            as ConnectivityManager
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var boundNetwork: Network? = null
    fun bind() {
        if (callback != null) return

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET)
            .build()
        val networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                runCatching {
                    connectivityManager.bindProcessToNetwork(network)
                    boundNetwork = network
                }
            }
            override fun onLost(network: Network) {
                if (network != boundNetwork) return
                runCatching { connectivityManager.bindProcessToNetwork(null) }
                boundNetwork = null
            }
        }
        runCatching {
            connectivityManager.registerNetworkCallback(request, networkCallback)
            callback = networkCallback
        }
    }
    fun unbind() {
        callback?.let { active ->
            runCatching { connectivityManager.unregisterNetworkCallback(active) }
        }
        callback = null
        runCatching { connectivityManager.bindProcessToNetwork(null) }
        boundNetwork = null
    }
}
