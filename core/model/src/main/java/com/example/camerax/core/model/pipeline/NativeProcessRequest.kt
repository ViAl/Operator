package com.example.camerax.core.model.pipeline

import com.example.camerax.core.model.capture.BurstFrameSet

data class NativeProcessRequest(
    val burstSet: BurstFrameSet,
    val enhancementLevel: Int = 1
)
