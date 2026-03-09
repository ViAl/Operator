/*
 * camera_engine.cpp
 *
 * Motion-Aware Multi-Frame Merge
 * ─────────────────────────────────────────────────────────────────────────────
 * Stages:
 *  1. Local block alignment  (32×32 blocks, SAD on Y, ±12 px search, stride 2)
 *  2. Confidence / motion map (normalized SAD → [0,1])
 *  3. Deghosting mask         (motion_score > GHOST_THRESHOLD → base frame only)
 *  4. Sharpness-aware weight  (gradient-energy proxy × confidence)
 *  5. Weighted merge          (Y exact pixel, VU block-level, base always w=1)
 *  6. Mild local tone mapping (unsharp-mask, alpha=0.25, attenuated on motion)
 */

#include "camera_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "CameraEngineNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { std::fprintf(stdout, "[I] " __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define LOGD(...) do { std::fprintf(stdout, "[D] " __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define LOGE(...) do { std::fprintf(stderr, "[E] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#endif

namespace cameraxmvp {
namespace {

inline float Clamp01(float v) {
    return std::min(1.0f, std::max(0.0f, v));
}

inline int MotionIndex(int frameIdx, int blockIdx, int numBlocks) {
    return frameIdx * numBlocks + blockIdx;
}

void CopyBaseFrame(const uint8_t* baseFrame, uint8_t* outBytes, int totalSize) {
    std::memcpy(outBytes, baseFrame, static_cast<size_t>(totalSize));
}

// Sharpness from a block in Y using an optional pixel offset.
float ComputeSharpnessAtOffset(const uint8_t* yPlane,
                               int width,
                               int height,
                               int blockCol,
                               int blockRow,
                               int offsetX,
                               int offsetY) {
    const int bx = blockCol * BLOCK_SIZE;
    const int by = blockRow * BLOCK_SIZE;
    const int bw = std::min(BLOCK_SIZE, width - bx);
    const int bh = std::min(BLOCK_SIZE, height - by);

    if (bw <= 1 || bh <= 1) {
        return 0.0f;
    }

    float energy = 0.0f;
    int count = 0;

    for (int py = 0; py < bh - 1; py += 2) {
        for (int px = 0; px < bw - 1; px += 2) {
            const int x = std::clamp(bx + px + offsetX, 0, width - 2);
            const int y = std::clamp(by + py + offsetY, 0, height - 2);

            const float gx = static_cast<float>(yPlane[y * width + (x + 1)])
                           - static_cast<float>(yPlane[y * width + x]);
            const float gy = static_cast<float>(yPlane[(y + 1) * width + x])
                           - static_cast<float>(yPlane[y * width + x]);
            energy += gx * gx + gy * gy;
            count++;
        }
    }

    if (count == 0) {
        return 0.0f;
    }

    const float meanEnergy = energy / static_cast<float>(count);
    return std::min(meanEnergy, SHARP_CLAMP);
}

} // namespace

CameraEngine::CameraEngine() { LOGI("CameraEngine constructed (motion-aware merge v4)"); }
CameraEngine::~CameraEngine() { LOGI("CameraEngine destroyed"); }

bool CameraEngine::GetLastRunDebugStats(DebugStats* outStats) const {
    if (outStats == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(statsMutex_);
    *outStats = lastStats_;
    return lastStats_.valid;
}

void CameraEngine::UpdateDebugStats(const MotionStats& motionStats,
                                    int numBlocks,
                                    int numFrames,
                                    long long alignMs,
                                    long long mergeYMs,
                                    long long mergeVuMs,
                                    long long toneMs,
                                    long long totalMs,
                                    bool valid) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    lastStats_.alignMs = alignMs;
    lastStats_.mergeYMs = mergeYMs;
    lastStats_.mergeVuMs = mergeVuMs;
    lastStats_.toneMs = toneMs;
    lastStats_.totalMs = totalMs;
    lastStats_.valid = valid;

    if (!valid || motionStats.samples <= 0) {
        lastStats_.avgConfidence = 0.0f;
        lastStats_.ghostedBlockRatio = 0.0f;
        lastStats_.avgAbsDx = 0.0f;
        lastStats_.avgAbsDy = 0.0f;
        return;
    }

    const int totalMotionBlocks = numBlocks * std::max(0, numFrames - 1);
    lastStats_.avgConfidence = motionStats.confidenceSum / static_cast<float>(motionStats.samples);
    lastStats_.ghostedBlockRatio = (totalMotionBlocks > 0)
            ? (static_cast<float>(motionStats.ghostedBlocks) / static_cast<float>(totalMotionBlocks))
            : 0.0f;
    lastStats_.avgAbsDx = motionStats.dxAbsSum / static_cast<float>(motionStats.samples);
    lastStats_.avgAbsDy = motionStats.dyAbsSum / static_cast<float>(motionStats.samples);
}

CameraEngine::BlockMotion CameraEngine::AlignBlock(const uint8_t* baseY,
                                                    const uint8_t* srcY,
                                                    int width,
                                                    int height,
                                                    int blockCol,
                                                    int blockRow) const {
    BlockMotion result;

    const int bx = blockCol * BLOCK_SIZE;
    const int by = blockRow * BLOCK_SIZE;
    const int bw = std::min(BLOCK_SIZE, width - bx);
    const int bh = std::min(BLOCK_SIZE, height - by);
    if (bw <= 0 || bh <= 0) {
        return result;
    }

    const int sampleCount = ((bw + 1) / 2) * ((bh + 1) / 2);
    if (sampleCount <= 0) {
        return result;
    }

    int bestSad = std::numeric_limits<int>::max();
    int bestDx = 0;
    int bestDy = 0;

    for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; ++dy) {
        for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; ++dx) {
            int sad = 0;
            bool abortCandidate = false;

            for (int py = 0; py < bh && !abortCandidate; py += 2) {
                for (int px = 0; px < bw; px += 2) {
                    const int baseX = bx + px;
                    const int baseYPos = by + py;

                    const int srcX = std::clamp(baseX + dx, 0, width - 1);
                    const int srcYPos = std::clamp(baseYPos + dy, 0, height - 1);

                    sad += std::abs(static_cast<int>(baseY[baseYPos * width + baseX])
                                  - static_cast<int>(srcY[srcYPos * width + srcX]));

                    // Early prune current candidate once it cannot beat best SAD.
                    if (sad >= bestSad) {
                        abortCandidate = true;
                        break;
                    }
                }
            }

            if (sad < bestSad) {
                bestSad = sad;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }

    result.dx = bestDx;
    result.dy = bestDy;

    const float normalizedSad = static_cast<float>(bestSad) / static_cast<float>(sampleCount);
    result.confidence = Clamp01(1.0f - normalizedSad / SAD_THRESHOLD);

    return result;
}

float CameraEngine::ComputeBlockSharpness(const uint8_t* Y,
                                          int width,
                                          int height,
                                          int blockCol,
                                          int blockRow) const {
    return ComputeSharpnessAtOffset(Y, width, height, blockCol, blockRow, 0, 0);
}

CameraEngine::MotionStats CameraEngine::BuildMotionField(
        const std::vector<const uint8_t*>& frames,
        int baseIndex,
        const uint8_t* baseY,
        int width,
        int height,
        int blockCols,
        int blockRows,
        std::vector<BlockMotion>& motionField) const {
    MotionStats stats;

    const int numFrames = static_cast<int>(frames.size());
    const int numBlocks = blockCols * blockRows;

    std::vector<float> baseSharpness(numBlocks, 0.0f);
    for (int br = 0; br < blockRows; ++br) {
        for (int bc = 0; bc < blockCols; ++bc) {
            const int blockIdx = br * blockCols + bc;
            baseSharpness[blockIdx] = ComputeBlockSharpness(baseY, width, height, bc, br);
        }
    }

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            for (int b = 0; b < numBlocks; ++b) {
                BlockMotion& bm = motionField[MotionIndex(f, b, numBlocks)];
                bm.dx = 0;
                bm.dy = 0;
                bm.confidence = 1.0f;
                bm.sharpness = baseSharpness[b];
            }
            continue;
        }

