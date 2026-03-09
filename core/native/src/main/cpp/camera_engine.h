#ifndef CAMERA_ENGINE_H
#define CAMERA_ENGINE_H

#include <cstdint>
#include <mutex>
#include <vector>

namespace cameraxmvp {

static constexpr int   BLOCK_SIZE      = 32;
static constexpr int   SEARCH_RANGE    = 12;
static constexpr float SAD_THRESHOLD   = 20.0f;
static constexpr float GHOST_THRESHOLD = 0.6f;
static constexpr float SHARP_EPSILON   = 1.0f;
static constexpr float SHARP_CLAMP     = 40.0f;
static constexpr float TONE_ALPHA      = 0.20f;
static constexpr int   TONE_BLUR_R     = 8;

static constexpr int   HIGHLIGHT_START = 220;
static constexpr int   HIGHLIGHT_END   = 250;
static constexpr float HDR_BLEND_MAX   = 0.85f;
static constexpr float SHADOW_LIFT_MAX = 1.08f;

class CameraEngine {
public:
    CameraEngine();
    ~CameraEngine();

    int ProcessMultiFrame(int width, int height,
                          const std::vector<const uint8_t*>& frames,
                          int baseIndex,
                          int shortExposureIndex,
                          const std::vector<int64_t>& exposureTimeNs,
                          const std::vector<int>& isoValues,
                          const std::vector<int>& exposureClass,
                          uint8_t* outBytes, int size);

    struct DebugStats {
        float avgConfidence = 0.0f;
        float ghostedBlockRatio = 0.0f;
        float avgAbsDx = 0.0f;
        float avgAbsDy = 0.0f;
        long long alignMs = 0;
        long long mergeYMs = 0;
        long long mergeVuMs = 0;
        long long toneMs = 0;
        long long totalMs = 0;
        bool valid = false;
    };

    bool GetLastRunDebugStats(DebugStats* outStats) const;

private:
    struct BlockMotion {
        int dx = 0;
        int dy = 0;
        float confidence = 0.0f;
        float sharpness  = 0.0f;
    };

    struct MotionStats {
        int ghostedBlocks = 0;
        float confidenceSum = 0.0f;
        float dxAbsSum = 0.0f;
        float dyAbsSum = 0.0f;
        int samples = 0;
    };

    BlockMotion AlignBlock(const uint8_t* baseY, const uint8_t* srcY,
                           int width, int height,
                           int blockCol, int blockRow) const;

    float ComputeBlockSharpness(const uint8_t* Y, int width, int height,
                                int blockCol, int blockRow) const;

    MotionStats BuildMotionField(const std::vector<const uint8_t*>& frames,
                                 int baseIndex,
                                 const uint8_t* baseY,
                                 int width, int height,
                                 int blockCols, int blockRows,
                                 std::vector<BlockMotion>& motionField) const;

    void MergeLuma(const std::vector<const uint8_t*>& frames,
                   int baseIndex,
                   int width, int height,
                   int blockCols, int blockRows,
                   const std::vector<BlockMotion>& motionField,
                   std::vector<float>& accum,
                   std::vector<float>& weight,
                   uint8_t* outY) const;

    void MergeChroma(const std::vector<const uint8_t*>& frames,
                     int baseIndex,
                     int width, int height,
                     int blockCols, int blockRows,
                     const std::vector<BlockMotion>& motionField,
                     std::vector<float>& accum,
                     std::vector<float>& weight,
                     uint8_t* outVU) const;

    void ApplyHdrFusion(const std::vector<const uint8_t*>& frames,
                        int width,
                        int height,
                        int baseIndex,
                        int shortExposureIndex,
                        const std::vector<int64_t>& exposureTimeNs,
                        const std::vector<int>& isoValues,
                        int blockCols,
                        const std::vector<BlockMotion>& motionField,
                        uint8_t* mergedY) const;

    void BuildToneConfidenceMap(const std::vector<BlockMotion>& motionField,
                                int numFrames,
                                int baseIndex,
                                int numBlocks,
                                std::vector<float>& toneConfMap) const;

    void LocalToneMap(uint8_t* Y, int width, int height,
                      const std::vector<float>& confidenceMap,
                      int blockCols, int blockRows,
                      std::vector<float>& scratchA,
                      std::vector<float>& scratchB) const;

    void UpdateDebugStats(const MotionStats& motionStats,
                          int numBlocks,
                          int numFrames,
                          long long alignMs,
                          long long mergeYMs,
                          long long mergeVuMs,
                          long long toneMs,
                          long long totalMs,
                          bool valid);

    mutable std::mutex statsMutex_;
    DebugStats lastStats_;
};

} // namespace cameraxmvp
#endif
