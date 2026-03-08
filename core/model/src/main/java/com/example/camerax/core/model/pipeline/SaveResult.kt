package com.example.camerax.core.model.pipeline

sealed interface SaveResult {
    data class Success(val uriString: String) : SaveResult
    data class Error(val exception: Throwable) : SaveResult
}
