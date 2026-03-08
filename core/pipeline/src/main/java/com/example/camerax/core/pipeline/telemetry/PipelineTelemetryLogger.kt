package com.example.camerax.core.pipeline.telemetry

import com.example.camerax.core.model.telemetry.PipelineTelemetry

interface PipelineTelemetryLogger {
    fun logCapture(telemetry: PipelineTelemetry)
}
