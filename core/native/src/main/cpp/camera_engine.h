#ifndef CAMERA_ENGINE_H
#define CAMERA_ENGINE_H

#include <cstdint>
#include <mutex>
#include <vector>

namespace cameraxmvp {

// ─── Tunable constants ────────────────────────────────────────────────────────
static constexpr int   BLOCK_SIZE      = 32;    // Local alignment block (pixels)
static constexpr int   SEARCH_RANGE    = 12;    // ±pixels for local SAD search
static constexpr float SAD_THRESHOLD   = 20.0f; // Normalized SAD → confidence=0 above this
static constexpr float GHOST_THRESHOLD = 0.6f;  // motion_score above this → base-frame only
static constexpr float SHARP_EPSILON   = 1.0f;  // Prevent zero sharpness weight
static constexpr float SHARP_CLAMP     = 40.0f; // Clamp sharpness to avoid noise boosting
static constexpr float TONE_ALPHA      = 0.25f; // Local contrast boost strength
static constexpr int   TONE_BLUR_R     = 8;     // Box-blur radius for tone mapping

/*
 * Motion-Aware Multi-Frame Merge.
 *
 * Algorithm stages per ProcessMultiFrame() call:
 *   1. Local block alignment   — per-block SAD search on Y plane
 *   2. Confidence / motion map — normalized SAD per block
 *   3. Deghosting mask         — low-confidence blocks fall back to base frame
 *   4. Sharpness-aware weights — gradient-energy sharpness proxy per block
 *   5. Weighted merge          — Y and VU accumulated with per-block weights
 *   6. Mild local tone mapping — unsharp-mask boost on Y, attenuated on motion blocks
 */
class CameraEngine {
public:
    CameraEngine();
    ~CameraEngine();

    // Public interface unchanged from previous version — JNI bridge untouched.
    // Returns 0 on SUCCESS, 1 on argument error.
    int ProcessMultiFrame(int width, int height,
                          const std::vector<const uint8_t*>& frames,
                          int baseIndex,
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

    // Safe readback for host-side benchmarks/tuning.
    bool GetLastRunDebugStats(DebugStats* outStats) const;

private:
    struct BlockMotion {
        int dx = 0;
        int dy = 0;
        float confidence = 0.0f; // [0, 1]
        float sharpness  = 0.0f; // gradient energy proxy
    };

    struct MotionStats {
        int ghostedBlocks = 0;
        float confidenceSum = 0.0f;
        float dxAbsSum = 0.0f;
        float dyAbsSum = 0.0f;
        int samples = 0;
    };

    // Stage 1 + 2: local block alignment and confidence for one (frame, block)
    BlockMotion AlignBlock(const uint8_t* baseY, const uint8_t* srcY,
                           int width, int height,
                           int blockCol, int blockRow) const;

    // Stage 4: gradient-energy sharpness estimate for one block in baseY
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

    void BuildToneConfidenceMap(const std::vector<BlockMotion>& motionField,
                                int numFrames,
                                int baseIndex,
                                int numBlocks,
                                std::vector<float>& toneConfMap) const;

    // Stage 6: in-place mild local tone map on Y plane.
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
#endif // CAMERA_ENGINE_H
