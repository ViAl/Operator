package com.example.camerax.core.camera.provider

import androidx.camera.core.CameraSelector
import com.example.camerax.core.model.capture.CameraCapabilities
import com.example.camerax.core.model.capture.CameraDeviceId
import com.example.camerax.core.model.capture.CameraLensFacing
import com.example.camerax.core.model.capture.Resolution
import javax.inject.Inject

class DeviceCapabilityRepositoryImpl @Inject constructor() : DeviceCapabilityRepository {
    
    override fun getCapabilities(selector: CameraSelector): CameraCapabilities {
        // MVP: Provide pragmatic stub capabilities
        val facing = if (selector == CameraSelector.DEFAULT_FRONT_CAMERA) {
            CameraLensFacing.FRONT
        } else {
            CameraLensFacing.BACK
        }
        
        return CameraCapabilities(
            deviceId = CameraDeviceId("mvp-stub-${facing.name.lowercase()}"),
            lensFacing = facing,
            isZslSupported = true, // We emulate ZSL in software for MVP
            supportedResolutions = listOf(
                Resolution(1920, 1080),
                Resolution(1280, 720)
            )
        )
    }
}
