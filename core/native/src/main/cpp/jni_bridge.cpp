#include <jni.h>
#include <vector>
#include "camera_engine.h"
#include <android/log.h>

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_camerax_core_nativebridge_JniProcessingGateway_nativeProcessMultiFrame(
        JNIEnv *env,
        jobject thiz,
        jint width,
        jint height,
        jobjectArray in_nv21_arrays,
        jint base_index,
        jint short_exposure_index,
        jlongArray exposure_time_ns,
        jintArray iso_values,
        jbyteArray out_nv21) {

    jsize num_frames = env->GetArrayLength(in_nv21_arrays);
    if (num_frames == 0) return 1;

    if (env->GetArrayLength(exposure_time_ns) != num_frames
        || env->GetArrayLength(iso_values) != num_frames) {
        __android_log_print(ANDROID_LOG_ERROR, "JNIBridge", "Exposure metadata size mismatch");
        return 1;
    }

    std::vector<jbyteArray> j_arrays;
    std::vector<jbyte*> j_bytes;
    std::vector<const uint8_t*> c_frames;

    jsize expected_size = env->GetArrayLength(out_nv21);

    for (int i = 0; i < num_frames; i++) {
        jbyteArray arr = (jbyteArray) env->GetObjectArrayElement(in_nv21_arrays, i);
        if (env->GetArrayLength(arr) != expected_size) {
            __android_log_print(ANDROID_LOG_ERROR, "JNIBridge", "Frame %d has invalid size", i);
            for (size_t j = 0; j < j_arrays.size(); j++) {
                env->ReleaseByteArrayElements(j_arrays[j], j_bytes[j], JNI_ABORT);
                env->DeleteLocalRef(j_arrays[j]);
            }
            return 1;
        }

        jbyte* bytes = env->GetByteArrayElements(arr, nullptr);
        j_arrays.push_back(arr);
        j_bytes.push_back(bytes);
        c_frames.push_back(reinterpret_cast<const uint8_t*>(bytes));
    }

    jlong* exp_ptr = env->GetLongArrayElements(exposure_time_ns, nullptr);
    jint* iso_ptr = env->GetIntArrayElements(iso_values, nullptr);
    jbyte *out_bytes = env->GetByteArrayElements(out_nv21, nullptr);

    std::vector<int64_t> exp_vec(static_cast<size_t>(num_frames));
    std::vector<int> iso_vec(static_cast<size_t>(num_frames));
    for (int i = 0; i < num_frames; ++i) {
        exp_vec[static_cast<size_t>(i)] = static_cast<int64_t>(exp_ptr[i]);
        iso_vec[static_cast<size_t>(i)] = static_cast<int>(iso_ptr[i]);
    }

    cameraxmvp::CameraEngine engine;

    int result = engine.ProcessMultiFrame(
        static_cast<int>(width),
        static_cast<int>(height),
        c_frames,
        static_cast<int>(base_index),
        static_cast<int>(short_exposure_index),
        exp_vec,
        iso_vec,
        reinterpret_cast<uint8_t *>(out_bytes),
        static_cast<int>(expected_size)
    );

    for (size_t i = 0; i < j_arrays.size(); i++) {
        env->ReleaseByteArrayElements(j_arrays[i], j_bytes[i], JNI_ABORT);
        env->DeleteLocalRef(j_arrays[i]);
    }

    env->ReleaseLongArrayElements(exposure_time_ns, exp_ptr, JNI_ABORT);
    env->ReleaseIntArrayElements(iso_values, iso_ptr, JNI_ABORT);
    env->ReleaseByteArrayElements(out_nv21, out_bytes, 0);

    return result;
}
