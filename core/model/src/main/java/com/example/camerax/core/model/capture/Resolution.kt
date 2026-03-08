package com.example.camerax.core.model.capture

data class Resolution(
    val width: Int,
    val height: Int
) {
    val aspectRatio: Float
        get() = if (height != 0) width.toFloat() / height else 0f
}
