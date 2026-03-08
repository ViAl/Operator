package com.example.camerax.core.model.pipeline

import com.example.camerax.core.model.capture.FrameMetadata
import com.example.camerax.core.model.capture.MotionEstimate

/** 
 * Указатель на ImageProxy / AHardwareBuffer со счетчиком ссылок.
 * MVP Simplification: Обертка скрывает реальный инстанс android.media.Image. 
 */
interface ZslFrameRef : AutoCloseable {
    val metadata: FrameMetadata
    val hardwareBufferPtr: Long? // null если не поддерживается (используется fallback)
    val width: Int
    val height: Int
    
    // Временный метод для извлечения сырого ImageProxy (для сохранения)
    fun getRawImageProxy(): Any? 
}

data class BurstRequest(
    val targetTimestampNs: Long,
    val frameCount: Int
)

data class BurstFrameSet(
    val baseFrame: ZslFrameRef,
    val alternateFrames: List<ZslFrameRef>,
    val motionEstimate: MotionEstimate
) : AutoCloseable {
    override fun close() {
        baseFrame.close()
        alternateFrames.forEach { it.close() }
    }
}

data class NativeProcessRequest(
    val burstSet: BurstFrameSet,
    val targetTimestampNs: Long,
    val toneMappingEnabled: Boolean
)

sealed interface NativeProcessResult {
    data class Success(val outFrame: ZslFrameRef) : NativeProcessResult
    data class Fallback(val reason: String, val fallbackFrame: ZslFrameRef) : NativeProcessResult
    data class Error(val throwable: Throwable) : NativeProcessResult
}

sealed interface SaveResult {
    data class Success(val uri: String) : SaveResult
    data class Error(val throwable: Throwable) : SaveResult
}
