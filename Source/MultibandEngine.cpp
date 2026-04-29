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
}

void CompressorBand::updateParameters(const BandParameters& params) {
    parameters = params;
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
    if (parameters.bypassed || !parameters.active) return;

    // 1. Filter band
    loPass.process(context);
    hiPass.process(context);
    
    // 2. Measure level before
    float before = 0.0f;
    auto& block = context.getOutputBlock();
    if (block.getNumSamples() > 0) {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
            float sum = 0.0f;
            auto* chPtr = block.getChannelPointer(ch);
            for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
            before += std::sqrt(sum / (float)block.getNumSamples());
        }
        before /= (float)block.getNumChannels();
    }

    const bool useSC = (parameters.sidechainSource == SidechainSource::External && sidechainBuffer.getNumSamples() > 0 && sidechainBuffer.getNumChannels() > 0);

    if (parameters.mode == Mode::Compress) {
        if (useSC) {
            juce::AudioBuffer<float> scCopy;
            scCopy.makeCopyOf(sidechainBuffer);
            juce::dsp::AudioBlock<float> scBlock(scCopy);
            juce::dsp::ProcessContextReplacing<float> scContext(scBlock);
            compressor.process(scContext);
            
            float scAfter = 0.0f, scBefore = 0.0f;
            for (size_t ch = 0; ch < scBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                auto* ptrA = scBlock.getChannelPointer(ch);
                auto* ptrB = sidechainBuffer.getReadPointer((int)ch);
                for (size_t s = 0; s < scBlock.getNumSamples(); ++s) {
                    sumA += ptrA[s] * ptrA[s];
                    sumB += ptrB[s] * ptrB[s];
                }
                scAfter += std::sqrt(sumA / (float)scBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)scBlock.getNumSamples());
            }
            float gr = (scBefore > 0.0001f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(gr);
        } else {
            compressor.process(context);
        }
    } else { // Expand
        if (useSC) {
            juce::AudioBuffer<float> scCopy;
            scCopy.makeCopyOf(sidechainBuffer);
            juce::dsp::AudioBlock<float> scBlock(scCopy);
            juce::dsp::ProcessContextReplacing<float> scContext(scBlock);
            expander.process(scContext);
            
            float scAfter = 0.0f, scBefore = 0.0f;
            for (size_t ch = 0; ch < scBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                auto* ptrA = scBlock.getChannelPointer(ch);
                auto* ptrB = sidechainBuffer.getReadPointer((int)ch);
                for (size_t s = 0; s < scBlock.getNumSamples(); ++s) {
                    sumA += ptrA[s] * ptrA[s];
                    sumB += ptrB[s] * ptrB[s];
                }
                scAfter += std::sqrt(sumA / (float)scBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)scBlock.getNumSamples());
            }
            float gr = (scBefore > 0.0001f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(gr);
        } else {
            expander.process(context);
        }
    }
    
    float after = 0.0f;
    if (block.getNumSamples() > 0) {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
            float sum = 0.0f;
            auto* chPtr = block.getChannelPointer(ch);
            for (size_t s = 0; s < block.getNumSamples(); ++s) sum += chPtr[s] * chPtr[s];
            after += std::sqrt(sum / (float)block.getNumSamples());
        }
        after /= (float)block.getNumChannels();
    }
    
    lastReduction = (before > 0.0001f) ? juce::Decibels::gainToDecibels(after / before) : 0.0f;
    
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
    juce::zeromem(fifo, sizeof(float) * getFFTSize());
    juce::zeromem(fftData, sizeof(float) * getFFTSize() * 2);
    juce::zeromem(scopeData, sizeof(float) * (getFFTSize() / 2));
    juce::zeromem(scFifo, sizeof(float) * getFFTSize());
    juce::zeromem(scFftData, sizeof(float) * getFFTSize() * 2);
    juce::zeromem(scScopeData, sizeof(float) * (getFFTSize() / 2));
}

void MultibandEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    outputBuffer.setSize((int)spec.numChannels, (int)spec.maximumBlockSize);
    for (auto& band : bands) {
        band->prepare(spec);
    }
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer) {
    // Collect FFT Data
    const float* mainRead = buffer.getReadPointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        pushNextSampleIntoFifo(mainRead[i], fifo, fifoIndex, nextFFTBlockReady);
    }

    if (sidechainBuffer.getNumSamples() > 0 && sidechainBuffer.getNumChannels() > 0) {
        const float* scRead = sidechainBuffer.getReadPointer(0);
        int scSamples = juce::jmin(buffer.getNumSamples(), sidechainBuffer.getNumSamples());
        for (int i = 0; i < scSamples; ++i) {
            pushNextSampleIntoFifo(scRead[i], scFifo, scFifoIndex, scNextFFTBlockReady);
        }
    }

    outputBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    outputBuffer.clear();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    bool anySolo = false;
    for (auto& band : bands) if (band->isSolo()) { anySolo = true; break; }

    for (int i = 0; i < (int)bands.size(); ++i) {
        auto& band = bands[i];
        
        juce::AudioBuffer<float> bandBuffer;
        bandBuffer.makeCopyOf(buffer);
        
        juce::dsp::AudioBlock<float> block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        band->process(context, sidechainBuffer);
        
        bool isAudible = !band->isMute() && (!anySolo || band->isSolo()) && band->isActive();
        
        if (isAudible) {
            for (int ch = 0; ch < numChannels; ++ch) {
                outputBuffer.addFrom(ch, 0, bandBuffer, ch, 0, numSamples);
            }
        }
    }
    
    buffer.makeCopyOf(outputBuffer, true);
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
