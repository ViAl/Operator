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
#include <android/log.h>
#include <chrono>
#include <cmath>

#define LOG_TAG "CameraEngineNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace cameraxmvp {
namespace {

inline float Clamp01(float v) {
    return std::min(1.0f, std::max(0.0f, v));
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

}  // namespace

CameraEngine::CameraEngine() { LOGI("CameraEngine constructed (motion-aware merge v2)"); }
CameraEngine::~CameraEngine() { LOGI("CameraEngine destroyed"); }

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

    int bestSad = INT32_MAX;
    int bestDx = 0;
    int bestDy = 0;

    for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; ++dy) {
        for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; ++dx) {
            int sad = 0;

            for (int py = 0; py < bh; py += 2) {
                for (int px = 0; px < bw; px += 2) {
                    const int baseX = bx + px;
                    const int baseYPos = by + py;

                    const int srcX = std::clamp(baseX + dx, 0, width - 1);
                    const int srcYPos = std::clamp(baseYPos + dy, 0, height - 1);

                    sad += std::abs(static_cast<int>(baseY[baseYPos * width + baseX])
                                  - static_cast<int>(srcY[srcYPos * width + srcX]));
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

void CameraEngine::LocalToneMap(uint8_t* Y,
                                int width,
                                int height,
                                const std::vector<float>& minConfMap,
                                int blockCols,
                                int blockRows) const {
    const int radius = TONE_BLUR_R;
    std::vector<float> horizontal(width * height, 0.0f);
    std::vector<float> blurred(width * height, 0.0f);

    for (int y = 0; y < height; ++y) {
        float sum = 0.0f;
        int count = 0;

        for (int x = -radius; x <= radius; ++x) {
            const int cx = std::clamp(x, 0, width - 1);
            sum += static_cast<float>(Y[y * width + cx]);
            count++;
        }

        for (int x = 0; x < width; ++x) {
            horizontal[y * width + x] = sum / static_cast<float>(count);

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
            sum += horizontal[cy * width + x];
            count++;
        }

        for (int y = 0; y < height; ++y) {
            blurred[y * width + x] = sum / static_cast<float>(count);

            const int removeY = std::clamp(y - radius, 0, height - 1);
            const int addY = std::clamp(y + radius + 1, 0, height - 1);
            sum += horizontal[addY * width + x] - horizontal[removeY * width + x];
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int bCol = std::min(x / BLOCK_SIZE, blockCols - 1);
            const int bRow = std::min(y / BLOCK_SIZE, blockRows - 1);
            const float confidence = minConfMap[bRow * blockCols + bCol];

            const float alpha = TONE_ALPHA * confidence;
            const float orig = static_cast<float>(Y[y * width + x]);
            const float detail = orig - blurred[y * width + x];
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
                                    uint8_t* outBytes,
                                    int size) {
    const auto totalStart = std::chrono::steady_clock::now();

    if (frames.empty() || outBytes == nullptr || width <= 0 || height <= 0) {
        LOGE("Invalid args: frames=%zu out=%p width=%d height=%d",
             frames.size(), outBytes, width, height);
        return 1;
    }

    const int expectedSize = width * height + (width * height) / 2;
    if (size != expectedSize) {
        LOGE("Size mismatch: got %d, expected %d", size, expectedSize);
        return 1;
    }

    const int numFrames = static_cast<int>(frames.size());
    if (baseIndex < 0 || baseIndex >= numFrames) {
        LOGE("Invalid baseIndex=%d for numFrames=%d", baseIndex, numFrames);
        return 1;
    }

    const int ySize = width * height;
    const int vuSize = ySize / 2;
    const int uvW = width / 2;
    const int uvH = height / 2;

    const int blockCols = (width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int blockRows = (height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int numBlocks = blockCols * blockRows;

    const uint8_t* baseFrame = frames[baseIndex];
    const uint8_t* baseY = baseFrame;

    const auto alignStart = std::chrono::steady_clock::now();

    std::vector<std::vector<BlockMotion>> motionField(
            numFrames, std::vector<BlockMotion>(numBlocks));

    std::vector<float> baseSharpness(numBlocks, 0.0f);
    for (int br = 0; br < blockRows; ++br) {
        for (int bc = 0; bc < blockCols; ++bc) {
            baseSharpness[br * blockCols + bc] = ComputeBlockSharpness(baseY, width, height, bc, br);
        }
    }

    int ghostedBlocks = 0;
    float sumConfidence = 0.0f;
    float sumDxMag = 0.0f;
    float sumDyMag = 0.0f;
    int motionSamples = 0;

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            for (int b = 0; b < numBlocks; ++b) {
                motionField[f][b].dx = 0;
                motionField[f][b].dy = 0;
                motionField[f][b].confidence = 1.0f;
                motionField[f][b].sharpness = baseSharpness[b];
            }
            continue;
        }

        const uint8_t* srcY = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const int bidx = br * blockCols + bc;
                BlockMotion bm = AlignBlock(baseY, srcY, width, height, bc, br);
                bm.sharpness = ComputeSharpnessAtOffset(srcY, width, height, bc, br, bm.dx, bm.dy);
                motionField[f][bidx] = bm;

                sumConfidence += bm.confidence;
                sumDxMag += std::abs(bm.dx);
                sumDyMag += std::abs(bm.dy);
                motionSamples++;

                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    ghostedBlocks++;
                }
            }
        }
    }

    const auto alignEnd = std::chrono::steady_clock::now();

    const auto mergeYStart = std::chrono::steady_clock::now();

    std::vector<float> accumY(ySize, 0.0f);
    std::vector<float> weightY(ySize, 0.0f);

    for (int i = 0; i < ySize; ++i) {
        accumY[i] = static_cast<float>(baseY[i]);
        weightY[i] = 1.0f;
    }

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            continue;
        }

        const uint8_t* srcY = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const BlockMotion& bm = motionField[f][br * blockCols + bc];
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

                        accumY[dstIdx] += w * static_cast<float>(srcY[srcYPos * width + srcX]);
                        weightY[dstIdx] += w;
                    }
                }
            }
        }
    }

    for (int i = 0; i < ySize; ++i) {
        const float w = std::max(weightY[i], 1e-6f);
        outBytes[i] = static_cast<uint8_t>(
                std::clamp(static_cast<int>(accumY[i] / w + 0.5f), 0, 255));
    }

    const auto mergeYEnd = std::chrono::steady_clock::now();

    const auto mergeVuStart = std::chrono::steady_clock::now();

    std::vector<float> accumVu(vuSize, 0.0f);
    std::vector<float> weightVu(vuSize, 0.0f);

    for (int i = 0; i < vuSize; ++i) {
        accumVu[i] = static_cast<float>(baseFrame[ySize + i]);
        weightVu[i] = 1.0f;
    }

    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            continue;
        }

        const uint8_t* src = frames[f];
        for (int br = 0; br < blockRows; ++br) {
            for (int bc = 0; bc < blockCols; ++bc) {
                const BlockMotion& bm = motionField[f][br * blockCols + bc];
                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    continue;
                }

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

                        accumVu[dstIdx] += w * static_cast<float>(src[ySize + srcIdx]);
                        accumVu[dstIdx + 1] += w * static_cast<float>(src[ySize + srcIdx + 1]);
                        weightVu[dstIdx] += w;
                        weightVu[dstIdx + 1] += w;
                    }
                }
            }
        }
    }

    for (int i = 0; i < vuSize; ++i) {
        const float w = std::max(weightVu[i], 1e-6f);
        outBytes[ySize + i] = static_cast<uint8_t>(
                std::clamp(static_cast<int>(accumVu[i] / w + 0.5f), 0, 255));
    }

    const auto mergeVuEnd = std::chrono::steady_clock::now();

    const auto toneStart = std::chrono::steady_clock::now();

    std::vector<float> minConfMap(numBlocks, 1.0f);
    for (int f = 0; f < numFrames; ++f) {
        if (f == baseIndex) {
            continue;
        }
        for (int b = 0; b < numBlocks; ++b) {
            minConfMap[b] = std::min(minConfMap[b], motionField[f][b].confidence);
        }
    }

    LocalToneMap(outBytes, width, height, minConfMap, blockCols, blockRows);

    const auto toneEnd = std::chrono::steady_clock::now();
    const auto totalEnd = toneEnd;

    if (motionSamples > 0) {
        LOGD("Align stats: avgConf=%.3f, ghosted=%d/%d, avg|dx|=%.2f, avg|dy|=%.2f",
             sumConfidence / static_cast<float>(motionSamples),
             ghostedBlocks,
             numBlocks * std::max(0, numFrames - 1),
             sumDxMag / static_cast<float>(motionSamples),
             sumDyMag / static_cast<float>(motionSamples));
    }

    const auto toMs = [](const std::chrono::steady_clock::time_point& a,
                         const std::chrono::steady_clock::time_point& b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };

    LOGD("Stage timings ms: align=%lld mergeY=%lld mergeVU=%lld tone=%lld total=%lld",
         static_cast<long long>(toMs(alignStart, alignEnd)),
         static_cast<long long>(toMs(mergeYStart, mergeYEnd)),
         static_cast<long long>(toMs(mergeVuStart, mergeVuEnd)),
         static_cast<long long>(toMs(toneStart, toneEnd)),
         static_cast<long long>(toMs(totalStart, totalEnd)));

    LOGI("ProcessMultiFrame complete: %d frames, %dx%d", numFrames, width, height);
    return 0;
}

}  // namespace cameraxmvp
