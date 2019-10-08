#pragma once

#include <iostream>
#include <mutex>
#include <algorithm>
#include <vector>

// TODO:
// id search optimization
// clip array defragmentation optimization
// limiter
// panning
// playback speed / pitch

namespace Sound {

void audioFormatInfo(const SDL_AudioSpec &spec);

const uint32_t FLAGS = SDL_INIT_AUDIO;

const float VOL = 0.5f;

SDL_AudioDeviceID audioDevice;
SDL_AudioSpec spec;

struct Track {
    SDL_AudioSpec spec;
    uint32_t length = 0;
    int16_t *buffer = nullptr;
};

std::vector<int16_t*> trackReferences;

struct Clip {
    int16_t *source = nullptr;
    int maskStart;
    int maskEnd;
    float volume;
    uint32_t progress = 0;
};

const int BUFFER_LENGTH = 8192;
int processingBuffer[BUFFER_LENGTH];

const int CLIP_COUNT = 128;
Clip clips[CLIP_COUNT];
int clipIds[CLIP_COUNT];
int nextId = 0;
int clipCount = 0;
std::mutex clipMutex;

inline int getClipIndex(int id) {
    int index = -1;
    for (int i = 0; i < clipCount; i++) {
        if (clipIds[i] == id) {
            index = i;
            break;
        }
    }
    return index;
}

struct Handle {
    int id;

    void setProgress(uint32_t p) {
        if (id < 0) return;
        clipMutex.lock();
        int index;
        if ((index = getClipIndex(id)) > 0) {
            clips[index].progress = p;
        }
        clipMutex.unlock();
    }

    void setVolume(float v) {
        if (id < 0) return;
        clipMutex.lock();
        int index;
        if ((index = getClipIndex(id)) > 0) {
            clips[index].volume = v;
        }
        clipMutex.unlock();
    }

    uint32_t getProgress() {
        uint32_t p = 0;
        if (id < 0) return p;
        clipMutex.lock();
        int index;
        if ((index = getClipIndex(id)) > 0) {
            p = clips[index].progress;
        }
        clipMutex.unlock();
        return p;
    }

    float getVolume() {
        float v = 0.f;
        if (id < 0) return v;
        clipMutex.lock();
        int index;
        if ((index = getClipIndex(id)) > 0) {
            v = clips[index].volume;
        }
        clipMutex.unlock();
        return v;
    }

    void stop() {
        if (id < 0) return;
        id = -1;
        clipMutex.lock();
        int index;
        if ((index = getClipIndex(id)) > 0) {
            for (int i = index; i < clipCount - 1; i++) {
                clips[i] = clips[i + 1];
                clipIds[i] = clipIds[i + 1];
            }
            clipCount--;
        }
        clipMutex.unlock();
    }
};

Handle play(const Clip &clip) {
    Handle handle = {-1 };
    if (clipCount < CLIP_COUNT) {
        clipMutex.lock();
        clips[clipCount] = clip;
        handle.id = nextId++;
        clipIds[clipCount++] = handle.id;
        clipMutex.unlock();
    }
    return handle;
}

void audio_callback(void* userdata, Uint8* stream, int byteLength) {
    int16_t *output = (int16_t*)stream;
    clipMutex.lock();

    int length = byteLength / sizeof(int16_t);
    int lengthDone = 0;
    while (lengthDone < length) {
        memset(processingBuffer, 0, BUFFER_LENGTH * sizeof(int));
        int currentLength = std::min(length - lengthDone, BUFFER_LENGTH);
        for (int i = 0; i < clipCount; i++) {
            int offset = clips[i].maskStart + clips[i].progress;
            int clipLength = std::min(currentLength, clips[i].maskEnd - offset);
            float volume = clips[i].volume;
            clips[i].progress += clipLength;
            for (int j = 0; j < clipLength; j += 2) {
                processingBuffer[j] += (int)(clips[i].source[offset + j] * volume);
                processingBuffer[j + 1] = (int)(clips[i].source[offset + j + 1] * volume);
            }
        }
        for (int i = 0; i < currentLength; i++) {
            output[lengthDone + i] = (int16_t)(processingBuffer[i]);
        }
        lengthDone += currentLength;
    }

    int empty = clipCount;
    for (int i = 0; i < clipCount; i++) {
        if (clips[i].source == nullptr) {
            empty = (i < empty ? i : empty);
        } else if (clips[i].progress >= clips[i].maskEnd - clips[i].maskStart) {
            empty = (i < empty ? i : empty);
            clips[i].source = nullptr;
        } else if (empty < i) {
            clips[empty] = clips[i];
            clipIds[empty] = clipIds[i];
            clips[i].source = nullptr;
            i = empty;
            empty = clipCount;
        }
    }
    clipCount = empty;

    clipMutex.unlock();
}

Track load(std::string dir) {
    Track t;
    uint32_t byteLength = 0;
    if (SDL_LoadWAV(dir.c_str(), &(t.spec), (uint8_t**)(&(t.buffer)), &byteLength) == NULL) {
        std::cerr << "Failed to load track: '" << dir << "'!\n";
    }
    trackReferences.push_back(t.buffer);
    t.length = byteLength / sizeof(int16_t);
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
    SDL_AudioSpec wantSpec;
    SDL_memset(&wantSpec, 0, sizeof(wantSpec));
    wantSpec.freq = 44100;
    wantSpec.format = AUDIO_S16;
    wantSpec.channels = 2;
    wantSpec.samples = 4096;
    wantSpec.callback = audio_callback;

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

    audioFormatInfo(spec);

    return true;
}

void audioFormatInfo(const SDL_AudioSpec &spec) {
    std::cout << "Format:\n";
    std::cout << "\tBitsize: " << SDL_AUDIO_BITSIZE(spec.format) << '\n';

    if (SDL_AUDIO_ISFLOAT(spec.format)) std::cout << "\tFloat\n";
    else std::cout << "\tInteger\n";
    
    if (SDL_AUDIO_ISBIGENDIAN(spec.format)) std::cout << "\tBig endian\n";
    else std::cout << "\tSmall endian\n";

    if (SDL_AUDIO_ISSIGNED(spec.format)) std::cout << "\tSigned\n";
    else std::cout << "\tUnsigned\n";

    std::cout << "Frequency: " << spec.freq << '\n';
}

void exit() {
    for (int16_t *reference : trackReferences) {
        SDL_FreeWAV((uint8_t*)(reference));
    }
}

}
