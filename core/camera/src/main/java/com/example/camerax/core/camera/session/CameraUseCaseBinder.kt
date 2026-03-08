package com.example.camerax.core.camera.session

import android.util.Size
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import java.util.concurrent.Executor
import javax.inject.Inject

class CameraUseCaseBinder @Inject constructor() {

    fun buildPreview(): Preview = Preview.Builder().build()

    fun buildImageCapture(): ImageCapture =
        ImageCapture.Builder()
            .setCaptureMode(ImageCapture.CAPTURE_MODE_MAXIMIZE_QUALITY)
            .build()

    fun buildImageAnalysis(executor: Executor): ImageAnalysis {
        // Limit analyzer to max 1280x720 to prevent OOM in ZSL buffer.
        // At 1280x720, one NV21 frame ≈ 1.4 MB → 8-frame buffer ≈ 11 MB (safe).
        // Preview and ImageCapture still use the camera's native resolution independently.
        val resolutionSelector = ResolutionSelector.Builder()
            .setResolutionStrategy(
                ResolutionStrategy(
                    Size(1280, 720),
                    ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER
                )
            )
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
            .build()

        return ImageAnalysis.Builder()
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setResolutionSelector(resolutionSelector)
            .build()
    }
}