        const uint8_t* srcY = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const int blockIdx = br * blockCols + bc;
                BlockMotion bm = AlignBlock(baseY, srcY, width, height, bc, br);
                bm.sharpness = ComputeSharpnessAtOffset(srcY, width, height, bc, br, bm.dx, bm.dy);
                motionField[MotionIndex(f, blockIdx, numBlocks)] = bm;

                stats.confidenceSum += bm.confidence;
                stats.dxAbsSum += std::abs(bm.dx);
                stats.dyAbsSum += std::abs(bm.dy);
                stats.samples++;

                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    stats.ghostedBlocks++;
                }
            }
        }
    }

    return stats;
}

void CameraEngine::MergeLuma(const std::vector<const uint8_t*>& frames,
                             int baseIndex,
                             int width,
                             int height,
                             int blockCols,
                             int blockRows,
                             const std::vector<BlockMotion>& motionField,
                             std::vector<float>& accum,
                             std::vector<float>& weight,
                             uint8_t* outY) const {
    const int numFrames = static_cast<int>(frames.size());
    const int numBlocks = blockCols * blockRows;
    const int ySize = width * height;
    const uint8_t* baseY = frames[baseIndex];

    accum.assign(static_cast<size_t>(ySize), 0.0f);
    weight.assign(static_cast<size_t>(ySize), 0.0f);

    for (int i = 0; i < ySize; ++i) {
        accum[i] = static_cast<float>(baseY[i]);
        weight[i] = 1.0f;
    }

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            continue;
        }

        const uint8_t* srcY = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const int blockIdx = br * blockCols + bc;
                const BlockMotion& bm = motionField[MotionIndex(f, blockIdx, numBlocks)];
                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    continue;
                }

                const float sharpTerm = std::sqrt(std::min(bm.sharpness, SHARP_CLAMP) + SHARP_EPSILON);
                const float w = bm.confidence * sharpTerm;
                if (w <= 0.0f) {
                    continue;
                }

                const int bx = bc * BLOCK_SIZE;
                const int by = br * BLOCK_SIZE;
                const int bw = std::min(BLOCK_SIZE, width - bx);
                const int bh = std::min(BLOCK_SIZE, height - by);

                for (int py = 0; py < bh; ++py) {
                    for (int px = 0; px < bw; ++px) {
                        const int dstX = bx + px;
                        const int dstY = by + py;
                        const int srcX = std::clamp(dstX + bm.dx, 0, width - 1);
                        const int srcYPos = std::clamp(dstY + bm.dy, 0, height - 1);
                        const int dstIdx = dstY * width + dstX;

                        accum[dstIdx] += w * static_cast<float>(srcY[srcYPos * width + srcX]);
                        weight[dstIdx] += w;
                    }
                }
            }
        }
    }

    for (int i = 0; i < ySize; ++i) {
        const float w = std::max(weight[i], 1e-6f);
        outY[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(accum[i] / w + 0.5f), 0, 255));
    }
}

