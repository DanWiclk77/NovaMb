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

void CompressorBand::process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer) {
    if (parameters.bypassed || !parameters.active) return;

    // 1. Filter band
    loPass.process(context);
    hiPass.process(context);
    
    // 2. Measure level before compression
    float before = 0.0f;
    auto& block = context.getOutputBlock();
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
        float sum = 0.0f;
        auto* chPtr = block.getChannelPointer(ch);
        for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
        before += std::sqrt(sum / (float)block.getNumSamples());
    }
    before /= (float)block.getNumChannels();

    if (parameters.sidechainExternal && sidechainBuffer.getNumSamples() > 0) {
        // Use external sidechain: Process sidechain buffer with compressor to get gain reduction
        juce::AudioBuffer<float> scCopy;
        scCopy.makeCopyOf(sidechainBuffer);
        juce::dsp::AudioBlock<float> scBlock(scCopy);
        juce::dsp::ProcessContextReplacing<float> scContext(scBlock);
        compressor.process(scContext);
        
        // Measure sidechain gain reduction
        float scAfter = 0.0f;
        for (size_t ch = 0; ch < scBlock.getNumChannels(); ++ch) {
            float sum = 0.0f;
            auto* chPtr = scBlock.getChannelPointer(ch);
            for (size_t s = 0; s < scBlock.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
            scAfter += std::sqrt(sum / (float)scBlock.getNumSamples());
        }
        scAfter /= (float)scBlock.getNumChannels();
        
        // Measure sidechain before (simple rms)
        float scBefore = 0.0f;
        for (size_t ch = 0; ch < sidechainBuffer.getNumChannels(); ++ch) {
            float sum = 0.0f;
            auto* chPtr = sidechainBuffer.getReadPointer(ch);
            for (size_t s = 0; s < (size_t)sidechainBuffer.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
            scBefore += std::sqrt(sum / (float)sidechainBuffer.getNumSamples());
        }
        scBefore /= (float)sidechainBuffer.getNumChannels();

        float gainReduction = (scBefore > 0.0001f) ? (scAfter / scBefore) : 1.0f;
        block.multiplyBy(gainReduction);
    } else {
        compressor.process(context);
    }
    
    float after = 0.0f;
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
        float sum = 0.0f;
        auto* chPtr = block.getChannelPointer(ch);
        for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
        after += std::sqrt(sum / (float)block.getNumSamples());
    }
    after /= (float)block.getNumChannels();
    
    lastReduction = (before > 0.0001f) ? juce::Decibels::gainToDecibels(after / before) : 0.0f;
    
    // 3. Makeup Gain
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

    if (sidechainBuffer.getNumSamples() > 0 && sidechainBuffer.getNumChannels() > 0) {
        auto* scRead = sidechainBuffer.getReadPointer(0);
        int scSamples = juce::jmin(buffer.getNumSamples(), sidechainBuffer.getNumSamples());
        for (int i = 0; i < scSamples; ++i) {
            pushNextSampleIntoFifo(scRead[i], scFifo, scFifoIndex, scNextFFTBlockReady);
        }
    }

    juce::AudioBuffer<float> outputBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    outputBuffer.clear();

    bool anySolo = false;
    for (auto& band : bands) if (band->isSolo()) anySolo = true;

    for (int i = 0; i < (int)bands.size(); ++i) {
        auto& band = bands[i];
        if (band->isMute() || (anySolo && !band->isSolo()) || !band->isActive()) continue;

        juce::AudioBuffer<float> bandBuffer;
        bandBuffer.makeCopyOf(buffer);
        
        juce::dsp::AudioBlock<float> block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        band->process(context, sidechainBuffer);
        
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            outputBuffer.addFrom(ch, 0, bandBuffer, ch, 0, buffer.getNumSamples());
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
