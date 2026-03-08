package com.example.camerax.core.camera.session

import android.content.Context
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
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
import kotlinx.coroutines.suspendCancellableCoroutine
import java.util.concurrent.Executor
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

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
    private var imageCapture: ImageCapture? = null

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

        provider.unbindAll()

        val preview = useCaseBinder.buildPreview().also {
            it.setSurfaceProvider(previewView.surfaceProvider)
        }
        val analyzer = useCaseBinder.buildImageAnalysis(executor).also {
            it.setAnalyzer(executor) { proxy ->
                onImageAvailable(proxy)
            }
        }
        val capture = useCaseBinder.buildImageCapture().also {
            imageCapture = it
        }
        val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA

        try {
            camera = provider.bindToLifecycle(
                lifecycleOwner,
                cameraSelector,
                preview,
                analyzer,
                capture
            )
            val capability = capabilityRepository.getCapabilities(cameraSelector)
            _currentCapabilities.value = capability
            _state.value = CameraSessionState.Active
            logger.d("CameraSession", "Camera bound successfully with Preview + Analyzer + Capture")
        } catch (e: Exception) {
            logger.e("CameraSession", "Use case binding failed", e)
            _state.value = CameraSessionState.Error(e)
        }
    }

    override suspend fun capturePhoto(): ByteArray {
        val capture = imageCapture
            ?: throw IllegalStateException("ImageCapture not initialized — call bindToLifecycle first")

        val executor = ContextCompat.getMainExecutor(context)

        return suspendCancellableCoroutine { continuation ->
            capture.takePicture(executor, object : ImageCapture.OnImageCapturedCallback() {
                override fun onCaptureSuccess(image: ImageProxy) {
                    try {
                        // Extract JPEG bytes from the ImageProxy
                        val buffer = image.planes[0].buffer
                        val bytes = ByteArray(buffer.remaining())
                        buffer.get(bytes)
                        image.close()
                        logger.d("CameraSession", "ImageCapture success: ${bytes.size} bytes, " +
                                "rotation=${image.imageInfo.rotationDegrees}")
                        continuation.resume(bytes)
                    } catch (e: Exception) {
                        image.close()
                        continuation.resumeWithException(e)
                    }
                }

                override fun onError(exception: ImageCaptureException) {
                    logger.e("CameraSession", "ImageCapture failed", exception)
                    continuation.resumeWithException(exception)
                }
            })
        }
    }

    override fun unbind() {
        cameraProvider?.unbindAll()
        imageCapture = null
        camera = null
        _state.value = CameraSessionState.Closed
    }
}
