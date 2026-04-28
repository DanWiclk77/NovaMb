#include "MultibandEngine.h"

namespace NovaMB {

// ---------------------------------------------------------------------------
// CompressorBand
// ---------------------------------------------------------------------------

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
    compressor.setRatio    (params.ratio);
    compressor.setAttack   (params.attack);
    compressor.setRelease  (params.release);

    expander.setThreshold(params.threshold);
    expander.setRatio    (params.ratio);
    expander.setAttack   (params.attack);
    expander.setRelease  (params.release);

    gain.setGainDecibels(params.makeUpGain);
}

void CompressorBand::process(juce::dsp::ProcessContextReplacing<float>& context,
                             const juce::AudioBuffer<float>& sidechainBuffer)
{
    if (parameters.bypassed || !parameters.active) return;

    // 1. Aplicar filtros de banda
    loPass.process(context);
    hiPass.process(context);

    auto& block = context.getOutputBlock();

    // 2. Medir nivel antes de la dinámica
    float before = 0.0f;
    if (block.getNumSamples() > 0) {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
            float rms = 0.0f;
            auto* ptr = block.getChannelPointer(ch);
            for (size_t s = 0; s < block.getNumSamples(); ++s)
                rms += ptr[s] * ptr[s];
            before += std::sqrt(rms / (float)block.getNumSamples());
        }
        before /= (float)block.getNumChannels();
    }

    const bool useSC = (parameters.sidechainSource == SidechainSource::External
                        && sidechainBuffer.getNumChannels() > 0
                        && sidechainBuffer.getNumSamples() > 0);

    if (parameters.mode == Mode::Compress) {
        if (useSC) {
            // Calcular ratio de ganancia del sidechain y aplicarlo a la señal principal
            juce::AudioBuffer<float> scCopy;
            scCopy.makeCopyOf(sidechainBuffer);
            juce::dsp::AudioBlock<float>              scBlock(scCopy);
            juce::dsp::ProcessContextReplacing<float> scCtx(scBlock);
            compressor.process(scCtx);

            float scBefore = 0.0f, scAfter = 0.0f;
            for (size_t ch = 0; ch < scBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                for (size_t s = 0; s < scBlock.getNumSamples(); ++s) {
                    sumA += scBlock.getChannelPointer(ch)[s] * scBlock.getChannelPointer(ch)[s];
                    sumB += sidechainBuffer.getReadPointer((int)ch)[s]
                          * sidechainBuffer.getReadPointer((int)ch)[s];
                }
                scAfter  += std::sqrt(sumA / (float)scBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)scBlock.getNumSamples());
            }
            float gr = (scBefore > 1e-4f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(gr);
        } else {
            compressor.process(context);
        }
    } else { // Mode::Expand
        if (useSC) {
            juce::AudioBuffer<float> scCopy;
            scCopy.makeCopyOf(sidechainBuffer);
            juce::dsp::AudioBlock<float>              scBlock(scCopy);
            juce::dsp::ProcessContextReplacing<float> scCtx(scBlock);
            expander.process(scCtx);

            float scBefore = 0.0f, scAfter = 0.0f;
            for (size_t ch = 0; ch < scBlock.getNumChannels(); ++ch) {
                float sumA = 0.0f, sumB = 0.0f;
                for (size_t s = 0; s < scBlock.getNumSamples(); ++s) {
                    sumA += scBlock.getChannelPointer(ch)[s] * scBlock.getChannelPointer(ch)[s];
                    sumB += sidechainBuffer.getReadPointer((int)ch)[s]
                          * sidechainBuffer.getReadPointer((int)ch)[s];
                }
                scAfter  += std::sqrt(sumA / (float)scBlock.getNumSamples());
                scBefore += std::sqrt(sumB / (float)scBlock.getNumSamples());
            }
            float gr = (scBefore > 1e-4f) ? (scAfter / scBefore) : 1.0f;
            block.multiplyBy(gr);
        } else {
            expander.process(context);
        }
    }

    // 3. Medir nivel después y calcular GR
    float after = 0.0f;
    if (block.getNumSamples() > 0) {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
            float rms = 0.0f;
            auto* ptr = block.getChannelPointer(ch);
            for (size_t s = 0; s < block.getNumSamples(); ++s)
                rms += ptr[s] * ptr[s];
            after += std::sqrt(rms / (float)block.getNumSamples());
        }
        after /= (float)block.getNumChannels();
    }

    lastReduction = (before > 1e-4f)
                  ? juce::Decibels::gainToDecibels(after / before)
                  : 0.0f;

    // 4. Makeup gain
    gain.process(context);
}

