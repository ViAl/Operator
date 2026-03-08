package com.example.camerax.core.model.pipeline

import com.example.camerax.core.model.capture.BurstFrameSet

sealed interface NativeProcessResult {
    data class Success(
        val w: Int,
        val h: Int,
        val processedData: ByteArray
    ) : NativeProcessResult
    
    // Fallback path in case native fusion fails or is bypassed for MVP
    data class Fallback(
        val baseFrame: BurstFrameSet.Frame,
        val reason: String
    ) : NativeProcessResult

    data class Error(val reason: String, val cause: Throwable? = null) : NativeProcessResult
}
