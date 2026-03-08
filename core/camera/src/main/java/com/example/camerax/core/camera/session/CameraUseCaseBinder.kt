package com.example.camerax.core.camera.session

import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import java.util.concurrent.Executor
import javax.inject.Inject

class CameraUseCaseBinder @Inject constructor() {

    fun buildPreview(): Preview = Preview.Builder().build()

    fun buildImageAnalysis(executor: Executor): ImageAnalysis {
        // Safe defaults for MVP: keep latest frame, select best resolution dynamically
        val resolutionSelector = ResolutionSelector.Builder()
            .setResolutionStrategy(ResolutionStrategy.HIGHEST_AVAILABLE_STRATEGY)
            .build()
            
        return ImageAnalysis.Builder()
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setResolutionSelector(resolutionSelector)
            .build()
    }
}
