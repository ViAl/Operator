package com.example.camerax.core.model.telemetry

import com.example.camerax.core.model.capture.CameraCapabilities

sealed interface CameraSessionState {
    object Idle : CameraSessionState
    object Opening : CameraSessionState
    object Configuring : CameraSessionState
    data class Previewing(val capabilities: CameraCapabilities) : CameraSessionState
    data class Error(val error: PipelineError) : CameraSessionState
    object Closed : CameraSessionState
}

sealed interface CapturePipelineState {
    object Idle : CapturePipelineState
    data class ShutterPressed(val captureTimestampNs: Long) : CapturePipelineState
    data class ExtractingFrames(val count: Int) : CapturePipelineState
    object NativeProcessing : CapturePipelineState
    object Rendering : CapturePipelineState
    object Saving : CapturePipelineState
    data class Completed(val uri: String) : CapturePipelineState
    data class Failed(val error: PipelineError) : CapturePipelineState
}

sealed class PipelineError(msg: String, cause: Throwable? = null) : RuntimeException(msg, cause) {
    class CameraAccessDenied : PipelineError("Camera permission denied")
    class CameraDisconnected : PipelineError("Camera disconnected")
    class ExtractionFailed(msg: String) : PipelineError("Failed to extract from buffer: $msg")
    class ProcessingFailure(cause: Throwable) : PipelineError("Native processing failed", cause)
    class SaveFailure(cause: Throwable) : PipelineError("Failed to save media", cause)
}

data class PipelineStageTiming(
    val name: String,
    val durationMs: Long
)

data class PipelineTelemetry(
    val sessionStartTs: Long,
    val captureRequestTs: Long,
    val zslExtractionTimeMs: Long,
    val nativeFusionTimeMs: Long,
    val saveTimeMs: Long,
    val totalTimeMs: Long,
    val isNativeFallback: Boolean,
    val droppedFramesCount: Int
)
