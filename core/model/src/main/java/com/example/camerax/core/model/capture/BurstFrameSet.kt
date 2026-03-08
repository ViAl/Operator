package com.example.camerax.core.model.capture

/**
 * MVP Simplification: Using raw NV21 byte array as temporary payload wrapper.
 * Production: Should hold AHardwareBuffer or ImageProxy ref with proper lifecycle.
 */
data class BurstFrameSet(
    val frames: List<Frame>
) {
    data class Frame(
        val metadata: FrameMetadata,
        val width: Int,
        val height: Int,
        val nv21Data: ByteArray
    )
}
