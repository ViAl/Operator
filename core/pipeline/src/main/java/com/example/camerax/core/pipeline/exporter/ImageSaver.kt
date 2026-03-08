package com.example.camerax.core.pipeline.exporter

import com.example.camerax.core.model.pipeline.NativeProcessResult
import com.example.camerax.core.model.pipeline.SaveResult

interface ImageSaver {
    suspend fun save(result: NativeProcessResult): SaveResult
}
