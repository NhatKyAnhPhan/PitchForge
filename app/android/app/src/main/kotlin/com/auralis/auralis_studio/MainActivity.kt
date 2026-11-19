package com.auralis.auralis_studio

import android.Manifest
import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private var permissionResult: MethodChannel.Result? = null
    private var audioChannel: MethodChannel? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        AndroidAudioHost.initialize(applicationContext)
        super.onCreate(savedInstanceState)
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        audioChannel = MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            AUDIO_CHANNEL,
        ).also { channel ->
            channel.setMethodCallHandler { call, result ->
                when (call.method) {
                    "requestMicrophonePermission" -> requestMicrophonePermission(result)
                    "requestAudioFocus" -> result.success(
                        status(if (AndroidAudioHost.requestAudioFocus()) "granted" else "denied"),
                    )
                    "abandonAudioFocus" -> {
                        AndroidAudioHost.abandonAudioFocus()
                        result.success(status("abandoned"))
                    }
                    else -> result.notImplemented()
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        AndroidAudioHost.onActivityResumed()
    }

    override fun onPause() {
        AndroidAudioHost.onActivityPaused()
        super.onPause()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == RECORD_AUDIO_REQUEST) {
            permissionResult?.success(
                status(if (AndroidAudioHost.hasRecordPermission()) "granted" else "denied"),
            )
            permissionResult = null
        }
    }

    override fun onDestroy() {
        permissionResult?.success(status("activity_destroyed"))
        permissionResult = null
        audioChannel?.setMethodCallHandler(null)
        super.onDestroy()
    }

    private fun requestMicrophonePermission(result: MethodChannel.Result) {
        if (AndroidAudioHost.hasRecordPermission()) {
            result.success(status("granted"))
            return
        }
        if (permissionResult != null) {
            result.success(status("request_in_progress"))
            return
        }
        permissionResult = result
        try {
            requestPermissions(
                arrayOf(Manifest.permission.RECORD_AUDIO),
                RECORD_AUDIO_REQUEST,
            )
        } catch (_: RuntimeException) {
            permissionResult = null
            result.success(status("unavailable"))
        }
    }

    private fun status(value: String): Map<String, String> = mapOf("status" to value)

    companion object {
        private const val AUDIO_CHANNEL = "com.auralis.audio/lifecycle"
        private const val RECORD_AUDIO_REQUEST = 4107

        init {
            System.loadLibrary("auralis_audio_engine")
        }

        @JvmStatic
        private external fun nativeSetHostState(isForeground: Boolean, hasAudioFocus: Boolean)

        @JvmStatic
        internal fun publishHostState(isForeground: Boolean, hasAudioFocus: Boolean) {
            nativeSetHostState(isForeground, hasAudioFocus)
        }
    }
}
