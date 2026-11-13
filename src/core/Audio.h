// Microphone capture and generated-audio playback. The microphone and the
// speaker share one audio interface, so only one of them is active at a time.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

constexpr size_t BLOCK = 256;

bool startCapture(uint32_t sampleRate);     // switches the speaker off
void stopCapture();                         // and the speaker back on
bool capturing();
// A finished block of BLOCK samples, or nullptr if none is ready. Call once per loop.
const int16_t* capture();

// Playback without blocking; tick() keeps the speaker fed. A source writes up
// to `max` samples per call and returns 0 when it has no more.
typedef size_t (*Source)(int16_t* out, size_t max, void* user);
void play(Source source, void* user, uint32_t sampleRate);
void play(std::vector<int16_t>&& samples, uint32_t sampleRate);     // short clips
bool playing();
void stopPlaying();                 // cancels what is playing
void tick();

}  // namespace audio
