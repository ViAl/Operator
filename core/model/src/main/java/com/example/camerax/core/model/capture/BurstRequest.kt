package com.example.camerax.core.model.capture

data class BurstRequest(
    val targetTimestampNs: Long,
    val framesNum: Int = 3
)
