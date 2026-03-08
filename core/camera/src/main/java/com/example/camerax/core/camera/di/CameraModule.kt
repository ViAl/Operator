package com.example.camerax.core.camera.di

import com.example.camerax.core.camera.provider.DeviceCapabilityRepository
import com.example.camerax.core.camera.provider.DeviceCapabilityRepositoryImpl
import com.example.camerax.core.camera.session.CameraSessionController
import com.example.camerax.core.camera.session.CameraSessionControllerImpl
import com.example.camerax.core.camera.session.CameraUseCaseBinder
import dagger.Binds
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object CameraProvidesModule {
    @Provides
    fun provideCameraUseCaseBinder(): CameraUseCaseBinder = CameraUseCaseBinder()
}

@Module
@InstallIn(SingletonComponent::class)
interface CameraBindsModule {

    @Binds
    @Singleton
    fun bindCameraSessionController(impl: CameraSessionControllerImpl): CameraSessionController
    
    @Binds
    @Singleton
    fun bindDeviceCapabilityRepository(impl: DeviceCapabilityRepositoryImpl): DeviceCapabilityRepository
}
