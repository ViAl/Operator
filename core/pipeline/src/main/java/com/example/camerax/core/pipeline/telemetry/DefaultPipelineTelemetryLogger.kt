package com.example.camerax.core.pipeline.telemetry

import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.telemetry.PipelineTelemetry
import javax.inject.Inject

class DefaultPipelineTelemetryLogger @Inject constructor(
    private val logger: Logger
) : PipelineTelemetryLogger {

    override fun logCapture(telemetry: PipelineTelemetry) {
        logger.d("Telemetry", "Capture finished: success=${telemetry.success}, " +
                "duration=${telemetry.totalDurationMs}ms, frames=${telemetry.frameCount}, " +
                "stages=${telemetry.stages.joinToString { it.stageName }}")
    }
}
