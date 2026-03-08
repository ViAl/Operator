package com.example.camerax.core.model.capture

data class MotionEstimate(
    val deltaX: Float,
    val deltaY: Float,
    val confidence: Float
) {
    companion object {
        val STUB = MotionEstimate(0f, 0f, 1.0f)
    }
}
