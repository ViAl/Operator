package com.example.camerax.feature.viewfinder.viewmodel

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
    private val frameBuffer: ZslFrameBuffer,
    private val logger: Logger
) : ViewModel() {

    val sessionState: StateFlow<CameraSessionState> = sessionController.state
    val captureState: StateFlow<CapturePipelineState> = orchestrator.state

    private val _lastSavedUri = MutableStateFlow<String?>(null)
    val lastSavedUri: StateFlow<String?> = _lastSavedUri.asStateFlow()

    fun bindCamera(lifecycleOwner: LifecycleOwner, previewView: PreviewView) {
        logger.d("ViewfinderViewModel", "Binding camera to lifecycle")
        sessionController.bindToLifecycle(lifecycleOwner, previewView) { proxy ->
            frameBuffer.pushFrame(proxy)
        }
    }

    fun unbindCamera() {
        sessionController.unbind()
        frameBuffer.clear()
        logger.d("ViewfinderViewModel", "Camera unbound and buffer cleared")
    }

    fun onShutterClicked() {
        val current = captureState.value
        if (current == CapturePipelineState.AcquiringFrames ||
            current == CapturePipelineState.Processing ||
            current == CapturePipelineState.Saving) {
            logger.d("ViewfinderViewModel", "Ignoring shutter: already capturing")
            return
        }

        viewModelScope.launch {
            try {
                // Step 1: Trigger full-res ImageCapture (camera layer)
                val jpegBytes = sessionController.capturePhoto()
                logger.d("ViewfinderViewModel", "ImageCapture acquired: ${jpegBytes.size} bytes")

                // Step 2: Hand JPEG bytes to pipeline for saving
                val result = orchestrator.capturePhoto(jpegBytes)
                if (result is SaveResult.Success) {
                    _lastSavedUri.value = result.uriString
                }
            } catch (e: Exception) {
                logger.e("ViewfinderViewModel", "Capture failed", e)
            }
        }
    }

    override fun onCleared() {
        unbindCamera()
        super.onCleared()
    }
}
