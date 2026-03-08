package com.example.camerax.core.pipeline.orchestrator

import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.pipeline.CapturePipelineState
import com.example.camerax.core.model.pipeline.SaveResult
import com.example.camerax.core.model.pipeline.PipelineError
import com.example.camerax.core.model.telemetry.PipelineStageTiming
import com.example.camerax.core.model.telemetry.PipelineTelemetry
import com.example.camerax.core.pipeline.exporter.ImageSaver
import com.example.camerax.core.pipeline.telemetry.PipelineTelemetryLogger
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.util.UUID
import javax.inject.Inject

class PhotoPipelineOrchestrator @Inject constructor(
    private val imageSaver: ImageSaver,
    private val telemetryLogger: PipelineTelemetryLogger,
    private val dispatchers: AppDispatchers,
    private val logger: Logger
) {
    private val _state = MutableStateFlow<CapturePipelineState>(CapturePipelineState.Idle)
    val state: StateFlow<CapturePipelineState> = _state.asStateFlow()

    /**
     * Primary capture path: receives full-resolution JPEG bytes from ImageCapture (via ViewModel),
     * saves directly to MediaStore. No NV21 conversion, no quality loss.
     */
    suspend fun capturePhoto(jpegBytes: ByteArray): SaveResult = withContext(dispatchers.default) {
        val captureId = UUID.randomUUID().toString()
        val startTimeMs = System.currentTimeMillis()
        val stages = mutableListOf<PipelineStageTiming>()

        try {
            _state.value = CapturePipelineState.Saving
            val stageStart = System.currentTimeMillis()
            val saveResult = imageSaver.saveJpeg(jpegBytes)
            stages.add(PipelineStageTiming("Save", System.currentTimeMillis() - stageStart))

            when (saveResult) {
                is SaveResult.Success -> {
                    _state.value = CapturePipelineState.Completed(saveResult)
                    logTelemetry(captureId, stages, startTimeMs, true, 1, null)
                    saveResult
                }
                is SaveResult.Error -> {
                    val error = PipelineError.SaveFailed(saveResult.exception)
                    _state.value = CapturePipelineState.Failed(error)
                    logTelemetry(captureId, stages, startTimeMs, false, 1, error.message)
                    throw error
                }
            }
        } catch (e: Exception) {
            val error = if (e is PipelineError) e else PipelineError.NativeProcessingFailed("Pipeline crash", e)
            _state.value = CapturePipelineState.Failed(error)
            logger.e("Orchestrator", "Pipeline failed", error)
            logTelemetry(captureId, stages, startTimeMs, false, 0, error.message)
            SaveResult.Error(error)
        }
    }

    private fun logTelemetry(id: String, stages: List<PipelineStageTiming>, start: Long, success: Boolean, frames: Int, errorMsg: String?) {
        val duration = System.currentTimeMillis() - start
        telemetryLogger.logCapture(
            PipelineTelemetry(
                captureId = id,
                stages = stages,
                totalDurationMs = duration,
                frameCount = frames,
                droppedFrames = 0,
                backendUsed = "IMAGE_CAPTURE_FULL_RES",
                success = success,
                errorMessage = errorMsg
            )
        )
    }
}
