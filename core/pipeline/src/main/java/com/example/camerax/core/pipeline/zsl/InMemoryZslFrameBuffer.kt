package com.example.camerax.core.pipeline.zsl

import androidx.camera.core.ImageProxy
import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.capture.BurstFrameSet
import com.example.camerax.core.model.capture.BurstRequest
import com.example.camerax.core.model.capture.ExposureClass
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

    private val capacity = 8
    private val buffer = LinkedList<BurstFrameSet.Frame>()
    private val bufferLock = Object()

    override fun pushFrame(image: ImageProxy) {
        val frameSnapshot = try {
            mapper.mapToFrameInfo(image)
        } catch (e: Exception) {
            logger.e("ZSL", "Failed to map base frame", e)
            image.close()
            return
        }

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

            val centerIndex = buffer.indexOfMinBy { abs(it.metadata.timestampNs - request.targetTimestampNs) }
                ?: throw PipelineError.NoFramesAvailable("Buffer is unexpectedly empty")

            val startIndex = maxOf(0, centerIndex - request.framesNum / 2)
            val endIndex = minOf(buffer.size - 1, startIndex + request.framesNum - 1)

            val framesRange = buffer.subList(startIndex, endIndex + 1).toList()
            val shortIdx = framesRange.indices.minByOrNull { estimateLumaMean(framesRange[it]) }

            val hdrAnnotated = framesRange.mapIndexed { idx, frame ->
                val exposureClass = if (shortIdx != null && idx == shortIdx) ExposureClass.SHORT else ExposureClass.NORMAL
                val exposureTimeNs = if (frame.metadata.exposureTimeNs > 0) {
                    frame.metadata.exposureTimeNs
                } else {
                    if (exposureClass == ExposureClass.SHORT) 4_000_000L else 10_000_000L
                }
                val iso = if (frame.metadata.iso > 0) frame.metadata.iso else 100

                frame.copy(
                    metadata = frame.metadata.copy(
                        exposureTimeNs = exposureTimeNs,
                        iso = iso,
                        exposureCompensationIndex = if (exposureClass == ExposureClass.SHORT) -1 else 0,
                        exposureClass = exposureClass
                    )
                )
            }

            logger.d(
                "ZSL",
                "Extracted burst=${hdrAnnotated.size}, centerTs=${request.targetTimestampNs}, shortIdx=${shortIdx ?: -1}"
            )

            return@withContext BurstFrameSet(hdrAnnotated)
        }
    }

    override fun clear() {
        synchronized(bufferLock) {
            buffer.clear()
        }
    }

    private fun estimateLumaMean(frame: BurstFrameSet.Frame): Float {
        val ySize = frame.width * frame.height
        if (ySize <= 0 || frame.nv21Data.isEmpty()) return 255f
        var sum = 0L
        for (i in 0 until minOf(ySize, frame.nv21Data.size)) {
            sum += frame.nv21Data[i].toInt() and 0xFF
        }
        return sum.toFloat() / ySize.toFloat()
    }

    private inline fun <T, R : Comparable<R>> Iterable<T>.indexOfMinBy(selector: (T) -> R): Int? {
        val iterator = iterator()
        if (!iterator.hasNext()) return null
        var minValue = selector(iterator.next())
        var minIndex = 0
        var i = 1
        while (iterator.hasNext()) {
            val v = selector(iterator.next())
            if (minValue > v) {
                minValue = v
                minIndex = i
            }
            i++
        }
        return minIndex
    }
}
