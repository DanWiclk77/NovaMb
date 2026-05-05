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
    expander.prepare(spec);
    gain.prepare(spec);
    scBuffer.setSize((int)spec.numChannels, (int)spec.maximumBlockSize);
}

void CompressorBand::updateParameters(const BandParameters& params) {
    parameters = params;
    
    // We'll configure filters based on the frequencies in process if needed, 
    // but better to set them here. 
    // Note: Band 0 only needs loPass, Band 1 needs both, Band 2 only needs hiPass.
    // However, to keep it simple, we'll configure both and use them selectively.
    loPass.setCutoffFrequency(params.frequencyHigh);
    hiPass.setCutoffFrequency(params.frequencyLow);
    
    compressor.setThreshold(params.threshold);
    compressor.setRatio(params.ratio);
    compressor.setAttack(params.attack);
    compressor.setRelease(params.release);
    
    expander.setThreshold(params.threshold);
    expander.setRatio(params.ratio);
    expander.setAttack(params.attack);
    expander.setRelease(params.release);
    
    gain.setGainDecibels(params.makeUpGain);
}

void CompressorBand::process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer) {
    if (!parameters.active) return;
    
    // 1. Filter band. 
    // If it's a low band, frequencyLow is 20, so hiPass(20) does nothing. 
    // If it's a high band, frequencyHigh is 20000, so loPass(20000) does nothing.
    // However, to sum correctly with Linkwitz-Riley, we should ideally use nested splits.
    // For now, we use these as crossovers. 
    if (parameters.frequencyHigh < 19900.0f) loPass.process(context);
    if (parameters.frequencyLow > 21.0f) hiPass.process(context);
    
    if (parameters.bypassed) return;

    auto& block = context.getOutputBlock();
    if (block.getNumSamples() == 0) return;

    // 2. Measure level before
    float before = 0.0f;
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
        float sum = 0.0f;
        auto* chPtr = block.getChannelPointer(ch);
        for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
        before += std::sqrt(sum / (float)block.getNumSamples());
    }
    before /= (float)block.getNumChannels();

    const bool useSC = (parameters.sidechainSource == SidechainSource::External && sidechainBuffer.getNumSamples() >= (int)block.getNumSamples() && sidechainBuffer.getNumChannels() > 0);

    if (parameters.mode == Mode::Compress) {
        if (useSC) {
            // SC compression logic (no-allocation version)
            const int scChannels = juce::jmin((int)sidechainBuffer.getNumChannels(), (int)scBuffer.getNumChannels());
            const int samplesToCopy = (int)block.getNumSamples();
            
            scBuffer.clear();
            for (int ch = 0; ch < scChannels; ++ch)
                scBuffer.copyFrom(ch, 0, sidechainBuffer, ch, 0, samplesToCopy);
            
            juce::dsp::AudioBlock<float> scBlock(scBuffer);
            auto subScBlock = scBlock.getSubBlock(0, (size_t)samplesToCopy);
            juce::dsp::ProcessContextReplacing<float> scContext(subScBlock);
            compressor.process(scContext);
            
            float scAfter = 0.0f, scBefore = 0.0f;
            for (size_t ch = 0; ch < subScBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                auto* ptrA = subScBlock.getChannelPointer(ch);
                auto* ptrB = sidechainBuffer.getReadPointer((int)ch);
                for (size_t s = 0; s < subScBlock.getNumSamples(); ++s) {
                    sumA += ptrA[s] * ptrA[s]; sumB += ptrB[s] * ptrB[s];
                }
                scAfter += std::sqrt(sumA / (float)subScBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)subScBlock.getNumSamples());
            }
            float gr = (scBefore > 0.0001f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(juce::jlimit(0.0f, 1.0f, gr));
        } else {
            compressor.process(context);
        }
    } else { // Expand
        if (useSC) {
            // SC expansion (no-allocation)
            const int scChannels = juce::jmin((int)sidechainBuffer.getNumChannels(), (int)scBuffer.getNumChannels());
            const int samplesToCopy = (int)block.getNumSamples();
            
            scBuffer.clear();
            for (int ch = 0; ch < scChannels; ++ch)
                scBuffer.copyFrom(ch, 0, sidechainBuffer, ch, 0, samplesToCopy);
                
            juce::dsp::AudioBlock<float> scBlock(scBuffer);
            auto subScBlock = scBlock.getSubBlock(0, (size_t)samplesToCopy);
            juce::dsp::ProcessContextReplacing<float> scContext(subScBlock);
            expander.process(scContext);
            
            float scAfter = 0.0f, scBefore = 0.0f;
            for (size_t ch = 0; ch < subScBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                auto* ptrA = subScBlock.getChannelPointer(ch);
                auto* ptrB = sidechainBuffer.getReadPointer((int)ch);
                for (size_t s = 0; s < subScBlock.getNumSamples(); ++s) {
                    sumA += ptrA[s] * ptrA[s]; sumB += ptrB[s] * ptrB[s];
                }
                scAfter += std::sqrt(sumA / (float)subScBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)subScBlock.getNumSamples());
            }
            float gr = (scBefore > 0.0001f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(juce::jlimit(1.0f, 10.0f, gr));
        } else {
            expander.process(context);
        }
    }
    
    // Makeup gain
    gain.process(context);

    // Measure level after
    float after = 0.0f;
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
        float sum = 0.0f;
        auto* chPtr = block.getChannelPointer(ch);
        for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
        after += std::sqrt(sum / (float)block.getNumSamples());
    }
    after /= (float)block.getNumChannels();
    
    lastReduction = (before > 0.0001f) ? juce::Decibels::gainToDecibels(after / before) : 0.0f;
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
    juce::zeromem(fifo, sizeof(float) * getFFTSize());
    juce::zeromem(fftData, sizeof(float) * getFFTSize() * 2);
    juce::zeromem(scopeData, sizeof(float) * (getFFTSize() / 2));
    juce::zeromem(scFifo, sizeof(float) * getFFTSize());
    juce::zeromem(scFftData, sizeof(float) * getFFTSize() * 2);
    juce::zeromem(scScopeData, sizeof(float) * (getFFTSize() / 2));
}

void MultibandEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    sumBuffer.setSize((int)spec.numChannels, (int)spec.maximumBlockSize, false, true, true);
    
    bandBuffers.clear();
    for (int i = 0; i < (int)bands.size(); ++i) {
        bandBuffers.emplace_back((int)spec.numChannels, (int)spec.maximumBlockSize);
        bands[i]->prepare(spec);
    }
    
    sidechainCopy.setSize((int)spec.numChannels, (int)spec.maximumBlockSize, false, true, true);
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0) return;

    // FFT Data Collection
    const float* mainRead = buffer.getReadPointer(0);
    for (int i = 0; i < numSamples; ++i) {
        pushNextSampleIntoFifo(mainRead[i], fifo, fifoIndex, nextFFTBlockReady);
    }

    if (sidechainBuffer.getNumSamples() >= numSamples && sidechainBuffer.getNumChannels() > 0) {
        const float* scRead = sidechainBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i) {
            pushNextSampleIntoFifo(scRead[i], scFifo, scFifoIndex, scNextFFTBlockReady);
        }
    }

    bool anySolo = false;
    for (auto& band : bands) 
        if (band->isSolo()) { anySolo = true; break; }

    sumBuffer.clear(0, numSamples);

    for (int i = 0; i < (int)bands.size(); ++i) {
        auto& band = bands[i];
        
        // Use pre-allocated buffer for this band
        if (i >= (int)bandBuffers.size()) continue;
        auto& bBuf = bandBuffers[i];
        
        // Copy original signal to band buffer
        for (int ch = 0; ch < numChannels; ++ch)
            bBuf.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        
        juce::dsp::AudioBlock<float> block(bBuf);
        juce::dsp::ProcessContextReplacing<float> context(block.getSubBlock(0, (size_t)numSamples));
        
        band->process(context, sidechainBuffer);
        
        bool isAudible = !band->isMute() && (!anySolo || band->isSolo()) && band->isActive();
        
        if (isAudible) {
            for (int ch = 0; ch < numChannels; ++ch) {
                sumBuffer.addFrom(ch, 0, bBuf, ch, 0, numSamples);
            }
        }
    }
    
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom(ch, 0, sumBuffer, ch, 0, numSamples);
}

void MultibandEngine::pushNextSampleIntoFifo(float sample, float* f, int& idx, bool& ready) {
    if (idx == getFFTSize()) {
        if (!ready) {
            float* dst = (f == fifo) ? fftData : scFftData;
            juce::zeromem(dst, sizeof(float) * getFFTSize() * 2);
            memcpy(dst, f, sizeof(float) * getFFTSize());
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
