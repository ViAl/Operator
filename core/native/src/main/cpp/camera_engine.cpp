/*
 * camera_engine.cpp
 *
 * Motion-Aware Multi-Frame Merge
 * ─────────────────────────────────────────────────────────────────────────────
 * Stages:
 *  1. Local block alignment  (32×32 blocks, SAD on Y, ±12 px search, stride 2)
 *  2. Confidence / motion map (normalized SAD → [0,1])
 *  3. Deghosting mask         (motion_score > GHOST_THRESHOLD → base frame only)
 *  4. Sharpness-aware weight  (Laplacian energy proxy × confidence)
 *  5. Weighted merge          (Y exact pixel, VU block-level, base always w=1)
 *  6. Mild local tone mapping (unsharp-mask, alpha=0.25, attenuated on motion)
 */

#include "camera_engine.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <android/log.h>

#define LOG_TAG "CameraEngineNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace cameraxmvp {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
CameraEngine::CameraEngine()  { LOGI("CameraEngine constructed (motion-aware merge v2)"); }
CameraEngine::~CameraEngine() { LOGI("CameraEngine destroyed"); }

// ─────────────────────────────────────────────────────────────────────────────
// Stage 1 + 2: AlignBlock
//   Finds the (dx, dy) that minimises SAD between a 32×32 block in baseY and
//   the same block position in srcY.  Returns BlockMotion with dx/dy and a
//   confidence score [0,1] derived from normalised SAD.
// ─────────────────────────────────────────────────────────────────────────────
CameraEngine::BlockMotion CameraEngine::AlignBlock(
        const uint8_t* baseY, const uint8_t* srcY,
        int width, int height,
        int blockCol, int blockRow) const
{
    BlockMotion result;

    const int bx = blockCol * BLOCK_SIZE;
    const int by = blockRow * BLOCK_SIZE;
    const int bw = std::min(BLOCK_SIZE, width  - bx);
    const int bh = std::min(BLOCK_SIZE, height - by);
    if (bw <= 0 || bh <= 0) return result;

    const int pixelCount = (bw / 2) * (bh / 2); // strided samples
    if (pixelCount == 0) return result;

    int   bestSad = INT32_MAX;
    int   bestDx  = 0, bestDy = 0;

    for (int dy = -SEARCH_RANGE; dy <= SEARCH_RANGE; dy++) {
        for (int dx = -SEARCH_RANGE; dx <= SEARCH_RANGE; dx++) {
            int sad = 0;

            for (int py = 0; py < bh; py += 2) {      // stride 2 for speed
                for (int px = 0; px < bw; px += 2) {
                    const int basePx = bx + px;
                    const int basePy = by + py;

                    const int curPx = std::clamp(basePx + dx, 0, width  - 1);
                    const int curPy = std::clamp(basePy + dy, 0, height - 1);

                    sad += std::abs((int)baseY[basePy * width + basePx]
                                  - (int)srcY [curPy  * width + curPx]);
                }
            }

            if (sad < bestSad) {
                bestSad = sad;
                bestDx  = dx;
                bestDy  = dy;
            }
        }
    }

    result.dx = bestDx;
    result.dy = bestDy;

    // Normalised SAD: divide by number of strided samples.
    // Values below SAD_THRESHOLD (≈20 per pixel) → high confidence.
    const float normSad    = static_cast<float>(bestSad) / static_cast<float>(pixelCount);
    const float confidence = std::max(0.0f, 1.0f - normSad / SAD_THRESHOLD);
    result.confidence = confidence;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 4: ComputeBlockSharpness
//   Estimates local sharpness via gradient energy (|∇I|²) over the block
//   using a simple horizontal+vertical first-difference proxy.
//   Result clamped to [0, SHARP_CLAMP] to prevent noisy frames inflating weight.
// ─────────────────────────────────────────────────────────────────────────────
float CameraEngine::ComputeBlockSharpness(
        const uint8_t* Y, int width, int height,
        int blockCol, int blockRow) const
{
    const int bx = blockCol * BLOCK_SIZE;
    const int by = blockRow * BLOCK_SIZE;
    const int bw = std::min(BLOCK_SIZE, width  - bx);
    const int bh = std::min(BLOCK_SIZE, height - by);

    float energy = 0.0f;
    int   count  = 0;

    for (int py = 0; py < bh - 1; py += 2) {    // stride 2
        for (int px = 0; px < bw - 1; px += 2) {
            const int x = bx + px;
            const int y = by + py;
            const float gx = (float)Y[y * width + (x + 1)] - (float)Y[y * width + x];
            const float gy = (float)Y[(y + 1) * width + x] - (float)Y[y * width + x];
            energy += gx * gx + gy * gy;
            count++;
        }
    }

    if (count == 0) return 0.0f;
    return std::min(energy / static_cast<float>(count), SHARP_CLAMP);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 6: LocalToneMap
//   Applies a mild unsharp-mask style local contrast boost to the Y plane.
//   Uses a fast horizontal+vertical separated box blur (radius TONE_BLUR_R).
//   Boost is attenuated per-block based on the minimum confidence across frames
//   (motion regions get less boost to avoid enhancing ghost edges).
// ─────────────────────────────────────────────────────────────────────────────
void CameraEngine::LocalToneMap(
        uint8_t* Y, int width, int height,
        const std::vector<float>& minConfMap,
        int blockCols, int blockRows) const
{
    const int R = TONE_BLUR_R;
    std::vector<float> blurred(width * height, 0.0f);
    std::vector<float> tmp(width * height, 0.0f);

    // Horizontal box blur
    for (int y = 0; y < height; y++) {
        float sum = 0.0f;
        int   cnt = 0;
        for (int x = 0; x < std::min(R, width); x++) { sum += Y[y * width + x]; cnt++; }

        for (int x = 0; x < width; x++) {
            const int addX = x + R;
            const int remX = x - R - 1;
            if (addX < width) { sum += Y[y * width + addX]; cnt++; }
            if (remX >= 0)    { sum -= Y[y * width + remX]; cnt--; }
            tmp[y * width + x] = sum / static_cast<float>(std::max(cnt, 1));
        }
    }

    // Vertical box blur
    for (int x = 0; x < width; x++) {
        float sum = 0.0f;
        int   cnt = 0;
        for (int y = 0; y < std::min(R, height); y++) { sum += tmp[y * width + x]; cnt++; }

        for (int y = 0; y < height; y++) {
            const int addY = y + R;
            const int remY = y - R - 1;
            if (addY < height) { sum += tmp[addY * width + x]; cnt++; }
            if (remY >= 0)     { sum -= tmp[remY * width + x]; cnt--; }
            blurred[y * width + x] = sum / static_cast<float>(std::max(cnt, 1));
        }
    }

    // Apply: Y = Y + alpha × (Y - blurred), attenuated by per-block confidence
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int bCol = std::min(x / BLOCK_SIZE, blockCols - 1);
            const int bRow = std::min(y / BLOCK_SIZE, blockRows - 1);
            const float conf = minConfMap[bRow * blockCols + bCol];

            // Attenuate tone-map boost in low-confidence (motion) blocks
            const float alpha = TONE_ALPHA * conf;

            const float orig  = static_cast<float>(Y[y * width + x]);
            const float blur  = blurred[y * width + x];
            const float boosted = orig + alpha * (orig - blur);
            Y[y * width + x] = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(boosted + 0.5f), 0, 255));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ProcessMultiFrame — main entry point, JNI signature unchanged
// ─────────────────────────────────────────────────────────────────────────────
int CameraEngine::ProcessMultiFrame(
        int width, int height,
        const std::vector<const uint8_t*>& frames,
        int baseIndex,
        uint8_t* outBytes, int size)
{
    // ── Validation ────────────────────────────────────────────────────────────
    if (frames.empty() || outBytes == nullptr) {
        LOGE("Empty frames or null outBytes");
        return 1;
    }
    const int expectedSize = width * height + (width * height) / 2;
    if (size != expectedSize) {
        LOGE("Size mismatch: got %d, expected %d", size, expectedSize);
        return 1;
    }
    const int numFrames = static_cast<int>(frames.size());
    if (baseIndex < 0 || baseIndex >= numFrames) {
        LOGE("Invalid baseIndex %d", baseIndex);
        return 1;
    }

    const int ySize   = width * height;
    const int vuSize  = ySize / 2;
    const int uvW     = width  / 2;
    const int uvH     = height / 2;

    // Block grid dimensions
    const int blockCols = (width  + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int blockRows = (height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int numBlocks = blockCols * blockRows;

    const uint8_t* baseFrame = frames[baseIndex];
    const uint8_t* baseY     = baseFrame;

    // ── Stage 1-4: Per-frame block alignment, confidence, sharpness ───────────
    // Storage: [frameIdx][blockIdx]
    std::vector<std::vector<BlockMotion>> motionField(numFrames,
                                                       std::vector<BlockMotion>(numBlocks));
    // Base sharpness per block (computed once)
    std::vector<float> baseSharpness(numBlocks, 0.0f);
    for (int br = 0; br < blockRows; br++) {
        for (int bc = 0; bc < blockCols; bc++) {
            baseSharpness[br * blockCols + bc] =
                ComputeBlockSharpness(baseY, width, height, bc, br);
        }
    }

    int   totalLowConfBlocks = 0;
    float sumConfidence      = 0.0f;
    float sumDxMag = 0.0f, sumDyMag = 0.0f;
    int   motionSamples = 0;

    for (int f = 0; f < numFrames; f++) {
        if (f == baseIndex) {
            // Base frame: confidence = 1, no motion
            for (int b = 0; b < numBlocks; b++) {
                motionField[f][b].dx = 0;
                motionField[f][b].dy = 0;
                motionField[f][b].confidence = 1.0f;
                motionField[f][b].sharpness  = baseSharpness[b];
            }
            continue;
        }

        const uint8_t* srcY = frames[f];
        for (int br = 0; br < blockRows; br++) {
            for (int bc = 0; bc < blockCols; bc++) {
                const int bidx = br * blockCols + bc;
                BlockMotion bm = AlignBlock(baseY, srcY, width, height, bc, br);

                // Stage 4: sharpness of aligned position in src frame
                // (approximate: use same block coords, offset by motion)
                int alignedBc = std::clamp(bc + bm.dx / BLOCK_SIZE,  0, blockCols - 1);
                int alignedBr = std::clamp(br + bm.dy / BLOCK_SIZE,  0, blockRows - 1);
                bm.sharpness  = ComputeBlockSharpness(srcY, width, height,
                                                       alignedBc, alignedBr);

                motionField[f][bidx] = bm;

                sumConfidence += bm.confidence;
                sumDxMag += std::abs(bm.dx);
                sumDyMag += std::abs(bm.dy);
                motionSamples++;

                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    totalLowConfBlocks++;
                }
            }
        }
    }

    if (motionSamples > 0) {
        LOGD("Align stats: avgConf=%.2f, avgDx=%.1f, avgDy=%.1f, ghostedBlocks=%d/%d",
             sumConfidence / motionSamples,
             sumDxMag / motionSamples, sumDyMag / motionSamples,
             totalLowConfBlocks, numBlocks * (numFrames - 1));
    }

    // ── Stage 5a: Weighted Y merge ────────────────────────────────────────────
    // Per-pixel float accumulation — avoids integer overflow and loss of precision.
    std::vector<float> accumY(ySize, 0.0f);
    std::vector<float> weightY(ySize, 0.0f);

    // Base frame contributes with fixed weight 1.0 everywhere
    for (int i = 0; i < ySize; i++) {
        accumY[i]  = static_cast<float>(baseY[i]);
        weightY[i] = 1.0f;
    }

    for (int f = 0; f < numFrames; f++) {
        if (f == baseIndex) continue;

        const uint8_t* srcY = frames[f];

        for (int br = 0; br < blockRows; br++) {
            for (int bc = 0; bc < blockCols; bc++) {
                const BlockMotion& bm = motionField[f][br * blockCols + bc];

                // Stage 3: deghosting — skip block if motion too high
                const float motionScore = 1.0f - bm.confidence;
                if (motionScore > GHOST_THRESHOLD) {
                    // Protect base frame: this block contributes nothing from frame f
                    continue;
                }

                // Stage 4 / 5: final weight = confidence × sqrt(sharpness + ε)
                const float sharpW = std::sqrt(bm.sharpness + SHARP_EPSILON);
                const float w      = bm.confidence * sharpW;
                if (w <= 0.0f) continue;

                // Pixel loop for this block
                const int bx = bc * BLOCK_SIZE;
                const int by = br * BLOCK_SIZE;
                const int bw = std::min(BLOCK_SIZE, width  - bx);
                const int bh = std::min(BLOCK_SIZE, height - by);

                for (int py = 0; py < bh; py++) {
                    for (int px = 0; px < bw; px++) {
                        const int dstX = bx + px;
                        const int dstY = by + py;
                        const int srcX = std::clamp(dstX + bm.dx, 0, width  - 1);
                        const int srcY2= std::clamp(dstY + bm.dy, 0, height - 1);

                        const int dstIdx = dstY * width + dstX;
                        accumY [dstIdx] += w * static_cast<float>(srcY[srcY2 * width + srcX]);
                        weightY[dstIdx] += w;
                    }
                }
            }
        }
    }

    // Normalise Y
    for (int i = 0; i < ySize; i++) {
        outBytes[i] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(accumY[i] / weightY[i] + 0.5f), 0, 255));
    }

    // ── Stage 5b: Conservative VU merge (block-level weights) ─────────────────
    // For chroma: use the average block confidence (half-resolution blocks).
    // Low-confidence blocks fall back to base frame chroma.
    {
        std::vector<float> accumVU(vuSize, 0.0f);
        std::vector<float> weightVU(vuSize, 0.0f);

        // Base contributes weight 1.0
        for (int i = 0; i < vuSize; i++) {
            accumVU[i]  = static_cast<float>(baseFrame[ySize + i]);
            weightVU[i] = 1.0f;
        }

        for (int f = 0; f < numFrames; f++) {
            if (f == baseIndex) continue;
            const uint8_t* src = frames[f];

            for (int br = 0; br < blockRows; br++) {
                for (int bc = 0; bc < blockCols; bc++) {
                    const BlockMotion& bm = motionField[f][br * blockCols + bc];
                    const float motionScore = 1.0f - bm.confidence;
                    if (motionScore > GHOST_THRESHOLD) continue;

                    // Conservative VU weight: no sharpness factor, lower overall
                    const float w = bm.confidence * 0.5f;
                    if (w <= 0.0f) continue;

                    const int uvDx = bm.dx / 2;
                    const int uvDy = bm.dy / 2;

                    const int uvBx = (bc * BLOCK_SIZE) / 2;
                    const int uvBy = (br * BLOCK_SIZE) / 2;
                    const int uvBw = std::min(BLOCK_SIZE / 2, uvW - uvBx);
                    const int uvBh = std::min(BLOCK_SIZE / 2, uvH - uvBy);

                    for (int vy = 0; vy < uvBh; vy++) {
                        for (int vx = 0; vx < uvBw; vx++) {
                            const int dstVx = uvBx + vx;
                            const int dstVy = uvBy + vy;
                            const int srcVx = std::clamp(dstVx + uvDx, 0, uvW - 1);
                            const int srcVy = std::clamp(dstVy + uvDy, 0, uvH - 1);

                            const int dstIdx = (dstVy * uvW + dstVx) * 2;
                            const int srcIdx = (srcVy * uvW + srcVx) * 2;

                            accumVU[dstIdx]     += w * src[ySize + srcIdx];
                            accumVU[dstIdx + 1] += w * src[ySize + srcIdx + 1];
                            weightVU[dstIdx]     += w;
                            weightVU[dstIdx + 1] += w;
                        }
                    }
                }
            }
        }

        for (int i = 0; i < vuSize; i++) {
            outBytes[ySize + i] = static_cast<uint8_t>(
                std::clamp(static_cast<int>(accumVU[i] / weightVU[i] + 0.5f), 0, 255));
        }
    }

    // ── Stage 6: Mild local tone mapping ─────────────────────────────────────
    // Build per-block minimum confidence map (across all non-base frames)
    // to attenuate boost in motion areas.
    std::vector<float> minConfMap(numBlocks, 1.0f);
    for (int f = 0; f < numFrames; f++) {
        if (f == baseIndex) continue;
        for (int b = 0; b < numBlocks; b++) {
            minConfMap[b] = std::min(minConfMap[b], motionField[f][b].confidence);
        }
    }

    LocalToneMap(outBytes, width, height, minConfMap, blockCols, blockRows);

    LOGI("ProcessMultiFrame complete: %d frames, %dx%d", numFrames, width, height);
    return 0;
}

} // namespace cameraxmvp
