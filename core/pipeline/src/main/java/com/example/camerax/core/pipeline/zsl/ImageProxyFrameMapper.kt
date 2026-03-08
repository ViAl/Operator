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
            lensPosition = 0f,   // MVP fallback
            rotationDegrees = image.imageInfo.rotationDegrees
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

        yBuffer.rewind()
        uBuffer.rewind()
        vBuffer.rewind()

        val numPixels = image.width * image.height
        val nv21 = ByteArray(numPixels + (numPixels / 2))

        val yRowStride = yPlane.rowStride
        val yPixelStride = yPlane.pixelStride

        // Copy Y channel
        var pos = 0
        if (yPixelStride == 1 && yRowStride == image.width) {
            yBuffer.get(nv21, 0, numPixels)
            pos = numPixels
        } else {
            val yRowData = ByteArray(yRowStride)
            for (row in 0 until image.height) {
                val bytesToRead = minOf(yRowStride, yBuffer.remaining())
                yBuffer.get(yRowData, 0, bytesToRead)
                var srcPos = 0
                for (col in 0 until image.width) {
                    nv21[pos++] = yRowData[srcPos]
                    srcPos += yPixelStride
                }
            }
        }

        // Copy VU channel
        val vRowStride = vPlane.rowStride
        val vPixelStride = vPlane.pixelStride
        val uRowStride = uPlane.rowStride
        val uPixelStride = uPlane.pixelStride

        val chromaWidth = image.width / 2
        val chromaHeight = image.height / 2

        val vRowData = ByteArray(vRowStride)
        val uRowData = ByteArray(uRowStride)

        for (row in 0 until chromaHeight) {
            val vBytesToRead = minOf(vRowStride, vBuffer.remaining())
            vBuffer.get(vRowData, 0, vBytesToRead)

            val uBytesToRead = minOf(uRowStride, uBuffer.remaining())
            uBuffer.get(uRowData, 0, uBytesToRead)

            var vSrcPos = 0
            var uSrcPos = 0

            for (col in 0 until chromaWidth) {
                nv21[pos++] = vRowData[vSrcPos]
                nv21[pos++] = uRowData[uSrcPos]
                
                vSrcPos += vPixelStride
                uSrcPos += uPixelStride
            }
        }

        return nv21
    }
}
