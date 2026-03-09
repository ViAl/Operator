package com.example.camerax.core.model.capture

data class FrameMetadata(
    val timestampNs: Long,
    val exposureTimeNs: Long,
    val iso: Int,
    val lensPosition: Float,
    val rotationDegrees: Int = 0,
    val exposureCompensationIndex: Int = 0,
    val exposureClass: ExposureClass = ExposureClass.NORMAL
)
