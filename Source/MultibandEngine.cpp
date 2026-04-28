#include "MultibandEngine.h"

namespace NovaMB {

CompressorBand::CompressorBand() {
    loPass.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    hiPass.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
}

void CompressorBand::prepare(const juce::dsp::ProcessSpec& spec) {
    loPass.prepare(spec);
    hiPass.prepare(spec);
    compressor.prepare(spec);
    gain.prepare(spec);
}

void CompressorBand::updateParameters(const BandParameters& params) {
    parameters = params;
    loPass.setCutoffFrequency(params.frequencyHigh);
    hiPass.setCutoffFrequency(params.frequencyLow);
    
    compressor.setThreshold(params.threshold);
    compressor.setRatio(params.ratio);
    compressor.setAttack(params.attack);
    compressor.setRelease(params.release);
    
    const float gainAmt = std::pow(10.0f, params.makeUpGain * 0.05f); 
    gain.setGainLinear(gainAmt);
}

void CompressorBand::process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer, bool useSidechain) {
    if (parameters.bypassed) return;

    // 1. Filter band
    loPass.process(context);
    hiPass.process(context);
    
    // 2. Compress
    float before = 0.0f;
    for (size_t ch = 0; ch < context.getInputBlock().getNumChannels(); ++ch)
        before += context.getInputBlock().getRMSLevel(ch);
    before /= (float)context.getInputBlock().getNumChannels();

    if (useSidechain && sidechainBuffer.getNumChannels() > 0) {
        // Simple sidechain implementation: use sidechain RMS to drive compression
        // In a real pro plugin, we'd use a dedicated sidechain-capable compressor but juce::dsp::Compressor is internal detection only
        // Standard practice for sidechain in JUCE dsp::Compressor is to use a separate detection path if needed.
        // For now, let's just use internal detection as juce::dsp::Compressor doesn't expose a sidechain input easily.
        compressor.process(context);
    } else {
        compressor.process(context);
    }
    
    float after = 0.0f;
    for (size_t ch = 0; ch < context.getOutputBlock().getNumChannels(); ++ch)
        after += context.getOutputBlock().getRMSLevel(ch);
    after /= (float)context.getOutputBlock().getNumChannels();
    
    if (before > 0.00001f) {
        float currentTarget = juce::Decibels::gainToDecibels(after / before);
        lastReduction = lastReduction * 0.85f + currentTarget * 0.15f;
    } else {
        lastReduction *= 0.9f;
    }
    
    // 3. Gain
    gain.process(context);
}

float CompressorBand::getGainReduction() const {
    return lastReduction;
}

// engine implementation
MultibandEngine::MultibandEngine() 
    : fft(fftOrder), window(getFFTSize(), juce::dsp::WindowingFunction<float>::hann)
{
    for (int i = 0; i < 3; ++i) { 
        bands.push_back(std::make_unique<CompressorBand>());
    }
    juce::zeromem(fifo, sizeof(fifo));
    juce::zeromem(fftData, sizeof(fftData));
    juce::zeromem(scopeData, sizeof(scopeData));
    juce::zeromem(scFifo, sizeof(scFifo));
    juce::zeromem(scFftData, sizeof(scFftData));
    juce::zeromem(scScopeData, sizeof(scScopeData));
}

void MultibandEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    for (auto& band : bands) {
        band->prepare(spec);
    }
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer) {
    // Collect FFT Data
    auto mainRead = buffer.getReadPointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        pushNextSampleIntoFifo(mainRead[i], fifo, fifoIndex, nextFFTBlockReady);
    }

    if (sidechainBuffer.getNumChannels() > 0) {
        auto scRead = sidechainBuffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            pushNextSampleIntoFifo(scRead[i], scFifo, scFifoIndex, scNextFFTBlockReady);
        }
    }

    juce::AudioBuffer<float> outputBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    outputBuffer.clear();

    bool anySolo = false;
    for (auto& band : bands) if (band->isSolo()) anySolo = true;

    for (int i = 0; i < (int)bands.size(); ++i) {
        auto& band = bands[i];
        if (band->isMute() || (anySolo && !band->isSolo())) continue;

        juce::AudioBuffer<float> bandBuffer;
        bandBuffer.makeCopyOf(buffer);
        
        juce::dsp::AudioBlock<float> block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        band->process(context, sidechainBuffer, true);
        
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            outputBuffer.addFrom(channel, 0, bandBuffer.getReadPointer(channel), buffer.getNumSamples());
        }
    }
    
    buffer.makeCopyOf(outputBuffer);
}

void MultibandEngine::pushNextSampleIntoFifo(float sample, float* f, int& idx, bool& ready) {
    if (idx == getFFTSize()) {
        if (!ready) {
            juce::zeromem(f == fifo ? fftData : scFftData, sizeof(float) * getFFTSize() * 2);
            memcpy(f == fifo ? fftData : scFftData, f, sizeof(float) * getFFTSize());
            ready = true;
        }
        idx = 0;
    }
    f[idx++] = sample;
}

void MultibandEngine::getFFTData(float* dest) {
    if (nextFFTBlockReady) {
        window.multiplyWithWindowingTable(fftData, getFFTSize());
        fft.performFrequencyOnlyForwardTransform(fftData);
        for (int i = 0; i < getFFTSize() / 2; ++i) scopeData[i] = fftData[i];
        nextFFTBlockReady = false;
    }
    memcpy(dest, scopeData, sizeof(float) * getFFTSize() / 2);
}

void MultibandEngine::getSidechainFFTData(float* dest) {
    if (scNextFFTBlockReady) {
        window.multiplyWithWindowingTable(scFftData, getFFTSize());
        fft.performFrequencyOnlyForwardTransform(scFftData);
        for (int i = 0; i < getFFTSize() / 2; ++i) scScopeData[i] = scFftData[i];
        scNextFFTBlockReady = false;
    }
    memcpy(dest, scScopeData, sizeof(float) * getFFTSize() / 2);
}

void MultibandEngine::updateBand(int index, const BandParameters& params) {
    if (index >= 0 && index < (int)bands.size()) {
        bands[index]->updateParameters(params);
    }
}

float MultibandEngine::getGainReduction(int index) const {
    if (index >= 0 && index < (int)bands.size()) {
        return bands[index]->getGainReduction();
    }
    return 0.0f;
}

} // namespace NovaMB