void CameraEngine::MergeChroma(const std::vector<const uint8_t*>& frames,
                               int baseIndex,
                               int width,
                               int height,
                               int blockCols,
                               int blockRows,
                               const std::vector<BlockMotion>& motionField,
                               std::vector<float>& accum,
                               std::vector<float>& weight,
                               uint8_t* outVU) const {
    const int numFrames = static_cast<int>(frames.size());
    const int numBlocks = blockCols * blockRows;
    const int ySize = width * height;
    const int vuSize = ySize / 2;
    const int uvW = width / 2;
    const int uvH = height / 2;

    const uint8_t* baseFrame = frames[baseIndex];

    accum.assign(static_cast<size_t>(vuSize), 0.0f);
    weight.assign(static_cast<size_t>(vuSize), 0.0f);

    for (int i = 0; i < vuSize; ++i) {
        accum[i] = static_cast<float>(baseFrame[ySize + i]);
        weight[i] = 1.0f;
    }

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            continue;
        }

        const uint8_t* src = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const int blockIdx = br * blockCols + bc;
                const BlockMotion& bm = motionField[MotionIndex(f, blockIdx, numBlocks)];
                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    continue;
                }

                // Conservative chroma merge: confidence-only weighting and reduced gain
                // to limit hue smearing across block boundaries in motion-heavy areas.
                const float w = bm.confidence * 0.5f;
                if (w <= 0.0f) {
                    continue;
                }

                const int uvDx = bm.dx / 2;
                const int uvDy = bm.dy / 2;

                const int uvBx = (bc * BLOCK_SIZE) / 2;
                const int uvBy = (br * BLOCK_SIZE) / 2;
                const int uvBw = std::min(BLOCK_SIZE / 2, uvW - uvBx);
                const int uvBh = std::min(BLOCK_SIZE / 2, uvH - uvBy);

                for (int vy = 0; vy < uvBh; ++vy) {
                    for (int vx = 0; vx < uvBw; ++vx) {
                        const int dstVx = uvBx + vx;
                        const int dstVy = uvBy + vy;
                        const int srcVx = std::clamp(dstVx + uvDx, 0, uvW - 1);
                        const int srcVy = std::clamp(dstVy + uvDy, 0, uvH - 1);

                        const int dstIdx = (dstVy * uvW + dstVx) * 2;
                        const int srcIdx = (srcVy * uvW + srcVx) * 2;

                        accum[dstIdx] += w * static_cast<float>(src[ySize + srcIdx]);
                        accum[dstIdx + 1] += w * static_cast<float>(src[ySize + srcIdx + 1]);
                        weight[dstIdx] += w;
                        weight[dstIdx + 1] += w;
                    }
                }
            }
        }
    }

    for (int i = 0; i < vuSize; ++i) {
        const float w = std::max(weight[i], 1e-6f);
        outVU[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(accum[i] / w + 0.5f), 0, 255));
    }
}

