package com.example.camerax.core.camera.session

import androidx.camera.core.ImageProxy
import androidx.camera.view.PreviewView
import androidx.lifecycle.LifecycleOwner
import com.example.camerax.core.model.capture.CameraCapabilities
import com.example.camerax.core.model.pipeline.CameraSessionState
import kotlinx.coroutines.flow.StateFlow

interface CameraSessionController {
    val state: StateFlow<CameraSessionState>
    val currentCapabilities: StateFlow<CameraCapabilities?>

    /**
     * Binds preview, analysis, and image capture to lifecycle.
     * @param onImageAvailable Callback to inject ImageProxy frames into ZSL buffer.
     */
    fun bindToLifecycle(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView,
        onImageAvailable: (ImageProxy) -> Unit
    )

    /**
     * Triggers a full-resolution still capture.
     * Returns JPEG bytes (already includes EXIF rotation metadata — no manual rotation needed).
     */
    suspend fun capturePhoto(): ByteArray

    fun unbind()
}
