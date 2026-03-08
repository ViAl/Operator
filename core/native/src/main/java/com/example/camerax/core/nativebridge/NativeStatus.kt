package com.example.camerax.core.nativebridge

enum class NativeStatus(val code: Int) {
    SUCCESS(0),
    ERROR_INVALID_ARGUMENT(1),
    ERROR_UNKNOWN(99);

    companion object {
        fun fromCode(code: Int): NativeStatus {
            return entries.find { it.code == code } ?: ERROR_UNKNOWN
        }
    }
}
