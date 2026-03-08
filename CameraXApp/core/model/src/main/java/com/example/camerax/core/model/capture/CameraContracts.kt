package com.example.camerax.core.model.capture

@JvmInline
value class CameraDeviceId(val id: String)

enum class CameraLensFacing { FRONT, BACK, EXTERNAL }

data class CameraCapabilities(
    val deviceId: CameraDeviceId,
    val lensFacing: CameraLensFacing,
    val isZslSupported: Boolean,
    val isOisSupported: Boolean,
    val opticalBlackRegions: IntArray?
) {
    // MVP simplification: Auto-generated equals and hashcode
}

data class FrameMetadata(
    val timestampNs: Long,
    val exposureTimeNs: Long,
    val iso: Int,
    val focusDistance: Float,
    val lensAperture: Float
)

data class MotionEstimate(
    val globalMotionVectorX: Float,
    val globalMotionVectorY: Float,
    val motionBlurProbability: Float
)
