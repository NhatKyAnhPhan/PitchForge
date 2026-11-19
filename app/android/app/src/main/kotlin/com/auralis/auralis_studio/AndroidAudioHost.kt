package com.auralis.auralis_studio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper

/**
 * Android framework boundary for microphone permission, audio focus and route
 * discovery. Audio samples never cross this Kotlin/Flutter boundary.
 */
internal object AndroidAudioHost {
    @Volatile
    private var appContext: Context? = null

    @Volatile
    private var foreground = false

    @Volatile
    private var wantsAudioFocus = false

    @Volatile
    private var hasAudioFocus = false

    private var audioManager: AudioManager? = null
    private var focusRequest: AudioFocusRequest? = null

    private val mainHandler = Handler(Looper.getMainLooper())
    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                hasAudioFocus = true
                publishState()
            }

            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                hasAudioFocus = false
                publishState()
            }

            AudioManager.AUDIOFOCUS_LOSS -> {
                hasAudioFocus = false
                wantsAudioFocus = false
                publishState()
            }
        }
    }

    @JvmStatic
    fun initialize(context: Context) {
        appContext = context.applicationContext
        audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    }

    @JvmStatic
    fun hasRecordPermission(): Boolean {
        val context = appContext ?: return false
        return context.checkPermission(
            Manifest.permission.RECORD_AUDIO,
            android.os.Process.myPid(),
            android.os.Process.myUid(),
        ) == PackageManager.PERMISSION_GRANTED
    }

    @JvmStatic
    fun requestAudioFocus(): Boolean {
        val manager = audioManager ?: return false
        if (!foreground) return false
        if (hasAudioFocus) return true
        wantsAudioFocus = true

        val result = try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                            .build(),
                    )
                    .setWillPauseWhenDucked(true)
                    .setOnAudioFocusChangeListener(focusListener, mainHandler)
                    .build()
                focusRequest = request
                manager.requestAudioFocus(request)
            } else {
                @Suppress("DEPRECATION")
                manager.requestAudioFocus(
                    focusListener,
                    AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN,
                )
            }
        } catch (_: RuntimeException) {
            AudioManager.AUDIOFOCUS_REQUEST_FAILED
        }
        hasAudioFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        publishState()
        return hasAudioFocus
    }

    @JvmStatic
    fun abandonAudioFocus() {
        wantsAudioFocus = false
        abandonFocusRequest()
        hasAudioFocus = false
        publishState()
    }

    @JvmStatic
    fun deviceSnapshot(forInput: Boolean): Array<String> {
        val manager = audioManager ?: return emptyArray()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return emptyArray()
        val direction = if (forInput) {
            AudioManager.GET_DEVICES_INPUTS
        } else {
            AudioManager.GET_DEVICES_OUTPUTS
        }
        return manager.getDevices(direction).map { device ->
            val channels = device.channelCounts.maxOrNull() ?: 0
            val isDefault = if (forInput) {
                device.type == AudioDeviceInfo.TYPE_BUILTIN_MIC
            } else {
                device.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
            }
            val name = device.productName.toString()
                .replace('\t', ' ')
                .replace('\n', ' ')
                .replace('\r', ' ')
            "${device.id}\t$channels\t${if (isDefault) 1 else 0}\t$name"
        }.toTypedArray()
    }

    fun onActivityResumed() {
        foreground = true
        if (wantsAudioFocus) {
            requestAudioFocus()
        } else {
            publishState()
        }
    }

    fun onActivityPaused() {
        foreground = false
        hasAudioFocus = false
        publishState()
        // Keep wantsAudioFocus so a running session can request focus again on resume.
        abandonFocusRequest()
    }

    private fun abandonFocusRequest() {
        val manager = audioManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            focusRequest?.let(manager::abandonAudioFocusRequest)
        } else {
            @Suppress("DEPRECATION")
            manager.abandonAudioFocus(focusListener)
        }
    }

    private fun publishState() {
        MainActivity.publishHostState(foreground, hasAudioFocus)
    }
}
