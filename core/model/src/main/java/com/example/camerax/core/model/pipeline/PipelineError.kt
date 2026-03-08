package com.example.camerax.core.model.pipeline

sealed class PipelineError(message: String, cause: Throwable? = null) : Exception(message, cause) {
    class CameraNotReady : PipelineError("Camera session is not active")
    class NoFramesAvailable(msg: String = "Not enough frames in ZSL buffer") : PipelineError(msg)
    class AcquisitionTimeout : PipelineError("Frame acquisition timed out")
    class NativeProcessingFailed(msg: String, cause: Throwable? = null) : PipelineError(msg, cause)
    class SaveFailed(cause: Throwable) : PipelineError("Save failed", cause)
}
