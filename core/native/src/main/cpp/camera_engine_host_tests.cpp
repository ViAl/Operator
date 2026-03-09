#include "camera_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using cameraxmvp::CameraEngine;

struct Nv21Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bytes;
};

Nv21Frame MakeFrame(int width, int height, uint8_t yValue = 0, uint8_t v = 128, uint8_t u = 128) {
    Nv21Frame frame;
    frame.width = width;
    frame.height = height;
    const int ySize = width * height;
    const int vuSize = ySize / 2;
    frame.bytes.resize(ySize + vuSize);
    std::fill(frame.bytes.begin(), frame.bytes.begin() + ySize, yValue);
    for (int i = 0; i < vuSize; i += 2) {
        frame.bytes[ySize + i] = v;
        frame.bytes[ySize + i + 1] = u;
    }
    return frame;
}

void FillTexturedY(Nv21Frame& frame) {
    const int w = frame.width;
    const int h = frame.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int val = (x * 13 + y * 7 + ((x * y) % 31)) & 0xFF;
            frame.bytes[y * w + x] = static_cast<uint8_t>(val);
        }
    }
}

Nv21Frame ShiftFrame(const Nv21Frame& src, int dx, int dy) {
    Nv21Frame out = MakeFrame(src.width, src.height);
    const int w = src.width;
    const int h = src.height;
    const int ySize = w * h;
    const int uvW = w / 2;
    const int uvH = h / 2;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = std::clamp(x - dx, 0, w - 1);
            const int sy = std::clamp(y - dy, 0, h - 1);
            out.bytes[y * w + x] = src.bytes[sy * w + sx];
        }
    }

    for (int y = 0; y < uvH; ++y) {
        for (int x = 0; x < uvW; ++x) {
            const int sx = std::clamp(x - dx / 2, 0, uvW - 1);
            const int sy = std::clamp(y - dy / 2, 0, uvH - 1);
            const int dstIdx = ySize + (y * uvW + x) * 2;
            const int srcIdx = ySize + (sy * uvW + sx) * 2;
            out.bytes[dstIdx] = src.bytes[srcIdx];
            out.bytes[dstIdx + 1] = src.bytes[srcIdx + 1];
        }
    }
    return out;
}

float MeanAbsDiffY(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int width, int height) {
    const int ySize = width * height;
    long long sum = 0;
    for (int i = 0; i < ySize; ++i) {
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    }
    return static_cast<float>(sum) / static_cast<float>(ySize);
}

float MeanSqDiffY(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int width, int height) {
    const int ySize = width * height;
    double sum = 0.0;
    for (int i = 0; i < ySize; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return static_cast<float>(sum / static_cast<double>(ySize));
}

std::vector<uint8_t> RawAverage(const std::vector<Nv21Frame>& frames) {
    const int size = static_cast<int>(frames[0].bytes.size());
    std::vector<uint8_t> out(size, 0);
    std::vector<int> accum(size, 0);
    for (const auto& f : frames) {
        for (int i = 0; i < size; ++i) {
            accum[i] += f.bytes[i];
        }
    }
    for (int i = 0; i < size; ++i) {
        out[i] = static_cast<uint8_t>(accum[i] / static_cast<int>(frames.size()));
    }
    return out;
}

int RunEngine(CameraEngine& engine,
              int width,
              int height,
              const std::vector<const uint8_t*>& framePtrs,
              int baseIndex,
              int shortIndex,
              const std::vector<int64_t>& exposureNs,
              const std::vector<int>& iso,
              std::vector<uint8_t>& out) {
    return engine.ProcessMultiFrame(width,
                                    height,
                                    framePtrs,
                                    baseIndex,
                                    shortIndex,
                                    exposureNs,
                                    iso,
                                    out.data(),
                                    static_cast<int>(out.size()));
}


bool RunIdenticalFramesTest() {
    const int width = 320;
    const int height = 240;

    Nv21Frame base = MakeFrame(width, height);
    FillTexturedY(base);

    std::vector<Nv21Frame> frameStorage(5, base);
    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) {
        framePtrs.push_back(frame.bytes.data());
    }

    std::vector<uint8_t> out(base.bytes.size(), 0);
    CameraEngine engine;
    const int status = RunEngine(engine, width, height, framePtrs, 2, -1, std::vector<int64_t>(framePtrs.size(), 10000000), std::vector<int>(framePtrs.size(), 100), out);
    if (status != 0) {
        std::fprintf(stderr, "IdenticalFramesTest: ProcessMultiFrame failed\n");
        return false;
    }

    const float mad = MeanAbsDiffY(out, base.bytes, width, height);
    std::printf("IdenticalFramesTest: mean-abs-diff Y = %.3f\n", mad);
    return mad <= 13.0f;
}