float CompressorBand::getGainReduction() const { return lastReduction; }

// ---------------------------------------------------------------------------
// MultibandEngine
// ---------------------------------------------------------------------------

MultibandEngine::MultibandEngine()
    : fft(fftOrder),
      window(getFFTSize(), juce::dsp::WindowingFunction<float>::hann)
{
    for (int i = 0; i < 3; ++i)
        bands.push_back(std::make_unique<CompressorBand>());
}

void MultibandEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    for (auto& band : bands)
        band->prepare(spec);
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer,
                              const juce::AudioBuffer<float>& sidechainBuffer)
{
    // Alimentar FIFO del espectro principal
    const float* mainRead = buffer.getReadPointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        pushNextSampleIntoFifo(mainRead[i], fifo, fifoIndex, nextFFTBlockReady);

    // Alimentar FIFO del sidechain
    if (sidechainBuffer.getNumChannels() > 0 && sidechainBuffer.getNumSamples() > 0) {
        const float* scRead = sidechainBuffer.getReadPointer(0);
        int n = juce::jmin(buffer.getNumSamples(), sidechainBuffer.getNumSamples());
        for (int i = 0; i < n; ++i)
            pushNextSampleIntoFifo(scRead[i], scFifo, scFifoIndex, scNextFFTBlockReady);
    }

    // Buffer de salida (suma de bandas)
    juce::AudioBuffer<float> outputBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    outputBuffer.clear();

    bool anySolo = false;
    for (auto& band : bands) if (band->isSolo()) { anySolo = true; break; }

    for (auto& band : bands) {
        if (band->isMute() || (anySolo && !band->isSolo()) || !band->isActive())
            continue;

        juce::AudioBuffer<float>                  bandBuffer;
        bandBuffer.makeCopyOf(buffer);
        juce::dsp::AudioBlock<float>              block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);

        band->process(ctx, sidechainBuffer);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            outputBuffer.addFrom(ch, 0, bandBuffer, ch, 0, buffer.getNumSamples());
    }

    buffer.makeCopyOf(outputBuffer);
}

void MultibandEngine::updateBand(int index, const BandParameters& params) {
    if (index >= 0 && index < (int)bands.size())
        bands[index]->updateParameters(params);
}

float MultibandEngine::getGainReduction(int index) const {
    if (index >= 0 && index < (int)bands.size())
        return bands[index]->getGainReduction();
    return 0.0f;
}

void MultibandEngine::pushNextSampleIntoFifo(float sample, float* f,
                                             int& idx, bool& ready)
{
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
        window.multiplyWithWindowingTable(fftData, (size_t)getFFTSize());
        fft.performFrequencyOnlyForwardTransform(fftData);
        for (int i = 0; i < getFFTSize() / 2; ++i)
            scopeData[i] = fftData[i];
        nextFFTBlockReady = false;
    }
    memcpy(dest, scopeData, sizeof(float) * getFFTSize() / 2);
}

void MultibandEngine::getSidechainFFTData(float* dest) {
    if (scNextFFTBlockReady) {
        window.multiplyWithWindowingTable(scFftData, (size_t)getFFTSize());
        fft.performFrequencyOnlyForwardTransform(scFftData);
        for (int i = 0; i < getFFTSize() / 2; ++i)
            scScopeData[i] = scFftData[i];
        scNextFFTBlockReady = false;
    }
    memcpy(dest, scScopeData, sizeof(float) * getFFTSize() / 2);
}

} // namespace NovaMB
