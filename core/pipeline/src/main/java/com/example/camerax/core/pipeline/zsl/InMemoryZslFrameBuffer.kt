package com.example.camerax.core.pipeline.zsl

import androidx.camera.core.ImageProxy
import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.capture.BurstFrameSet
import com.example.camerax.core.model.capture.BurstRequest
import com.example.camerax.core.model.pipeline.PipelineError
import kotlinx.coroutines.withContext
import java.util.LinkedList
import javax.inject.Inject
import kotlin.math.abs

class InMemoryZslFrameBuffer @Inject constructor(
    private val mapper: ImageProxyFrameMapper,
    private val dispatchers: AppDispatchers,
    private val logger: Logger
) : ZslFrameBuffer {

    private val capacity = 10
    
    // Buffer stores Frame instances (snapshots), NOT ImageProxy, 
    // to avoid leaking CameraX buffers and blocking the pipeline.
    private val buffer = LinkedList<BurstFrameSet.Frame>()
    private val bufferLock = Object()

    override fun pushFrame(image: ImageProxy) {
        val frameSnapshot = try {
            mapper.mapToFrameInfo(image)
        } catch (e: Exception) {
            logger.e("ZSL", "Failed to map base frame", e)
            image.close() // Important: close even on format mapping failure
            return
        }
        
        // Ensure proxy is closed immediately after mapping to prevent analyzer blocks
        image.close()

        synchronized(bufferLock) {
            if (buffer.size >= capacity) {
                buffer.removeFirst()
            }
            buffer.addLast(frameSnapshot)
        }
    }

    override suspend fun extractBurst(request: BurstRequest): BurstFrameSet = withContext(dispatchers.default) {
        synchronized(bufferLock) {
            if (buffer.size < request.framesNum) {
                logger.e("ZSL", "Not enough frames. Expected >= ${request.framesNum}, got ${buffer.size}")
                throw PipelineError.NoFramesAvailable("Buffer size: ${buffer.size}")
            }

            // Find closest index by target timestamp
            val centerIndex = buffer.indexOfMinBy { abs(it.metadata.timestampNs - request.targetTimestampNs) }
                ?: throw PipelineError.NoFramesAvailable("Buffer is unexpectedly empty")
                
            val startIndex = maxOf(0, centerIndex - request.framesNum / 2)
            val endIndex = minOf(buffer.size - 1, startIndex + request.framesNum - 1)
            
            // For BurstFrameSet, we can just copy the references to the Domain Objects
            val framesRange = buffer.subList(startIndex, endIndex + 1).toList()
            
            logger.d("ZSL", "Extracted burst of ${framesRange.size} frames centered around timestamp ${request.targetTimestampNs}")
            
            return@withContext BurstFrameSet(framesRange)
        }
    }

    override fun clear() {
        synchronized(bufferLock) {
            buffer.clear()
        }
    }

    private inline fun <T, R : Comparable<R>> Iterable<T>.indexOfMinBy(selector: (T) -> R): Int? {
        val iterator = iterator()
        if (!iterator.hasNext()) return null
        var minElem = iterator.next()
        var minValue = selector(minElem)
        var minIndex = 0
        var i = 1
        while (iterator.hasNext()) {
            val e = iterator.next()
            val v = selector(e)
            if (minValue > v) {
                minElem = e
                minValue = v
                minIndex = i
            }
            i++
        }
        return minIndex
    }
}
