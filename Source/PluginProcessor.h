#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MultibandEngine.h"

class NovaMBAudioProcessor : public juce::AudioProcessor {
public:
    NovaMBAudioProcessor();
    ~NovaMBAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    
    // Analytics
    void getFFTData(float* dest) { engine.getFFTData(dest); }
    void getSidechainFFTData(float* dest) { engine.getSidechainFFTData(dest); }
    int getFFTSize() const { return engine.getFFTSize(); }

    static juce::ParameterID getParamID(int band, juce::String name);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    NovaMB::MultibandEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NovaMBAudioProcessor)
};
