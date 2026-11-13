#include "Audio.h"
#include <M5Cardputer.h>

namespace audio {

static bool isCapturing = false;
static uint32_t captureRate = 8000;
static int16_t blocks[4][BLOCK];
static int blockIndex = 0;

constexpr size_t CHUNK = 1024;
static int16_t chunks[3][CHUNK];        // one playing, one queued, one being filled
static int chunkIndex = 0;
static Source source = nullptr;
static void* sourceUser = nullptr;
static uint32_t playRate = 8000;
static bool isPlaying = false;
static std::vector<int16_t> clip;
static size_t clipPos = 0;

bool startCapture(uint32_t sampleRate) {
    if (isCapturing) stopCapture();
    M5Cardputer.Speaker.end();
    auto cfg = M5Cardputer.Mic.config();
    cfg.sample_rate = sampleRate;
    M5Cardputer.Mic.config(cfg);
    isCapturing = M5Cardputer.Mic.begin();
    captureRate = sampleRate;
    blockIndex = 0;
    if (!isCapturing) M5Cardputer.Speaker.begin();
    return isCapturing;
}

void stopCapture() {
    if (!isCapturing) return;
    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    isCapturing = false;
}

bool capturing() { return isCapturing; }

const int16_t* capture() {
    if (!isCapturing) return nullptr;
    // The driver keeps two blocks queued: the one finished now was queued two calls ago.
    if (!M5Cardputer.Mic.record(blocks[blockIndex], BLOCK, captureRate)) return nullptr;
    const int16_t* done = blocks[(blockIndex + 2) % 4];
    blockIndex = (blockIndex + 1) % 4;
    return done;
}

void play(Source src, void* user, uint32_t sampleRate) {
    if (isCapturing) stopCapture();
    M5Cardputer.Speaker.stop();
    source = src;
    sourceUser = user;
    playRate = sampleRate;
    isPlaying = src != nullptr;
}

void play(std::vector<int16_t>&& samples, uint32_t sampleRate) {
    clip = std::move(samples);
    clipPos = 0;
    play([](int16_t* out, size_t max, void*) -> size_t {
        size_t n = clip.size() - clipPos < max ? clip.size() - clipPos : max;
        for (size_t i = 0; i < n; i++) out[i] = clip[clipPos + i];
        clipPos += n;
        if (n == 0) std::vector<int16_t>().swap(clip);      // finished: free the memory
        return n;
    }, nullptr, sampleRate);
}

bool playing() { return isPlaying; }

void tick() {
    if (!isPlaying) return;
    // Hand the speaker the next piece whenever its queue has room (it holds two).
    while (source && M5Cardputer.Speaker.isPlaying(0) < 2) {
        int16_t* buf = chunks[chunkIndex];
        size_t n = source(buf, CHUNK, sourceUser);
        if (n == 0) {
            source = nullptr;
            break;
        }
        M5Cardputer.Speaker.playRaw(buf, n, playRate, false, 1, 0);
        chunkIndex = (chunkIndex + 1) % 3;
    }
    if (!source && M5Cardputer.Speaker.isPlaying(0) == 0) isPlaying = false;
}

}  // namespace audio
