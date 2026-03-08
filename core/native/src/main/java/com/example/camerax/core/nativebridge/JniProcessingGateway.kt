package com.example.camerax.core.nativebridge

import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
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
            if (request.burstSet.frames.isEmpty()) {
                return@withContext NativeProcessResult.Error("Burst is empty")
            }

            // MVP: Pass-through the base frame to verify JNI wiring
            val baseFrame = request.burstSet.frames.first()
            val nv21Bytes = baseFrame.nv21Data
            
            // In a real fusion, we'd pass an array of byte arrays or AHardwareBuffer refs.
            // For MVP, we pass the base frame and ask C++ to just return success status.
            val outBytes = ByteArray(nv21Bytes.size)
            
            val statusCode = nativeProcessPassThrough(
                baseFrame.width,
                baseFrame.height,
                nv21Bytes,
                outBytes
            )

            val status = NativeStatus.fromCode(statusCode)
            if (status == NativeStatus.SUCCESS) {
                logger.d("JniProcessingGateway", "Native processing succeeded (MVP Pass-through)")
                NativeProcessResult.Success(baseFrame.width, baseFrame.height, outBytes)
            } else {
                logger.e("JniProcessingGateway", "Native processing failed with code $statusCode")
                NativeProcessResult.Fallback(baseFrame, "Native returned error $statusCode")
            }
        } catch (e: Exception) {
            logger.e("JniProcessingGateway", "JNI call crashed", e)
            NativeProcessResult.Fallback(request.burstSet.frames.first(), "JNI Exception: ${e.message}")
        }
    }

    private external fun nativeProcessPassThrough(
        width: Int,
        height: Int,
        inNv21: ByteArray,
        outNv21: ByteArray
    ): Int
}