bool RunTranslationAlignmentTest() {
    const int width = 320;
    const int height = 240;

    Nv21Frame base = MakeFrame(width, height);
    FillTexturedY(base);

    std::vector<std::pair<int, int>> shifts = {
            {-4, -2}, {-2, -1}, {0, 0}, {2, 1}, {4, 2}
    };

    std::vector<Nv21Frame> frameStorage;
    for (const auto& shift : shifts) {
        frameStorage.push_back(ShiftFrame(base, shift.first, shift.second));
    }

    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) {
        framePtrs.push_back(frame.bytes.data());
    }

    std::vector<uint8_t> out(base.bytes.size(), 0);
    CameraEngine engine;
    const int status = RunEngine(engine, width, height, framePtrs, 2, -1, std::vector<int64_t>(framePtrs.size(), 10000000), std::vector<int>(framePtrs.size(), 100), out);
    if (status != 0) {
        std::fprintf(stderr, "TranslationAlignmentTest: ProcessMultiFrame failed\n");
        return false;
    }

    const std::vector<uint8_t> avg = RawAverage(frameStorage);
    const float mseOut = MeanSqDiffY(out, base.bytes, width, height);
    const float mseAvg = MeanSqDiffY(avg, base.bytes, width, height);

    std::printf("TranslationAlignmentTest: mseOut=%.3f mseRawAvg=%.3f\n", mseOut, mseAvg);
    return mseOut < mseAvg * 0.80f;
}

bool RunMotionDeghostingTest() {
    const int width = 256;
    const int height = 192;
    const int ySize = width * height;

    std::vector<Nv21Frame> frameStorage;
    for (int i = 0; i < 5; ++i) {
        Nv21Frame f = MakeFrame(width, height, 30, 128, 128);
        const int rectW = 36;
        const int rectH = 28;
        const int startX = 20 + i * 24;
        const int startY = 70;

        for (int y = startY; y < std::min(startY + rectH, height); ++y) {
            for (int x = startX; x < std::min(startX + rectW, width); ++x) {
                f.bytes[y * width + x] = 220;
            }
        }
        frameStorage.push_back(std::move(f));
    }

    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) {
        framePtrs.push_back(frame.bytes.data());
    }

    std::vector<uint8_t> out(static_cast<size_t>(ySize + ySize / 2), 0);
    CameraEngine engine;
    const int status = RunEngine(engine, width, height, framePtrs, 2, -1, std::vector<int64_t>(framePtrs.size(), 10000000), std::vector<int>(framePtrs.size(), 100), out);
    if (status != 0) {
        std::fprintf(stderr, "MotionDeghostingTest: ProcessMultiFrame failed\n");
        return false;
    }

    const std::vector<uint8_t> avg = RawAverage(frameStorage);
    const auto& base = frameStorage[2].bytes;

    // ROI around moving object position in base frame.
    const int roiX0 = 20 + 2 * 24;
    const int roiY0 = 70;
    const int roiW = 36;
    const int roiH = 28;

    double mseOut = 0.0;
    double mseAvg = 0.0;
    int count = 0;
    for (int y = roiY0; y < roiY0 + roiH; ++y) {
        for (int x = roiX0; x < roiX0 + roiW; ++x) {
            const int idx = y * width + x;
            const double dOut = static_cast<double>(out[idx]) - static_cast<double>(base[idx]);
            const double dAvg = static_cast<double>(avg[idx]) - static_cast<double>(base[idx]);
            mseOut += dOut * dOut;
            mseAvg += dAvg * dAvg;
            count++;
        }
    }
    mseOut /= static_cast<double>(count);
    mseAvg /= static_cast<double>(count);

    std::printf("MotionDeghostingTest: roiMseOut=%.3f roiMseRawAvg=%.3f\n", mseOut, mseAvg);
    return mseOut < mseAvg;
}