void CameraEngine::ApplyHdrFusion(const std::vector<const uint8_t*>& frames,
                                  int width,
                                  int height,
                                  int baseIndex,
                                  int shortExposureIndex,
                                  const std::vector<int64_t>& exposureTimeNs,
                                  const std::vector<int>& isoValues,
                                  int blockCols,
                                  const std::vector<BlockMotion>& motionField,
                                  uint8_t* mergedY) const {
    if (shortExposureIndex < 0 || shortExposureIndex >= static_cast<int>(frames.size())) {
        return;
    }

    const uint8_t* baseY = frames[baseIndex];
    const uint8_t* shortY = frames[shortExposureIndex];

    const float baseEv = static_cast<float>(std::max<int64_t>(1, exposureTimeNs[baseIndex]))
            * static_cast<float>(std::max(isoValues[baseIndex], 1));
    const float shortEv = static_cast<float>(std::max<int64_t>(1, exposureTimeNs[shortExposureIndex]))
            * static_cast<float>(std::max(isoValues[shortExposureIndex], 1));
    const float shortToBaseScale = std::clamp(baseEv / std::max(shortEv, 1.0f), 1.0f, 8.0f);

    const int numBlocks = static_cast<int>(frames.size()) * ((width + BLOCK_SIZE - 1) / BLOCK_SIZE) * ((height + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (static_cast<int>(motionField.size()) < numBlocks) {
        return;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            const float baseLuma = static_cast<float>(baseY[idx]);

            const float highlightMask = Clamp01((baseLuma - static_cast<float>(HIGHLIGHT_START))
                                                / static_cast<float>(HIGHLIGHT_END - HIGHLIGHT_START));
            if (highlightMask <= 0.0f) {
                if (baseLuma < 48.0f) {
                    const float shadowT = Clamp01((48.0f - baseLuma) / 48.0f);
                    const float gain = 1.0f + shadowT * (SHADOW_LIFT_MAX - 1.0f);
                    mergedY[idx] = static_cast<uint8_t>(std::clamp(static_cast<int>(mergedY[idx] * gain + 0.5f), 0, 255));
                }
                continue;
            }

            const int blockCol = std::min(x / BLOCK_SIZE, blockCols - 1);
            const int blockRow = std::min(y / BLOCK_SIZE, (height + BLOCK_SIZE - 1) / BLOCK_SIZE - 1);
            const int blockIdx = blockRow * blockCols + blockCol;
            const BlockMotion& shortMotion = motionField[MotionIndex(shortExposureIndex, blockIdx, blockCols * ((height + BLOCK_SIZE - 1) / BLOCK_SIZE))];
            const float motionSafe = Clamp01((shortMotion.confidence - 0.2f) / 0.8f);
            if (motionSafe <= 0.1f) {
                continue;
            }

            const int shortX = std::clamp(x + shortMotion.dx, 0, width - 1);
            const int shortYPos = std::clamp(y + shortMotion.dy, 0, height - 1);
            const float shortLuma = static_cast<float>(shortY[shortYPos * width + shortX]);
            const float shortNorm = std::clamp(shortLuma * shortToBaseScale, 0.0f, 255.0f);

            const float blend = HDR_BLEND_MAX * highlightMask * motionSafe;
            const float fused = (1.0f - blend) * static_cast<float>(mergedY[idx]) + blend * shortNorm;
            mergedY[idx] = static_cast<uint8_t>(std::clamp(static_cast<int>(fused + 0.5f), 0, 255));
        }
    }
}

void CameraEngine::BuildToneConfidenceMap(const std::vector<BlockMotion>& motionField,
                                          int numFrames,
                                          int baseIndex,
                                          int numBlocks,
                                          std::vector<float>& toneConfMap) const {
    toneConfMap.assign(static_cast<size_t>(numBlocks), 1.0f);
    if (numFrames <= 1) {
        return;
    }

    // Blend min and mean confidence per block to avoid one bad frame from fully
    // killing local contrast boost. This reduces aggressive over-attenuation.
    constexpr float kMinBlend = 0.35f;
    constexpr float kMeanBlend = 0.65f;

    for (int b = 0; b < numBlocks; ++b) {
        float minConf = 1.0f;
        float sumConf = 0.0f;
        int count = 0;
        for (int f = 0; f < numFrames; ++f) {
            if (f == baseIndex) {
                continue;
            }
            const float conf = motionField[MotionIndex(f, b, numBlocks)].confidence;
            minConf = std::min(minConf, conf);
            sumConf += conf;
            count++;
        }

        const float meanConf = (count > 0) ? (sumConf / static_cast<float>(count)) : 1.0f;
        const float blended = kMinBlend * minConf + kMeanBlend * meanConf;
        toneConfMap[b] = Clamp01(blended);
    }
}

void CameraEngine::LocalToneMap(uint8_t* Y,
                                int width,
                                int height,
                                const std::vector<float>& toneConfMap,
                                int blockCols,
                                int blockRows,
                                std::vector<float>& scratchA,
                                std::vector<float>& scratchB) const {
    const int radius = TONE_BLUR_R;

    // NOTE: block-wise attenuation may still show minor seam risk near block edges.
    // Current mitigation is mild alpha and confidence blending; keep conservative.
    scratchA.assign(static_cast<size_t>(width * height), 0.0f); // horizontal blur
    scratchB.assign(static_cast<size_t>(width * height), 0.0f); // full blur

    for (int y = 0; y < height; ++y) {
        float sum = 0.0f;
        int count = 0;

        for (int x = -radius; x <= radius; ++x) {
            const int cx = std::clamp(x, 0, width - 1);
            sum += static_cast<float>(Y[y * width + cx]);
            count++;
        }

        for (int x = 0; x < width; ++x) {
            scratchA[y * width + x] = sum / static_cast<float>(count);

            const int removeX = std::clamp(x - radius, 0, width - 1);
            const int addX = std::clamp(x + radius + 1, 0, width - 1);
            sum += static_cast<float>(Y[y * width + addX])
                 - static_cast<float>(Y[y * width + removeX]);
        }
    }

    for (int x = 0; x < width; ++x) {
        float sum = 0.0f;
        int count = 0;

        for (int y = -radius; y <= radius; ++y) {
            const int cy = std::clamp(y, 0, height - 1);
            sum += scratchA[cy * width + x];
            count++;
        }

        for (int y = 0; y < height; ++y) {
            scratchB[y * width + x] = sum / static_cast<float>(count);

            const int removeY = std::clamp(y - radius, 0, height - 1);
            const int addY = std::clamp(y + radius + 1, 0, height - 1);
            sum += scratchA[addY * width + x] - scratchA[removeY * width + x];
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int bCol = std::min(x / BLOCK_SIZE, blockCols - 1);
            const int bRow = std::min(y / BLOCK_SIZE, blockRows - 1);
            const float confidence = toneConfMap[bRow * blockCols + bCol];

            const float alpha = TONE_ALPHA * confidence;
            const float orig = static_cast<float>(Y[y * width + x]);
            const float detail = orig - scratchB[y * width + x];
            const float boosted = orig + alpha * detail;
            Y[y * width + x] = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(boosted + 0.5f), 0, 255));
        }
    }
}

