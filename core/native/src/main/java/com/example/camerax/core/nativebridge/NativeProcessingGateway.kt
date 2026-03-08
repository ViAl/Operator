package com.example.camerax.core.nativebridge

import com.example.camerax.core.model.pipeline.NativeProcessRequest
import com.example.camerax.core.model.pipeline.NativeProcessResult

interface NativeProcessingGateway {
    suspend fun process(request: NativeProcessRequest): NativeProcessResult
}
