package com.example.camerax.core.camera.provider

import androidx.camera.core.CameraSelector
import com.example.camerax.core.model.capture.CameraCapabilities

interface DeviceCapabilityRepository {
    fun getCapabilities(selector: CameraSelector): CameraCapabilities
}