bool RunToneMappingSanityTest() {
    const int width = 320;
    const int height = 240;

    Nv21Frame base = MakeFrame(width, height, 16, 128, 128);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x > width / 2) {
                base.bytes[y * width + x] = 220;
            }
            if (((x / 8) + (y / 8)) % 2 == 0) {
                base.bytes[y * width + x] = std::min(255, base.bytes[y * width + x] + 12);
            }
        }
    }

    std::vector<Nv21Frame> frameStorage(5, base);
    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) {
        framePtrs.push_back(frame.bytes.data());
    }

    std::vector<uint8_t> out(base.bytes.size(), 0);
    CameraEngine engine;
    const int status = RunEngine(engine, width, height, framePtrs, 2, -1, std::vector<int64_t>(framePtrs.size(), 10000000), std::vector<int>(framePtrs.size(), 100), out);
    if (status != 0) {
        std::fprintf(stderr, "ToneMappingSanityTest: ProcessMultiFrame failed\n");
        return false;
    }

    // Sanity: no severe blow-up around edge.
    int maxAbsDelta = 0;
    const int ySize = width * height;
    for (int i = 0; i < ySize; ++i) {
        maxAbsDelta = std::max(maxAbsDelta,
                               std::abs(static_cast<int>(out[i]) - static_cast<int>(base.bytes[i])));
    }

    std::printf("ToneMappingSanityTest: maxAbsDeltaY=%d\n", maxAbsDelta);
    return maxAbsDelta <= 64;
}



bool RunFalseHdrTriggerTest() {
    const int width = 320;
    const int height = 240;
    Nv21Frame base = MakeFrame(width, height, 80, 128, 128);
    FillTexturedY(base);

    std::vector<Nv21Frame> frameStorage(3, base);
    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) framePtrs.push_back(frame.bytes.data());

    CameraEngine engine;
    std::vector<uint8_t> outNoHdr(base.bytes.size(), 0);
    std::vector<uint8_t> outHdr(base.bytes.size(), 0);

    const int stNoHdr = RunEngine(engine, width, height, framePtrs, 1, -1,
                                  std::vector<int64_t>(3, 10000000), std::vector<int>(3, 100), outNoHdr);
    const int stHdr = RunEngine(engine, width, height, framePtrs, 1, 0,
                                std::vector<int64_t>(3, 10000000), std::vector<int>(3, 100), outHdr);
    if (stNoHdr != 0 || stHdr != 0) return false;

    const float mad = MeanAbsDiffY(outNoHdr, outHdr, width, height);
    std::printf("FalseHdrTriggerTest: mad=%.3f\n", mad);
    return mad <= 1.0f;
}

bool RunWeakShortFrameRejectionTest() {
    const int width = 320;
    const int height = 240;
    Nv21Frame base = MakeFrame(width, height, 90, 128, 128);
    FillTexturedY(base);
    Nv21Frame shortFrame = MakeFrame(width, height, 2, 128, 128);
    std::vector<Nv21Frame> frameStorage = {shortFrame, base, base};

    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) framePtrs.push_back(frame.bytes.data());

    CameraEngine engine;
    std::vector<uint8_t> outNoHdr(base.bytes.size(), 0);
    std::vector<uint8_t> outHdr(base.bytes.size(), 0);

    const int stNoHdr = RunEngine(engine, width, height, framePtrs, 1, -1,
                                  std::vector<int64_t>{4000000, 10000000, 10000000},
                                  std::vector<int>{100, 100, 100}, outNoHdr);
    const int stHdr = RunEngine(engine, width, height, framePtrs, 1, 0,
                                std::vector<int64_t>{4000000, 10000000, 10000000},
                                std::vector<int>{100, 100, 100}, outHdr);
    if (stNoHdr != 0 || stHdr != 0) return false;

    const float mad = MeanAbsDiffY(outNoHdr, outHdr, width, height);
    std::printf("WeakShortFrameRejectionTest: mad=%.3f\n", mad);
    return mad <= 2.0f;
}

