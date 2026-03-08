import os

def write_file(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content.strip() + '\n')

PROJECT_ROOT = r"C:\Projects\Hobby\Operator\CameraXApp"

# ----------------- ROOT -----------------
write_file(f"{PROJECT_ROOT}/settings.gradle.kts", """
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "CameraXApp"
include(":app")
include(":core:common")
include(":core:model")
include(":core:camera")
include(":core:pipeline")
include(":core:native")
include(":feature:viewfinder")
""")

write_file(f"{PROJECT_ROOT}/build.gradle.kts", """
// Top-level build file
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.hilt) apply false
    alias(libs.plugins.ksp) apply false
}
""")

write_file(f"{PROJECT_ROOT}/gradle/libs.versions.toml", """
[versions]
agp = "8.2.0"
kotlin = "1.9.22"
ksp = "1.9.22-1.0.17"
camerax = "1.4.0-alpha04"
coroutines = "1.8.0"
hilt = "2.50"
composeBom = "2024.02.00"
activityCompose = "1.8.2"

[libraries]
androidx-core-ktx = { group = "androidx.core", name = "core-ktx", version = "1.12.0" }
androidx-lifecycle-runtime-ktx = { group = "androidx.lifecycle", name = "lifecycle-runtime-ktx", version = "2.7.0" }
androidx-activity-compose = { group = "androidx.activity", name = "activity-compose", version.ref = "activityCompose" }

compose-bom = { group = "androidx.compose", name = "compose-bom", version.ref = "composeBom" }
compose-ui = { group = "androidx.compose.ui", name = "ui" }
compose-ui-graphics = { group = "androidx.compose.ui", name = "ui-graphics" }
compose-ui-tooling-preview = { group = "androidx.compose.ui", name = "ui-tooling-preview" }
compose-material3 = { group = "androidx.compose.material3", name = "material3" }

camerax-core = { group = "androidx.camera", name = "camera-core", version.ref = "camerax" }
camerax-camera2 = { group = "androidx.camera", name = "camera-camera2", version.ref = "camerax" }
camerax-lifecycle = { group = "androidx.camera", name = "camera-lifecycle", version.ref = "camerax" }
camerax-view = { group = "androidx.camera", name = "camera-view", version.ref = "camerax" }

coroutines-android = { group = "org.jetbrains.kotlinx", name = "kotlinx-coroutines-android", version.ref = "coroutines" }
hilt-android = { group = "com.google.dagger", name = "hilt-android", version.ref = "hilt" }
hilt-compiler = { group = "com.google.dagger", name = "hilt-android-compiler", version.ref = "hilt" }

[plugins]
android-application = { id = "com.android.application", version.ref = "agp" }
android-library = { id = "com.android.library", version.ref = "agp" }
kotlin-android = { id = "org.jetbrains.kotlin.android", version.ref = "kotlin" }
hilt = { id = "com.google.dagger.hilt.android", version.ref = "hilt" }
ksp = { id = "com.google.devtools.ksp", version.ref = "ksp" }
""")

# ----------------- CORE:COMMON -----------------
write_file(f"{PROJECT_ROOT}/core/common/build.gradle.kts", """
plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.hilt)
    alias(libs.plugins.ksp)
}

android {
    namespace = "com.example.camerax.core.common"
    compileSdk = 34
    defaultConfig { minSdk = 28 }
}

dependencies {
    implementation(libs.coroutines.android)
    implementation(libs.hilt.android)
    ksp(libs.hilt.compiler)
}
""")

write_file(f"{PROJECT_ROOT}/core/common/src/main/AndroidManifest.xml", """
<manifest package="com.example.camerax.core.common" />
""")

write_file(f"{PROJECT_ROOT}/core/common/src/main/java/com/example/camerax/core/common/AppDispatchers.kt", """
package com.example.camerax.core.common

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class AppDispatchers @Inject constructor() {
    val main: CoroutineDispatcher = Dispatchers.Main
    val default: CoroutineDispatcher = Dispatchers.Default
    val io: CoroutineDispatcher = Dispatchers.IO
}
""")

write_file(f"{PROJECT_ROOT}/core/common/src/main/java/com/example/camerax/core/common/Logger.kt", """
package com.example.camerax.core.common

import android.util.Log

object Logger {
    fun d(tag: String, message: String) { Log.d(tag, message) }
    fun e(tag: String, message: String, throwable: Throwable? = null) { Log.e(tag, message, throwable) }
    fun i(tag: String, message: String) { Log.i(tag, message) }
}
""")

# ----------------- CORE:MODEL -----------------
write_file(f"{PROJECT_ROOT}/core/model/build.gradle.kts", """
plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.example.camerax.core.model"
    compileSdk = 34
    defaultConfig { minSdk = 28 }
}

dependencies {
    implementation(libs.androidx.core.ktx)
}
""")

write_file(f"{PROJECT_ROOT}/core/model/src/main/AndroidManifest.xml", """
<manifest package="com.example.camerax.core.model" />
""")

