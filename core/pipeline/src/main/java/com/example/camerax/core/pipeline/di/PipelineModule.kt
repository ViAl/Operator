package com.example.camerax.core.pipeline.di

import com.example.camerax.core.pipeline.exporter.ImageSaver
import com.example.camerax.core.pipeline.exporter.MediaStoreImageSaver
import com.example.camerax.core.pipeline.orchestrator.PhotoPipelineOrchestrator
import com.example.camerax.core.pipeline.telemetry.DefaultPipelineTelemetryLogger
import com.example.camerax.core.pipeline.telemetry.PipelineTelemetryLogger
import com.example.camerax.core.pipeline.zsl.ImageProxyFrameMapper
import com.example.camerax.core.pipeline.zsl.InMemoryZslFrameBuffer
import com.example.camerax.core.pipeline.zsl.ZslFrameBuffer
import dagger.Binds
import dagger.Module
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
interface PipelineBindsModule {

    @Binds
    @Singleton
    fun bindZslFrameBuffer(impl: InMemoryZslFrameBuffer): ZslFrameBuffer

    @Binds
    @Singleton
    fun bindImageSaver(impl: MediaStoreImageSaver): ImageSaver

    @Binds
    @Singleton
    fun bindPipelineTelemetryLogger(impl: DefaultPipelineTelemetryLogger): PipelineTelemetryLogger
    
    // Removed temporary ImageProcessingGatewayStub binding here!
    // It is now successfully bound in core:native by NativeBindsModule.
}
