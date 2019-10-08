#pragma once

#include <iostream>
#include <mutex>
#include <algorithm>
#include <vector>
#include <array>
#include <atomic>
#include <climits>

namespace SoundEngine {

const uint32_t FLAGS = SDL_INIT_AUDIO;

const unsigned int CLIP_COUNT = 128;
const unsigned int SAMPLE_COUNT = 4096;

using StaticFlags = uint32_t;
#define SE_STATIC_VOLUME 0x0001
#define SE_STATIC_SPEED 0x0002

SDL_AudioDeviceID audioDevice;
SDL_AudioSpec spec;

struct ClipInfo {
    int16_t *data = nullptr;
    uint32_t start = 0, end = 0;
    uint32_t progress = 0;
    StaticFlags flags = 0;
    float volumeL = 1.f, volumeR = 1.f;
    float speed = 1.0f;
    float difference = 0.f;

    bool active = false;
};

struct Clip {
    ClipInfo info;
    std::mutex mutex;

    bool play();

};

Clip *clipPointers[CLIP_COUNT] = { };
uint32_t clipCount = 0;
std::mutex clipMutex;

bool Clip::play() {
    clipMutex.lock();
    uint32_t cc = clipCount;

    if (cc == CLIP_COUNT) {
        clipMutex.unlock();
        return false;
    }

    mutex.lock();

    if (!info.active) {
        info.progress = 0;
        info.active = true;
        clipPointers[cc] = this;
        clipCount++;
    }

    mutex.unlock();
    clipMutex.unlock();
    return true;
}

ClipInfo dispatchInfos[CLIP_COUNT];
int dispatchIndices[CLIP_COUNT];
uint32_t dispatchCount = 0;
std::mutex dispatchMutex;

bool dispatch(const ClipInfo &info) {
    dispatchMutex.lock();
    uint32_t dc = dispatchCount;

    if (dc == CLIP_COUNT) {
        dispatchMutex.unlock();
        return false;
    }

    for (int i = 0; i < CLIP_COUNT; i++) {
        if (!dispatchInfos[i].active) {
            dispatchInfos[i] = info;
            dispatchInfos[i].active = true;
            dispatchIndices[dispatchCount++] = i;
            dispatchMutex.unlock();
            return true;
        }
    }

    dispatchMutex.unlock();
    return false;
}

int masterBuffer[SAMPLE_COUNT * 2];
int channelBuffer[SAMPLE_COUNT * 2];

void processInfo(ClipInfo &info) {
    int sampleRange = info.end - info.start - info.progress;
    int length = std::min(sampleRange, (int)SAMPLE_COUNT);
    uint32_t offset = (info.start + info.progress) * 2;
    bool remove = false;

    if (info.flags & SE_STATIC_SPEED) {
        if (info.flags & SE_STATIC_VOLUME) {
            for (int j = 0; j < length * 2; j++) {
                channelBuffer[j] = (int)(info.data[offset + j]);
            }
        }
        else {
            for (int j = 0; j < length * 2; j += 2) {
                channelBuffer[j] = (int)(info.data[offset + j] * info.volumeL);
                channelBuffer[j + 1] = (int)(info.data[offset + j + 1] * info.volumeR);
            }
        }

        for (int j = 0; j < length * 2; j++) {
            masterBuffer[j] += channelBuffer[j];
        }

        info.active = sampleRange > SAMPLE_COUNT;
        info.progress += length;
    }
    else {
        uint32_t sampleCount = std::min((uint32_t)floor((float)sampleRange / info.speed), SAMPLE_COUNT);
        float fProgress = info.difference;

        if (info.flags & SE_STATIC_VOLUME) {
            for (int j = 0; j < sampleCount * 2; j += 2) {
                uint32_t position = offset + floor(fProgress) * 2;
                fProgress += info.speed;
                channelBuffer[j] = info.data[position];
                channelBuffer[j + 1] = info.data[position + 1];
            }
        }
        else {
            for (int j = 0; j < sampleCount * 2; j += 2) {
                uint32_t position = offset + floor(fProgress) * 2;
                fProgress += info.speed;
                channelBuffer[j] = (int)(info.data[position] * info.volumeL);
                channelBuffer[j + 1] = (int)(info.data[position + 1] * info.volumeR);
            }
        }

        for (int j = 0; j < sampleCount * 2; j++) {
            masterBuffer[j] += channelBuffer[j];
        }

        info.active = (offset / 2) + fProgress + 3 < info.end;
        info.progress += floor(fProgress);
        info.difference = fProgress - floor(fProgress);
    }
}

void callback(void *userData, Uint8 *stream, int byteLength) {

    memset(masterBuffer, 0, SAMPLE_COUNT * 2 * sizeof(int));
    int16_t *outStream = (int16_t*)stream;

    dispatchMutex.lock();
    clipMutex.lock();
    uint32_t dcount = dispatchCount;
    uint32_t ccount = clipCount;
    dispatchMutex.unlock();
    clipMutex.unlock();

    for (int i = 0; i < ccount; i++) {
        Clip *clip = clipPointers[i];
        clip->mutex.lock();
        ClipInfo info = clip->info;
        clip->mutex.unlock();

        processInfo(info);

        clip->mutex.lock();
        clip->info = info;
        clip->mutex.unlock();
    }

    for (int i = 0; i < dcount; i++) {
        processInfo(dispatchInfos[dispatchIndices[i]]);
    }

    for (int i = 0; i < SAMPLE_COUNT * 2; i++) {
        outStream[i] = (int16_t)std::max(std::min(masterBuffer[i], SHRT_MAX), SHRT_MIN);
    }

    int backSpace = 0;
    clipMutex.lock();

    for (int i = 0; i < clipCount; i++) {
        Clip *pointer = clipPointers[i];

        if (pointer == nullptr || !pointer->info.active) {
            backSpace++;
            continue;
        }
        else if (backSpace == 0) {
            continue;
        }
        else {
            clipPointers[i - backSpace] = pointer;
            clipPointers[i] = nullptr;
        }
    }

    clipCount -= backSpace;
    clipMutex.unlock();
    backSpace = 0;
    dispatchMutex.lock();

    for (int i = 0; i < dispatchCount; i++) {
        int index = dispatchIndices[i];

        if (!dispatchInfos[index].active) {
            backSpace++;
            continue;
        }
        else if (backSpace == 0) {
            continue;
        }
        else {
            dispatchIndices[i - backSpace] = index;
        }
    }

    dispatchCount -= backSpace;
    dispatchMutex.unlock();
}

struct Track {
    SDL_AudioSpec spec;
    uint32_t length = 0;
    int16_t *buffer = nullptr;
};

std::vector<int16_t*> trackReferences;

Track load(std::string dir) {
    Track t;
    uint32_t byteLength = 0;
    if (SDL_LoadWAV(dir.c_str(), &(t.spec), (uint8_t**)(&(t.buffer)), &byteLength) == NULL) {
        std::cerr << "Failed to load track: '" << dir << "'!\n";
    }
    trackReferences.push_back(t.buffer);
    t.length = byteLength / sizeof(int16_t) / 2;
    return t;
}

void free(const Track &t) {
    for (int i = 0; i < trackReferences.size(); i++) {
        if (trackReferences[i] == t.buffer) {
            SDL_FreeWAV((uint8_t*)(t.buffer));
            trackReferences.erase(trackReferences.begin() + i);
            break;
        }
    }
}

bool init() {
    for (int i = 0; i < CLIP_COUNT; i++) {
        dispatchInfos[i].active = false;
    }

    SDL_AudioSpec wantSpec;
    SDL_memset(&wantSpec, 0, sizeof(wantSpec));
    wantSpec.freq = 44100;
    wantSpec.format = AUDIO_S16;
    wantSpec.channels = 2;
    wantSpec.samples = 4096;
    wantSpec.callback = callback;

    audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantSpec, &spec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);

    if (audioDevice == 0) {
        std::cerr << "Opening audio device failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    if (spec.format != wantSpec.format) {
        std::cerr << "Requested audio format not available!\n";
        return false;
    }

    SDL_PauseAudioDevice(audioDevice, 0);

    return true;
}

void exit() {
    for (int16_t *reference : trackReferences) {
        SDL_FreeWAV((uint8_t*)(reference));
    }
}

}
