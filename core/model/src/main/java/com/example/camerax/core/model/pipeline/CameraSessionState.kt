package com.example.camerax.core.model.pipeline

sealed interface CameraSessionState {
    object Initializing : CameraSessionState
    object Starting : CameraSessionState
    object Active : CameraSessionState
    object Closed : CameraSessionState
    data class Error(val throwable: Throwable) : CameraSessionState
}
