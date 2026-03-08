package com.example.camerax.core.common.di

import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.AndroidLogger
import com.example.camerax.core.common.logger.Logger
import dagger.Binds
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import kotlinx.coroutines.Dispatchers
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object CommonProvidesModule {

    @Provides
    @Singleton
    fun provideAppDispatchers(): AppDispatchers = AppDispatchers(
        main = Dispatchers.Main,
        io = Dispatchers.IO,
        default = Dispatchers.Default
    )
}

@Module
@InstallIn(SingletonComponent::class)
interface CommonBindsModule {

    @Binds
    @Singleton
    fun bindLogger(impl: AndroidLogger): Logger
}
