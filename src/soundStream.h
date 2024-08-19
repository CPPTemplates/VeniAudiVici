#pragma once
#include <SFML/Audio.hpp>
#include "math/random/random.h"

// the amount of channels
constexpr int channelCount = 2;

constexpr int samplesPerChunk = 0x400;

// the size of the audio buffer.
constexpr int audioBufSize = samplesPerChunk * channelCount;

// inclusive max sample amplitudes
constexpr short soundMax = 32767, soundMin = -32768;

constexpr int sampleRate = 44100;
// original:
// https://en.sfml-dev.org/forums/index.php?topic=24924.0

auto currentRandom = getGeneratorBasedOnTime();

class rawSoundStream : public sf::SoundStream
{
public:
    virtual void SetBufSize(int sz, int channelCount, int sampleRate)
    {
        samples.resize(sz, 0);
        currentSample = 0;
        initialize(channelCount, sampleRate);
    }

    virtual bool onGetData(Chunk &data)
    {
        data.samples = &samples[currentSample];

        data.sampleCount = audioBufSize;
        currentSample = 0;

        return true;
    }

    virtual void onSeek(sf::Time timeOffset)
    {
        currentSample = static_cast<std::size_t>(timeOffset.asSeconds() * (float)getSampleRate() * (float)getChannelCount());
    }

    std::vector<sf::Int16> samples;
    std::size_t currentSample;
};
