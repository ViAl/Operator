package com.example.camerax.core.model.telemetry

data class PipelineTelemetry(
    val captureId: String,
    val stages: List<PipelineStageTiming>,
    val totalDurationMs: Long,
    val frameCount: Int,
    val droppedFrames: Int,
    val backendUsed: String, // e.g. "NATIVE_FUSION" or "MVP_FALLBACK"
    val success: Boolean,
    val errorMessage: String? = null
)
