#include <jni.h>
#include "camera_engine.h"

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_camerax_core_nativebridge_JniProcessingGateway_nativeProcessPassThrough(
        JNIEnv *env, jobject thiz, jint width, jint height, jbyteArray in_nv21,
        jbyteArray out_nv21) {
    
    jsize in_len = env->GetArrayLength(in_nv21);
    jsize out_len = env->GetArrayLength(out_nv21);

    if (in_len != out_len) {
        return 1; // ERROR_INVALID_ARGUMENT
    }

    jbyte *in_bytes = env->GetByteArrayElements(in_nv21, nullptr);
    jbyte *out_bytes = env->GetByteArrayElements(out_nv21, nullptr);

    cameraxmvp::CameraEngine engine;
    
    int result = engine.ProcessPassThrough(
        static_cast<int>(width),
        static_cast<int>(height),
        reinterpret_cast<const uint8_t *>(in_bytes),
        reinterpret_cast<uint8_t *>(out_bytes),
        static_cast<int>(in_len)
    );

    env->ReleaseByteArrayElements(in_nv21, in_bytes, JNI_ABORT);
    env->ReleaseByteArrayElements(out_nv21, out_bytes, 0);

    return result;
}
