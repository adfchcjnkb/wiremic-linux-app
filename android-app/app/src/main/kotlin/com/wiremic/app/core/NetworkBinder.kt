package com.wiremic.app.core

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import java.net.NetworkInterface

/**
 * Keeps the app's sockets on the network that actually reaches the desktop.
 *
 * Pinning the whole process to the Wi-Fi network sounds like the safe default,
 * but it is wrong more often than it is right. It is only ever needed to escape
 * a VPN, which captures the default route and swallows LAN traffic. Without a
 * VPN the system routing table is already correct, and overriding it actively
 * breaks the wired cases: with USB tethering or a Wi-Fi hotspot the phone *is*
 * the router, the desktop sits on a local interface that is not a [Network] at
 * all, and forcing traffic onto Wi-Fi or mobile data sends it the wrong way.
 *
 * So the rule is: bind only while a VPN is up, and never while the phone is
 * sharing its own connection.
 */
class NetworkBinder(
    context: Context,
    /**
     * Called whenever the network the process is pinned to actually changes.
     *
     * Binding the process only affects sockets opened afterwards, so discovery's
     * socket -- opened when the app started -- keeps whatever network it was
     * created on. When a VPN comes up or goes down under a running app, that is
     * a socket bound to a network that may no longer reach anything, and no
     * amount of waiting fixes it. It has to be reopened.
     */
    private val onBoundNetworkChanged: () -> Unit = {}
) {
    private val connectivityManager =
        context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE)
            as ConnectivityManager

    private var lanCallback: ConnectivityManager.NetworkCallback? = null
    private var vpnCallback: ConnectivityManager.NetworkCallback? = null

    private val lock = Any()
    private var lanNetwork: Network? = null
    private var vpnCount = 0
    private var boundNetwork: Network? = null

    fun bind() {
        if (lanCallback != null) return

        val lanRequest = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET)
            .build()

        val lan = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                synchronized(lock) { lanNetwork = network }
                reconcile()
            }

            override fun onLost(network: Network) {
                synchronized(lock) {
                    if (lanNetwork == network) lanNetwork = null
                }
                reconcile()
            }
        }

        // A VPN network is filtered out of a default request, so ask for it
        // explicitly rather than inferring one from the absence of a route.
        val vpnRequest = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_VPN)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
            .build()

        val vpn = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                synchronized(lock) { vpnCount++ }
                reconcile()
            }

            override fun onLost(network: Network) {
                synchronized(lock) { if (vpnCount > 0) vpnCount-- }
                reconcile()
            }
        }

        runCatching {
            connectivityManager.registerNetworkCallback(lanRequest, lan)
            lanCallback = lan
        }
        runCatching {
            connectivityManager.registerNetworkCallback(vpnRequest, vpn)
            vpnCallback = vpn
        }

        reconcile()
    }

    fun unbind() {
        lanCallback?.let { active ->
            runCatching { connectivityManager.unregisterNetworkCallback(active) }
        }
        vpnCallback?.let { active ->
            runCatching { connectivityManager.unregisterNetworkCallback(active) }
        }
        lanCallback = null
        vpnCallback = null

        synchronized(lock) {
            lanNetwork = null
            vpnCount = 0
            boundNetwork = null
        }
        runCatching { connectivityManager.bindProcessToNetwork(null) }
    }

    /** True while the phone is sharing its connection over USB or as a hotspot. */
    private fun isSharingConnection(): Boolean = runCatching {
        NetworkInterface.getNetworkInterfaces().toList().any { candidate ->
            if (!candidate.isUp || candidate.isLoopback) return@any false
            val name = candidate.name.lowercase()
            val shared = name.startsWith("rndis") || name.startsWith("ncm") ||
                name.startsWith("usb") || name.startsWith("ap") ||
                name.startsWith("swlan") || name.startsWith("wlan1")
            shared && candidate.inetAddresses.toList().any { address ->
                !address.isLoopbackAddress && address.address.size == 4
            }
        }
    }.getOrDefault(false)

    private fun reconcile() {
        // Enumerating interfaces touches the filesystem, so it stays outside
        // the lock.
        val sharing = isSharingConnection()

        val desired = synchronized(lock) {
            val want = if (vpnCount > 0 && !sharing) lanNetwork else null
            if (want == boundNetwork) return
            boundNetwork = want
            want
        }
        runCatching { connectivityManager.bindProcessToNetwork(desired) }
        runCatching { onBoundNetworkChanged() }
    }
}
