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
     * Binds preview and analysis to lifecycle.
     * @param onImageAvailable Callback out to UI/Orchestrator layers to inject into ZSL Buffer.
     */
    fun bindToLifecycle(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView,
        onImageAvailable: (ImageProxy) -> Unit
    )

    fun unbind()
}
