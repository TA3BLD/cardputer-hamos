#include "Voice.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdlib>
#include "Audio.h"
#include "Rig.h"
#include "Storage.h"

namespace voice {

namespace {

bool rec = false, play_ = false, ptt = false;
int recSlot = 0, peak = 0;
size_t recSamples = 0;
std::string pending;
std::string playPath;
uint32_t playOffset = 0, playSize = 0;
size_t leadIn = 0;                  // silence before the message while the radio switches to transmit

std::string pathOf(int n) { return "/HamOS/voice/m" + std::to_string(n) + ".pcm"; }

size_t source(int16_t* out, size_t max, void*) {
    size_t n = 0;
    while (n < max && leadIn) {
        out[n++] = 0;
        leadIn--;
    }
    if (n < max && playOffset < playSize) {
        size_t want = (max - n) * 2;
        if (want > playSize - playOffset) want = playSize - playOffset;
        size_t got = storage::readAt(playPath.c_str(), playOffset, out + n, want);
        playOffset += (uint32_t)got;
        n += got / 2;
        if (!got) playOffset = playSize;
    }
    return n;
}

}  // namespace

bool startRecording(int n) {
    if (n < 1 || n > kMemories || rec || !storage::ready()) return false;
    stop();
    storage::makeDir("/HamOS/voice");
    storage::remove(pathOf(n).c_str());
    if (!audio::startCapture(kRate)) return false;
    rec = true;
    recSlot = n;
    recSamples = 0;
    pending.clear();
    return true;
}

void stopRecording() {
    if (!rec) return;
    rec = false;
    audio::stopCapture();
    if (!pending.empty()) storage::appendFile(pathOf(recSlot).c_str(), pending);
    pending.clear();
}

bool recording() { return rec; }
int level() { return peak; }
float seconds() { return rec ? recSamples / (float)kRate : 0; }

bool play(int n, bool transmit) {
    if (rec || !exists(n)) return false;
    stop();
    playPath = pathOf(n);
    playSize = storage::fileSize(playPath.c_str()) & ~1u;
    playOffset = 0;
    leadIn = transmit ? kRate * 3 / 10 : 0;
    if (transmit) ptt = rig::ptt(true);
    audio::play(source, nullptr, kRate);
    play_ = true;
    return true;
}

void stop() {
    if (play_) audio::stopPlaying();
    play_ = false;
    if (ptt) rig::ptt(false);
    ptt = false;
}

bool playing() { return play_; }
bool erase(int n) {
    if (n < 1 || n > kMemories || !exists(n)) return false;
    if (rec && recSlot == n) stopRecording();
    if (playing()) stop();
    return storage::remove(pathOf(n).c_str());
}

bool exists(int n) { return storage::exists(pathOf(n).c_str()) && storage::fileSize(pathOf(n).c_str()) > 0; }
float length(int n) { return exists(n) ? storage::fileSize(pathOf(n).c_str()) / 2.0f / kRate : 0; }

void tick() {
    if (rec) {
        if (const int16_t* block = audio::capture()) {
            int p = 0;
            for (size_t i = 0; i < audio::BLOCK; i++) p = std::max(p, abs((int)block[i]));
            int lvl = p * 100 / 32768;
            peak = lvl > peak ? lvl : peak * 7 / 8;     // falls back slowly, easier to read
            pending.append((const char*)block, audio::BLOCK * 2);
            recSamples += audio::BLOCK;
        }
        if (pending.size() >= 8192) {
            storage::appendFile(pathOf(recSlot).c_str(), pending);
            pending.clear();
        }
        if (recSamples >= (size_t)kMaxSeconds * kRate) stopRecording();
    }
    if (play_ && !audio::playing()) stop();
}

}  // namespace voice
