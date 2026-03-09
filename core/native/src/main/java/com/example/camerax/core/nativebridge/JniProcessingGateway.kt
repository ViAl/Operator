package com.example.camerax.core.nativebridge

import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.capture.ExposureClass
import com.example.camerax.core.model.pipeline.NativeProcessRequest
import com.example.camerax.core.model.pipeline.NativeProcessResult
import kotlinx.coroutines.withContext
import javax.inject.Inject

class JniProcessingGateway @Inject constructor(
    private val dispatchers: AppDispatchers,
    private val logger: Logger
) : NativeProcessingGateway {

    init {
        try {
            System.loadLibrary("camera_engine")
            logger.d("JniProcessingGateway", "Loaded camera_engine library")
        } catch (e: UnsatisfiedLinkError) {
            logger.e("JniProcessingGateway", "Failed to load camera_engine", e)
        }
    }

    override suspend fun process(request: NativeProcessRequest): NativeProcessResult = withContext(dispatchers.default) {
        try {
            val framesList = request.burstSet.frames
            if (framesList.isEmpty()) {
                return@withContext NativeProcessResult.Error("Burst is empty")
            }

            val baseIndex = framesList.size / 2
            val baseFrame = framesList[baseIndex]

            val nv21Arrays = framesList.map { it.nv21Data }.toTypedArray()
            val exposureTimeNs = LongArray(framesList.size) { idx -> framesList[idx].metadata.exposureTimeNs }
            val isoValues = IntArray(framesList.size) { idx -> framesList[idx].metadata.iso }
            val exposureClass = IntArray(framesList.size) { idx ->
                if (framesList[idx].metadata.exposureClass == ExposureClass.SHORT) 1 else 0
            }

            val shortExposureIndex = framesList.indexOfFirst { it.metadata.exposureClass == ExposureClass.SHORT }
            val outBytes = ByteArray(baseFrame.nv21Data.size)

            val statusCode = nativeProcessMultiFrame(
                baseFrame.width,
                baseFrame.height,
                nv21Arrays,
                baseIndex,
                shortExposureIndex,
                exposureTimeNs,
                isoValues,
                exposureClass,
                outBytes
            )

            val status = NativeStatus.fromCode(statusCode)
            if (status == NativeStatus.SUCCESS) {
                logger.d(
                    "JniProcessingGateway",
                    "Multi-frame processing succeeded: frames=${framesList.size}, base=$baseIndex, short=$shortExposureIndex"
                )
                NativeProcessResult.Success(
                    baseFrame.width,
                    baseFrame.height,
                    outBytes,
                    baseFrame.metadata.rotationDegrees
                )
            } else {
                logger.e("JniProcessingGateway", "Native processing failed with code $statusCode")
                NativeProcessResult.Fallback(baseFrame, "Native returned error $statusCode")
            }
        } catch (e: Exception) {
            logger.e("JniProcessingGateway", "JNI call crashed", e)
            NativeProcessResult.Fallback(request.burstSet.frames.first(), "JNI Exception: ${e.message}")
        }
    }

    private external fun nativeProcessMultiFrame(
        width: Int,
        height: Int,
        inNv21Arrays: Array<ByteArray>,
        baseIndex: Int,
        shortExposureIndex: Int,
        exposureTimeNs: LongArray,
        isoValues: IntArray,
        exposureClass: IntArray,
        outNv21: ByteArray
    ): Int
}
