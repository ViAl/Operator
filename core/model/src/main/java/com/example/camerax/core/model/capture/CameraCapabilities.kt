package com.example.camerax.core.model.capture

data class CameraCapabilities(
    val deviceId: CameraDeviceId,
    val lensFacing: CameraLensFacing,
    val isZslSupported: Boolean,
    val supportedResolutions: List<Resolution>
)
