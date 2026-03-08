package com.example.camerax.feature.viewfinder.viewmodel

import android.os.SystemClock
import androidx.camera.view.PreviewView
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.camerax.core.camera.session.CameraSessionController
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.pipeline.CameraSessionState
import com.example.camerax.core.model.pipeline.CapturePipelineState
import com.example.camerax.core.model.pipeline.SaveResult
import com.example.camerax.core.pipeline.orchestrator.PhotoPipelineOrchestrator
import com.example.camerax.core.pipeline.zsl.ZslFrameBuffer
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class ViewfinderViewModel @Inject constructor(
    private val sessionController: CameraSessionController,
    private val orchestrator: PhotoPipelineOrchestrator,
    private val frameBuffer: ZslFrameBuffer, // Only for pushFrame from analyzer
    private val logger: Logger
) : ViewModel() {

    val sessionState: StateFlow<CameraSessionState> = sessionController.state
    val captureState: StateFlow<CapturePipelineState> = orchestrator.state

    private val _lastSavedUri = MutableStateFlow<String?>(null)
    val lastSavedUri: StateFlow<String?> = _lastSavedUri.asStateFlow()

    fun bindCamera(lifecycleOwner: LifecycleOwner, previewView: PreviewView) {
        logger.d("ViewfinderViewModel", "Binding camera to lifecycle")
        sessionController.bindToLifecycle(lifecycleOwner, previewView) { proxy ->
            // High frequency analyzer callback -> push to ZSL Buffer
            frameBuffer.pushFrame(proxy)
        }
    }

    fun unbindCamera() {
        sessionController.unbind()
        frameBuffer.clear()
        logger.d("ViewfinderViewModel", "Camera unbound and buffer cleared")
    }

    fun onShutterClicked() {
        if (captureState.value == CapturePipelineState.AcquiringFrames || 
            captureState.value == CapturePipelineState.Processing || 
            captureState.value == CapturePipelineState.Saving) {
            logger.d("ViewfinderViewModel", "Ignoring shutter: already capturing")
            return
        }

        // CameraX images typically use SystemClock.elapsedRealtimeNanos() instead of System.nanoTime()
        val targetTimestamp = SystemClock.elapsedRealtimeNanos() 
        
        viewModelScope.launch {
            try {
                val result = orchestrator.capturePhoto(targetTimestamp)
                if (result is SaveResult.Success) {
                    _lastSavedUri.value = result.uriString
                }
            } catch (e: Exception) {
                logger.e("ViewfinderViewModel", "Capture failed from UI layer", e)
            }
        }
    }

    override fun onCleared() {
        unbindCamera()
        super.onCleared()
    }
}
