#pragma once
#include <juce_dsp/juce_dsp.h>

namespace NovaMB {

struct BandParameters {
    float frequencyLow = 20.0f;
    float frequencyHigh = 20000.0f;
    float threshold = -20.0f;
    float ratio = 4.0f;
    float attack = 20.0f;
    float release = 100.0f;
    float knee = 6.0f;
    float makeUpGain = 0.0f;
    float lookahead = 0.0f;
    bool sidechainExternal = false;
    bool solo = false;
    bool mute = false;
    bool bypassed = false;
    bool active = true;
};

class CompressorBand {
public:
    CompressorBand();
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer);
    void updateParameters(const BandParameters& params);
    float getGainReduction() const;
    bool isSolo() const { return parameters.solo; }
    bool isMute() const { return parameters.mute; }
    bool isActive() const { return parameters.active; }

private:
    juce::dsp::LinkwitzRileyFilter<float> loPass, hiPass;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> gain;
    BandParameters parameters;
    float lastReduction = 0.0f;
};

class MultibandEngine {
public:
    MultibandEngine();
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer);
    void updateBand(int index, const BandParameters& params);
    float getGainReduction(int index) const;

    // FFT Data for visuals
    void getFFTData(float* dest);
    void getSidechainFFTData(float* dest);
    int getFFTSize() const { return 1 << fftOrder; }

private:
    void pushNextSampleIntoFifo(float sample, float* fifo, int& index, bool& ready);

    std::vector<std::unique_ptr<CompressorBand>> bands;
    juce::dsp::ProcessSpec currentSpec;

    // FFT members
    static constexpr int fftOrder = 11;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    
    float fifo[1 << fftOrder];
    float fftData[1 << (fftOrder + 1)];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    float scopeData[1 << fftOrder];

    float scFifo[1 << fftOrder];
    float scFftData[1 << (fftOrder + 1)];
    int scFifoIndex = 0;
    bool scNextFFTBlockReady = false;
    float scScopeData[1 << fftOrder];
};

} // namespace NovaMB