bool RunHighlightRecoveryTest() {
    const int width = 320;
    const int height = 240;
    Nv21Frame base = MakeFrame(width, height, 80, 128, 128);
    Nv21Frame shortFrame = MakeFrame(width, height, 80, 128, 128);

    const int x0 = 90, y0 = 70, w = 120, h = 80;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const int idx = y * width + x;
            base.bytes[idx] = 252;
            shortFrame.bytes[idx] = static_cast<uint8_t>(170 + ((x + y) % 28));
        }
    }

    std::vector<Nv21Frame> hdrFrameStorage = {shortFrame, base, base};
    std::vector<Nv21Frame> noHdrFrameStorage = {base, base, base};
    std::vector<const uint8_t*> hdrPtrs;
    std::vector<const uint8_t*> noHdrPtrs;
    for (auto& frame : hdrFrameStorage) hdrPtrs.push_back(frame.bytes.data());
    for (auto& frame : noHdrFrameStorage) noHdrPtrs.push_back(frame.bytes.data());

    CameraEngine engine;
    std::vector<uint8_t> outNoHdr(base.bytes.size(), 0);
    std::vector<uint8_t> outHdr(base.bytes.size(), 0);

    const int stNoHdr = RunEngine(engine, width, height, noHdrPtrs, 1, -1,
                                  std::vector<int64_t>{10000000, 10000000, 10000000},
                                  std::vector<int>{100, 100, 100}, outNoHdr);
    const int stHdr = RunEngine(engine, width, height, hdrPtrs, 1, 0,
                                std::vector<int64_t>{4000000, 10000000, 10000000},
                                std::vector<int>{100, 100, 100}, outHdr);
    if (stNoHdr != 0 || stHdr != 0) return false;

    float varNoHdr = 0.0f;
    float varHdr = 0.0f;
    float meanNoHdr = 0.0f;
    float meanHdr = 0.0f;
    int count = 0;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const int idx = y * width + x;
            meanNoHdr += outNoHdr[idx];
            meanHdr += outHdr[idx];
            count++;
        }
    }
    meanNoHdr /= count;
    meanHdr /= count;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const int idx = y * width + x;
            const float d0 = outNoHdr[idx] - meanNoHdr;
            const float d1 = outHdr[idx] - meanHdr;
            varNoHdr += d0 * d0;
            varHdr += d1 * d1;
        }
    }
    varNoHdr /= count;
    varHdr /= count;
    std::printf("HighlightRecoveryTest: meanNoHdr=%.2f meanHdr=%.2f varNoHdr=%.2f varHdr=%.2f\n", meanNoHdr, meanHdr, varNoHdr, varHdr);
    return (varHdr > varNoHdr * 1.10f) || (meanHdr + 3.0f < meanNoHdr);
}

bool RunMotionHighlightSafetyTest() {
    const int width = 320;
    const int height = 240;
    std::vector<Nv21Frame> frameStorage;
    for (int i = 0; i < 3; ++i) {
        Nv21Frame f = MakeFrame(width, height, 40, 128, 128);
        const int cx = 70 + i * 60;
        const int cy = 120;
        for (int y = cy - 15; y <= cy + 15; ++y) {
            for (int x = cx - 15; x <= cx + 15; ++x) {
                if (x >= 0 && x < width && y >= 0 && y < height) {
                    f.bytes[y * width + x] = 250;
                }
            }
        }
        frameStorage.push_back(std::move(f));
    }

    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) framePtrs.push_back(frame.bytes.data());
    CameraEngine engine;
    std::vector<uint8_t> out(frameStorage[1].bytes.size(), 0);
    const int st = RunEngine(engine, width, height, framePtrs, 1, 0,
                             std::vector<int64_t>{4000000, 10000000, 10000000},
                             std::vector<int>{100, 100, 100}, out);
    if (st != 0) return false;

    int brightTrail = 0;
    for (int i = 0; i < width * height; ++i) {
        if (frameStorage[1].bytes[i] < 120 && out[i] > 220) brightTrail++;
    }
    const float ratio = static_cast<float>(brightTrail) / static_cast<float>(width * height);
    std::printf("MotionHighlightSafetyTest: trailRatio=%.4f\n", ratio);
    return ratio < 0.01f;
}

