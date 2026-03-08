#ifndef CAMERA_ENGINE_H
#define CAMERA_ENGINE_H

#include <cstdint>

namespace cameraxmvp {

/*
 * MVP Simplification:
 * The future engine will use AHardwareBuffer zero-copy architecture.
 * For now, this is a placeholder engine that takes NV21 bytes.
 */
class CameraEngine {
public:
    CameraEngine();
    ~CameraEngine();

    // Returns 0 for SUCCESS
    int ProcessPassThrough(int width, int height, const uint8_t* inBytes, uint8_t* outBytes, int size);
};

} // namespace cameraxmvp

#endif // CAMERA_ENGINE_H