int CameraEngine::ProcessMultiFrame(int width,
                                    int height,
                                    const std::vector<const uint8_t*>& frames,
                                    int baseIndex,
                                    int shortExposureIndex,
                                    const std::vector<int64_t>& exposureTimeNs,
                                    const std::vector<int>& isoValues,
                                    const std::vector<int>& exposureClass,
                                    uint8_t* outBytes,
                                    int size) {
    const auto totalStart = std::chrono::steady_clock::now();

    MotionStats emptyStats;

    if (frames.empty() || outBytes == nullptr || width <= 0 || height <= 0) {
        LOGE("Invalid args: frames=%zu out=%p width=%d height=%d",
             frames.size(), outBytes, width, height);
        UpdateDebugStats(emptyStats, 0, static_cast<int>(frames.size()), 0, 0, 0, 0, 0, false);
        return 1;
    }

    if ((width & 1) != 0 || (height & 1) != 0) {
        LOGE("YUV420 requires even dimensions, got %dx%d", width, height);
        UpdateDebugStats(emptyStats, 0, static_cast<int>(frames.size()), 0, 0, 0, 0, 0, false);
        return 1;
    }

    const int expectedSize = width * height + (width * height) / 2;
    if (size < expectedSize) {
        LOGE("Output buffer too small: got %d, required >= %d", size, expectedSize);
        UpdateDebugStats(emptyStats, 0, static_cast<int>(frames.size()), 0, 0, 0, 0, 0, false);
        return 1;
    }

    const int numFrames = static_cast<int>(frames.size());
    if (exposureTimeNs.size() != frames.size() || isoValues.size() != frames.size() || exposureClass.size() != frames.size()) {
        LOGE("Exposure metadata mismatch: frames=%d exp=%zu iso=%zu class=%zu", numFrames,
             exposureTimeNs.size(), isoValues.size(), exposureClass.size());
        UpdateDebugStats(emptyStats, 0, numFrames, 0, 0, 0, 0, 0, false);
        return 1;
    }

    if (baseIndex < 0 || baseIndex >= numFrames) {
        LOGE("Invalid baseIndex=%d for numFrames=%d", baseIndex, numFrames);
        UpdateDebugStats(emptyStats, 0, numFrames, 0, 0, 0, 0, 0, false);
        return 1;
    }

    for (int f = 0; f < numFrames; ++f) {
        if (frames[f] == nullptr) {
            LOGE("Null frame pointer at index %d", f);
            UpdateDebugStats(emptyStats, 0, numFrames, 0, 0, 0, 0, 0, false);
            return 1;
        }
    }

    const int ySize = width * height;
    const int blockCols = (width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int blockRows = (height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int numBlocks = blockCols * blockRows;

    const uint8_t* baseFrame = frames[baseIndex];

    try {
        const auto alignStart = std::chrono::steady_clock::now();

        std::vector<BlockMotion> motionField(static_cast<size_t>(numFrames) * numBlocks);
        const MotionStats stats = BuildMotionField(
                frames, baseIndex, baseFrame, width, height, blockCols, blockRows, motionField);

        const auto alignEnd = std::chrono::steady_clock::now();
        const auto mergeYStart = std::chrono::steady_clock::now();

        std::vector<float> accum;
        std::vector<float> weight;
        MergeLuma(frames, baseIndex, width, height, blockCols, blockRows,
                  motionField, accum, weight, outBytes);

        const auto mergeYEnd = std::chrono::steady_clock::now();
        const auto mergeVuStart = std::chrono::steady_clock::now();

        MergeChroma(frames, baseIndex, width, height, blockCols, blockRows,
                    motionField, accum, weight, outBytes + ySize);

        ApplyHdrFusion(
                frames,
                width,
                height,
                baseIndex,
                shortExposureIndex,
                exposureTimeNs,
                isoValues,
                blockCols,
                motionField,
                outBytes);

        const auto mergeVuEnd = std::chrono::steady_clock::now();
        const auto toneStart = std::chrono::steady_clock::now();

        std::vector<float> toneConfMap;
        BuildToneConfidenceMap(motionField, numFrames, baseIndex, numBlocks, toneConfMap);
        LocalToneMap(outBytes, width, height, toneConfMap, blockCols, blockRows, accum, weight);

        const auto toneEnd = std::chrono::steady_clock::now();

        const auto toMs = [](const std::chrono::steady_clock::time_point& a,
                             const std::chrono::steady_clock::time_point& b) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
        };

        const long long alignMs = toMs(alignStart, alignEnd);
        const long long mergeYMs = toMs(mergeYStart, mergeYEnd);
        const long long mergeVuMs = toMs(mergeVuStart, mergeVuEnd);
        const long long toneMs = toMs(toneStart, toneEnd);
        const long long totalMs = toMs(totalStart, toneEnd);

        UpdateDebugStats(stats, numBlocks, numFrames,
                         alignMs, mergeYMs, mergeVuMs, toneMs, totalMs, true);

        DebugStats outStats;
        GetLastRunDebugStats(&outStats);

        LOGD("Align stats: avgConf=%.3f ghostRatio=%.3f avg|dx|=%.2f avg|dy|=%.2f",
             outStats.avgConfidence,
             outStats.ghostedBlockRatio,
             outStats.avgAbsDx,
             outStats.avgAbsDy);

        LOGD("Stage timings ms: align=%lld mergeY=%lld mergeVU=%lld tone=%lld total=%lld",
             outStats.alignMs,
             outStats.mergeYMs,
             outStats.mergeVuMs,
             outStats.toneMs,
             outStats.totalMs);

    } catch (const std::bad_alloc&) {
        LOGE("Memory allocation failure in ProcessMultiFrame. Fallback to base frame.");
        CopyBaseFrame(baseFrame, outBytes, expectedSize);
        UpdateDebugStats(emptyStats, numBlocks, numFrames, 0, 0, 0, 0, 0, true);
        return 0;
    }

    LOGI("ProcessMultiFrame complete: %d frames, %dx%d", numFrames, width, height);
    return 0;
}

} // namespace cameraxmvp
