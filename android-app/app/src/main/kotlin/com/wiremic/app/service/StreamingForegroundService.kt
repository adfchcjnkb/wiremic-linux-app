package com.wiremic.app.service

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Color
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.media.app.NotificationCompat.MediaStyle
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat
import com.wiremic.app.MainActivity
import com.wiremic.app.R
import com.wiremic.app.WireMicApplication
import com.wiremic.app.core.NativeBridge

class StreamingForegroundService : Service() {

    private var mediaSession: MediaSessionCompat? = null

    override fun onCreate() {
        super.onCreate()
        mediaSession = MediaSessionCompat(this, "WireMicSession").apply {
            setCallback(object : MediaSessionCompat.Callback() {
                override fun onStop() {
                    handleStopAction()
                }
                override fun onPause() {
                    handleStopAction()
                }
            })
            isActive = true
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP_STREAMING) {
            handleStopAction()
            return START_NOT_STICKY
        }

        val deviceName = intent?.getStringExtra(EXTRA_DEVICE_NAME) ?: "computer"

        mediaSession?.setPlaybackState(
            PlaybackStateCompat.Builder()
                .setActions(PlaybackStateCompat.ACTION_STOP or PlaybackStateCompat.ACTION_PAUSE)
                .setState(PlaybackStateCompat.STATE_PLAYING, 0, 1f)
                .build()
        )

        if (!startForegroundCompat(deviceName)) {
            // Android 12 and later can refuse a foreground start outright, and
            // Android 14 rejects a microphone service whose permission was
            // revoked. Stopping cleanly beats being killed by the system.
            NativeBridge.nativeDisconnect()
            stopSelf()
            return START_NOT_STICKY
        }
        return START_STICKY
    }

    private fun startForegroundCompat(deviceName: String): Boolean {
        return try {
            val notification = buildNotification(deviceName)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
                )
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
            true
        } catch (t: Throwable) {
            Log.e(TAG, "could not start the streaming notification", t)
            false
        }
    }

    private fun handleStopAction() {
        NativeBridge.nativeDisconnect()
        mediaSession?.setPlaybackState(
            PlaybackStateCompat.Builder()
                .setState(PlaybackStateCompat.STATE_STOPPED, 0, 0f)
                .build()
        )
        stopForegroundCompat()
        stopSelf()
    }

    override fun onDestroy() {
        mediaSession?.isActive = false
        mediaSession?.release()
        mediaSession = null
        super.onDestroy()
    }

    private fun stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
    }

    override fun onBind(intent: Intent?) = null

    private fun buildNotification(deviceName: String): Notification {
        val contentIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val stopIntent = Intent(this, StreamingForegroundService::class.java).apply {
            action = ACTION_STOP_STREAMING
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 0, stopIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, WireMicApplication.CHANNEL_ID)
            .setContentTitle(getString(R.string.streaming_notification_title))
            .setContentText(getString(R.string.streaming_notification_text, deviceName))
            .setSmallIcon(R.drawable.ic_stat_mic)
            .setColor(Color.parseColor("#7C93FF"))
            .setColorized(true)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setShowWhen(false)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .addAction(R.drawable.ic_stat_stop, getString(R.string.stop_streaming_action), stopPendingIntent)
            .setStyle(
                MediaStyle()
                    .setMediaSession(mediaSession?.sessionToken)
                    .setShowActionsInCompactView(0)
            )
            .build()
    }

    companion object {
        private const val TAG = "WireMicService"
        const val EXTRA_DEVICE_NAME = "device_name"
        const val ACTION_STOP_STREAMING = "com.wiremic.app.action.STOP_STREAMING"
        private const val NOTIFICATION_ID = 4201
    }
}