write_file(f"{PROJECT_ROOT}/core/model/src/main/java/com/example/camerax/core/model/capture/CameraContracts.kt", """
package com.example.camerax.core.model.capture

@JvmInline
value class CameraDeviceId(val id: String)

enum class CameraLensFacing { FRONT, BACK, EXTERNAL }

data class CameraCapabilities(
    val deviceId: CameraDeviceId,
    val lensFacing: CameraLensFacing,
    val isZslSupported: Boolean,
    val isOisSupported: Boolean,
    val opticalBlackRegions: IntArray?
) {
    // MVP simplification: Auto-generated equals and hashcode
}

data class FrameMetadata(
    val timestampNs: Long,
    val exposureTimeNs: Long,
    val iso: Int,
    val focusDistance: Float,
    val lensAperture: Float
)

data class MotionEstimate(
    val globalMotionVectorX: Float,
    val globalMotionVectorY: Float,
    val motionBlurProbability: Float
)
""")

write_file(f"{PROJECT_ROOT}/core/model/src/main/java/com/example/camerax/core/model/pipeline/PipelineContracts.kt", """
package com.example.camerax.core.model.pipeline

import com.example.camerax.core.model.capture.FrameMetadata
import com.example.camerax.core.model.capture.MotionEstimate

/** 
 * Указатель на ImageProxy / AHardwareBuffer со счетчиком ссылок.
 * MVP Simplification: Обертка скрывает реальный инстанс android.media.Image. 
 */
interface ZslFrameRef : AutoCloseable {
    val metadata: FrameMetadata
    val hardwareBufferPtr: Long? // null если не поддерживается (используется fallback)
    val width: Int
    val height: Int
    
    // Временный метод для извлечения сырого ImageProxy (для сохранения)
    fun getRawImageProxy(): Any? 
}

data class BurstRequest(
    val targetTimestampNs: Long,
    val frameCount: Int
)

data class BurstFrameSet(
    val baseFrame: ZslFrameRef,
    val alternateFrames: List<ZslFrameRef>,
    val motionEstimate: MotionEstimate
) : AutoCloseable {
    override fun close() {
        baseFrame.close()
        alternateFrames.forEach { it.close() }
    }
}

data class NativeProcessRequest(
    val burstSet: BurstFrameSet,
    val targetTimestampNs: Long,
    val toneMappingEnabled: Boolean
)

sealed interface NativeProcessResult {
    data class Success(val outFrame: ZslFrameRef) : NativeProcessResult
    data class Fallback(val reason: String, val fallbackFrame: ZslFrameRef) : NativeProcessResult
    data class Error(val throwable: Throwable) : NativeProcessResult
}

sealed interface SaveResult {
    data class Success(val uri: String) : SaveResult
    data class Error(val throwable: Throwable) : SaveResult
}
""")

write_file(f"{PROJECT_ROOT}/core/model/src/main/java/com/example/camerax/core/model/telemetry/TelemetryContracts.kt", """
package com.example.camerax.core.model.telemetry

import com.example.camerax.core.model.capture.CameraCapabilities

sealed interface CameraSessionState {
    object Idle : CameraSessionState
    object Opening : CameraSessionState
    object Configuring : CameraSessionState
    data class Previewing(val capabilities: CameraCapabilities) : CameraSessionState
    data class Error(val error: PipelineError) : CameraSessionState
    object Closed : CameraSessionState
}

sealed interface CapturePipelineState {
    object Idle : CapturePipelineState
    data class ShutterPressed(val captureTimestampNs: Long) : CapturePipelineState
    data class ExtractingFrames(val count: Int) : CapturePipelineState
    object NativeProcessing : CapturePipelineState
    object Rendering : CapturePipelineState
    object Saving : CapturePipelineState
    data class Completed(val uri: String) : CapturePipelineState
    data class Failed(val error: PipelineError) : CapturePipelineState
}

sealed class PipelineError(msg: String, cause: Throwable? = null) : RuntimeException(msg, cause) {
    class CameraAccessDenied : PipelineError("Camera permission denied")
    class CameraDisconnected : PipelineError("Camera disconnected")
    class ExtractionFailed(msg: String) : PipelineError("Failed to extract from buffer: $msg")
    class ProcessingFailure(cause: Throwable) : PipelineError("Native processing failed", cause)
    class SaveFailure(cause: Throwable) : PipelineError("Failed to save media", cause)
}

data class PipelineStageTiming(
    val name: String,
    val durationMs: Long
)

data class PipelineTelemetry(
    val sessionStartTs: Long,
    val captureRequestTs: Long,
    val zslExtractionTimeMs: Long,
    val nativeFusionTimeMs: Long,
    val saveTimeMs: Long,
    val totalTimeMs: Long,
    val isNativeFallback: Boolean,
    val droppedFramesCount: Int
)
""")

print("Batch 1 Generated on Disk!")
