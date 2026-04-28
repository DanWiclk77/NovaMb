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
    loPass.setCutoffFrequency(params.frequencyHigh);
    hiPass.setCutoffFrequency(params.frequencyLow);
    
    compressor.setThreshold(params.threshold);
    compressor.setRatio(params.ratio);
    compressor.setAttack(params.attack);
    compressor.setRelease(params.release);
    
    // Explicit float math function for decibel to gain conversion
    const float gainAmt = std::pow(10.0f, params.makeUpGain * 0.05f); 
    gain.setGainLinear(gainAmt);
}

void CompressorBand::process(juce::dsp::ProcessContextReplacing<float>& context, const juce::AudioBuffer<float>& sidechainBuffer) {
    // 1. Filtrar banda
    loPass.process(context);
    hiPass.process(context);
    
    // 2. Comprimir (en una implementación real de VST3, manejaríamos el sidechain externo aquí)
    compressor.process(context);
    
    // 3. Gain
    gain.process(context);
}

// engine implementation
MultibandEngine::MultibandEngine() {
    for (int i = 0; i < 3; ++i) { // Default 3 bands
        bands.push_back(std::make_unique<CompressorBand>());
    }
}

void MultibandEngine::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    for (auto& band : bands) {
        band->prepare(spec);
    }
}

void MultibandEngine::process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechainBuffer) {
    juce::AudioBuffer<float> outputBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    outputBuffer.clear();

    for (auto& band : bands) {
        // Creamos una copia del input para cada banda para procesar en paralelo
        juce::AudioBuffer<float> bandBuffer;
        bandBuffer.makeCopyOf(buffer);
        
        juce::dsp::AudioBlock<float> block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        band->process(context, sidechainBuffer);
        
        // Sumamos la banda procesada al output total
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            outputBuffer.addFrom(channel, 0, bandBuffer.getReadPointer(channel), buffer.getNumSamples());
        }
    }
    
    buffer.makeCopyOf(outputBuffer);
}

void MultibandEngine::updateBand(int index, const BandParameters& params) {
    if (index >= 0 && index < bands.size()) {
        bands[index]->updateParameters(params);
    }
}

} // namespace NovaMB
