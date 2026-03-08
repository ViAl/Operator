package com.example.camerax.core.camera.session

import android.content.Context
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.example.camerax.core.camera.provider.DeviceCapabilityRepository
import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.capture.CameraCapabilities
import com.example.camerax.core.model.pipeline.CameraSessionState
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.guava.await
import java.util.concurrent.Executor
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class CameraSessionControllerImpl @Inject constructor(
    @ApplicationContext private val context: Context,
    private val useCaseBinder: CameraUseCaseBinder,
    private val capabilityRepository: DeviceCapabilityRepository,
    private val dispatchers: AppDispatchers,
    private val logger: Logger
) : CameraSessionController {

    private val _state = MutableStateFlow<CameraSessionState>(CameraSessionState.Initializing)
    override val state: StateFlow<CameraSessionState> = _state.asStateFlow()

    private val _currentCapabilities = MutableStateFlow<CameraCapabilities?>(null)
    override val currentCapabilities: StateFlow<CameraCapabilities?> = _currentCapabilities.asStateFlow()

    private var cameraProvider: ProcessCameraProvider? = null
    private var camera: Camera? = null

    override fun bindToLifecycle(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView,
        onImageAvailable: (ImageProxy) -> Unit
    ) {
        _state.value = CameraSessionState.Starting
        val cameraProviderFuture = ProcessCameraProvider.getInstance(context)
        val executor: Executor = ContextCompat.getMainExecutor(context)

        cameraProviderFuture.addListener({
            try {
                cameraProvider = cameraProviderFuture.get()
                bindCameraUseCases(lifecycleOwner, previewView, executor, onImageAvailable)
            } catch (e: Exception) {
                logger.e("CameraSession", "Camera binding failed", e)
                _state.value = CameraSessionState.Error(e)
            }
        }, executor)
    }

    private fun bindCameraUseCases(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView,
        executor: Executor,
        onImageAvailable: (ImageProxy) -> Unit
    ) {
        val provider = cameraProvider ?: return
        
        // Unbind before rebinding
        provider.unbindAll()

        val preview = useCaseBinder.buildPreview().also {
            it.setSurfaceProvider(previewView.surfaceProvider)
        }
        val analyzer = useCaseBinder.buildImageAnalysis(executor).also {
            it.setAnalyzer(executor) { proxy ->
                onImageAvailable(proxy)
            }
        }
        val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA

        try {
            camera = provider.bindToLifecycle(
                lifecycleOwner,
                cameraSelector,
                preview,
                analyzer
            )
            val capability = capabilityRepository.getCapabilities(cameraSelector)
            _currentCapabilities.value = capability
            _state.value = CameraSessionState.Active
            logger.d("CameraSession", "Camera bound successfully")
        } catch (e: Exception) {
            logger.e("CameraSession", "Use case binding failed", e)
            _state.value = CameraSessionState.Error(e)
        }
    }

    override fun unbind() {
        cameraProvider?.unbindAll()
        camera = null
        _state.value = CameraSessionState.Closed
    }
}
