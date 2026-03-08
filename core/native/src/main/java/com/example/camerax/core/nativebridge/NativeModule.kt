package com.example.camerax.core.nativebridge

import dagger.Binds
import dagger.Module
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
interface NativeBindsModule {

    @Binds
    @Singleton
    fun bindNativeProcessingGateway(impl: JniProcessingGateway): NativeProcessingGateway
}
