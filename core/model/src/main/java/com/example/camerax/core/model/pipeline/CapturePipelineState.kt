package com.example.camerax.core.model.pipeline

sealed interface CapturePipelineState {
    object Idle : CapturePipelineState
    object AcquiringFrames : CapturePipelineState
    object Processing : CapturePipelineState
    object Saving : CapturePipelineState
    data class Completed(val saveResult: SaveResult.Success) : CapturePipelineState
    data class Failed(val error: PipelineError) : CapturePipelineState
}
