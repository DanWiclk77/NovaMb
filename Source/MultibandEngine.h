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
    bool sidechainExternal = false;
};

class CompressorBand {
public:
    CompressorBand();
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer);
    void updateParameters(const BandParameters& params);
    float getGainReduction() const;

private:
    juce::dsp::LinkwitzRileyFilter<float> loPass, loPass2;
    juce::dsp::LinkwitzRileyFilter<float> hiPass, hiPass2;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> gain;
    float lastReduction = 0.0f;
};

class MultibandEngine {
public:
    MultibandEngine();
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer);
    void updateBand(int index, const BandParameters& params);
    float getGainReduction(int index) const;

private:
    std::vector<std::unique_ptr<CompressorBand>> bands;
    juce::dsp::ProcessSpec currentSpec;
};

} // namespace NovaMB
