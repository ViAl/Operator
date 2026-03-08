package com.example.camerax.core.pipeline.exporter

import com.example.camerax.core.model.pipeline.NativeProcessResult
import com.example.camerax.core.model.pipeline.SaveResult

interface ImageSaver {
    /** Saves from the NV21-based multi-frame pipeline (legacy / denoising path). */
    suspend fun save(result: NativeProcessResult): SaveResult

    /** Saves a raw JPEG byte array directly to MediaStore (full-res ImageCapture path). */
    suspend fun saveJpeg(jpegBytes: ByteArray): SaveResult
}
