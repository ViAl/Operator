package com.example.camerax.core.pipeline.zsl

import androidx.camera.core.ImageProxy
import com.example.camerax.core.model.capture.BurstFrameSet
import com.example.camerax.core.model.capture.FrameMetadata
import javax.inject.Inject

class ImageProxyFrameMapper @Inject constructor() {
    
    /**
     * Temporary MVP simplification: converts ImageProxy YUV_420_888 to NV21 byte array.
     * In a production zero-copy pipeline, this would just wrap an AHardwareBuffer reference.
     */
    fun mapToFrameInfo(image: ImageProxy): BurstFrameSet.Frame {
        val nv21bytes = yuv420888ToNv21(image)

        val metadata = FrameMetadata(
            timestampNs = image.imageInfo.timestamp,
            exposureTimeNs = 0L, // MVP fallback
            iso = 100,           // MVP fallback
            lensPosition = 0f    // MVP fallback
        )
        
        return BurstFrameSet.Frame(
            metadata = metadata,
            width = image.width,
            height = image.height,
            nv21Data = nv21bytes
        )
    }

    private fun yuv420888ToNv21(image: ImageProxy): ByteArray {
        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]

        val yBuffer = yPlane.buffer
        val uBuffer = uPlane.buffer
        val vBuffer = vPlane.buffer

        val numPixels = image.width * image.height
        val nv21 = ByteArray(numPixels + (numPixels / 2))

        val yRowStride = yPlane.rowStride
        val yPixelStride = yPlane.pixelStride

        // Copy Y channel
        var pos = 0
        if (yPixelStride == 1 && yRowStride == image.width) {
            yBuffer.position(0)
            yBuffer.get(nv21, 0, numPixels)
            pos = numPixels
        } else {
            val yBytes = ByteArray(yBuffer.remaining())
            yBuffer.position(0)
            yBuffer.get(yBytes)
            var srcPos = 0
            for (row in 0 until image.height) {
                System.arraycopy(yBytes, srcPos, nv21, pos, image.width)
                srcPos += yRowStride
                pos += image.width
            }
        }

        // Copy VU channel (NV21 format is YYYY... VU VU VU...)
        val vRowStride = vPlane.rowStride
        val vPixelStride = vPlane.pixelStride
        val uRowStride = uPlane.rowStride
        val uPixelStride = uPlane.pixelStride
        
        val vBytes = ByteArray(vBuffer.remaining())
        vBuffer.position(0)
        vBuffer.get(vBytes)
        
        val uBytes = ByteArray(uBuffer.remaining())
        uBuffer.position(0)
        uBuffer.get(uBytes)

        val chromaWidth = image.width / 2
        val chromaHeight = image.height / 2
        
        // MVP Slow path for foolproof extraction of VU bytes into NV21 format
        for (row in 0 until chromaHeight) {
            for (col in 0 until chromaWidth) {
                val vIndex = row * vRowStride + col * vPixelStride
                val uIndex = row * uRowStride + col * uPixelStride
                
                nv21[pos++] = vBytes[vIndex] // V
                nv21[pos++] = uBytes[uIndex] // U
            }
        }

        return nv21
    }
}
