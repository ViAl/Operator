#include "camera_engine.h"
#include <cstring>
#include <android/log.h>

#define LOG_TAG "CameraEngineNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace cameraxmvp {

CameraEngine::CameraEngine() {
    LOGI("CameraEngine constructed");
}

CameraEngine::~CameraEngine() {
    LOGI("CameraEngine destroyed");
}

int CameraEngine::ProcessPassThrough(int width, int height, const uint8_t* inBytes, uint8_t* outBytes, int size) {
    if (inBytes == nullptr || outBytes == nullptr) {
        LOGE("Null pointers passed to ProcessPassThrough");
        return 1; // ERROR_INVALID_ARGUMENT
    }

    if (size <= 0) {
        LOGE("Invalid size %d", size);
        return 1;
    }

    // MVP Pass-through: Just copy the input to output
    std::memcpy(outBytes, inBytes, size);
    
    LOGI("Successfully processed pass-through for %dx%d image (size=%d)", width, height, size);

    return 0; // SUCCESS
}

} // namespace cameraxmvp
