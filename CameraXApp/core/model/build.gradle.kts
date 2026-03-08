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