bool RunContractConsistencySanityTest() {
    const int width = 160;
    const int height = 120;
    Nv21Frame f0 = MakeFrame(width, height, 80, 128, 128);
    Nv21Frame f1 = MakeFrame(width, height, 90, 128, 128);
    std::vector<const uint8_t*> framePtrs = {f0.bytes.data(), f1.bytes.data()};

    CameraEngine engine;
    std::vector<uint8_t> out(f0.bytes.size(), 0);

    const int badMeta = RunEngine(engine, width, height, framePtrs, 1, 0,
                                  std::vector<int64_t>{10000000}, std::vector<int>{100, 100}, out);
    const int outOfRangeShort = RunEngine(engine, width, height, framePtrs, 1, 9,
                                          std::vector<int64_t>{10000000, 4000000}, std::vector<int>{100, 100}, out);
    std::printf("ContractConsistencySanityTest: badMeta=%d outOfRangeShort=%d\n", badMeta, outOfRangeShort);
    return badMeta != 0 && outOfRangeShort == 0;
}
bool RunBenchmark() {
    const int width = 1280;
    const int height = 720;

    Nv21Frame base = MakeFrame(width, height);
    FillTexturedY(base);

    std::vector<Nv21Frame> frameStorage;
    frameStorage.push_back(ShiftFrame(base, -4, -2));
    frameStorage.push_back(ShiftFrame(base, -2, -1));
    frameStorage.push_back(base);
    frameStorage.push_back(ShiftFrame(base,  2,  1));
    frameStorage.push_back(ShiftFrame(base,  4,  2));

    std::vector<const uint8_t*> framePtrs;
    for (auto& frame : frameStorage) {
        framePtrs.push_back(frame.bytes.data());
    }

    CameraEngine engine;
    std::vector<uint8_t> out(base.bytes.size(), 0);

    constexpr int kIters = 5;
    double totalMs = 0.0;
    for (int i = 0; i < kIters; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const int status = RunEngine(engine, width, height, framePtrs, 2, -1, std::vector<int64_t>(framePtrs.size(), 10000000), std::vector<int>(framePtrs.size(), 100), out);
        const auto t1 = std::chrono::steady_clock::now();
        if (status != 0) {
            std::fprintf(stderr, "Benchmark: ProcessMultiFrame failed\n");
            return false;
        }
        totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    CameraEngine::DebugStats stats;
    if (!engine.GetLastRunDebugStats(&stats)) {
        std::fprintf(stderr, "Benchmark: debug stats unavailable\n");
        return false;
    }

    std::printf("Benchmark(1280x720,5f): avgTotalMs=%.2f | align=%lld mergeY=%lld mergeVU=%lld tone=%lld total(last)=%lld\n",
                totalMs / static_cast<double>(kIters),
                stats.alignMs,
                stats.mergeYMs,
                stats.mergeVuMs,
                stats.toneMs,
                stats.totalMs);
    std::printf("Benchmark stats: avgConf=%.3f ghostRatio=%.3f avg|dx|=%.2f avg|dy|=%.2f\n",
                stats.avgConfidence,
                stats.ghostedBlockRatio,
                stats.avgAbsDx,
                stats.avgAbsDy);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const bool benchmarkOnly = (argc > 1 && std::string(argv[1]) == "--benchmark");

    if (benchmarkOnly) {
        return RunBenchmark() ? 0 : 1;
    }

    const bool t1 = RunIdenticalFramesTest();
    const bool t2 = RunTranslationAlignmentTest();
    const bool t3 = RunMotionDeghostingTest();
    const bool t4 = RunToneMappingSanityTest();
    const bool t5 = RunFalseHdrTriggerTest();
    const bool t6 = RunWeakShortFrameRejectionTest();
    const bool t7 = RunHighlightRecoveryTest();
    const bool t8 = RunMotionHighlightSafetyTest();
    const bool t9 = RunContractConsistencySanityTest();
    const bool bench = RunBenchmark();

    std::printf("\nSummary:\n");
    std::printf("  IdenticalFramesTest:       %s\n", t1 ? "PASS" : "FAIL");
    std::printf("  TranslationAlignment:      %s\n", t2 ? "PASS" : "FAIL");
    std::printf("  MotionDeghostingTest:      %s\n", t3 ? "PASS" : "FAIL");
    std::printf("  ToneMappingSanityTest:     %s\n", t4 ? "PASS" : "FAIL");
    std::printf("  FalseHdrTriggerTest:       %s\n", t5 ? "PASS" : "FAIL");
    std::printf("  WeakShortFrameRejectTest:  %s\n", t6 ? "PASS" : "FAIL");
    std::printf("  HighlightRecoveryTest:     %s\n", t7 ? "PASS" : "FAIL");
    std::printf("  MotionHighlightSafetyTest: %s\n", t8 ? "PASS" : "FAIL");
    std::printf("  ContractConsistencyTest:   %s\n", t9 ? "PASS" : "FAIL");
    std::printf("  Benchmark:                 %s\n", bench ? "PASS" : "FAIL");

    return (t1 && t2 && t3 && t4 && t5 && t6 && t7 && t8 && t9 && bench) ? 0 : 1;
}
