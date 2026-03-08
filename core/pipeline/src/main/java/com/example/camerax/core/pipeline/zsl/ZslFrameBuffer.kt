package com.example.camerax.core.pipeline.zsl

import androidx.camera.core.ImageProxy
import com.example.camerax.core.model.capture.BurstFrameSet
import com.example.camerax.core.model.capture.BurstRequest

interface ZslFrameBuffer {
    fun pushFrame(image: ImageProxy)
    suspend fun extractBurst(request: BurstRequest): BurstFrameSet
    fun clear()
}
