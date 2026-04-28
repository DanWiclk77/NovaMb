#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MultibandEngine.h"

// --- Custom Editor Class (Geometric Balance UI) ---
class NovaMBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    NovaMBEditor(juce::AudioProcessor& p, NovaMB::MultibandEngine& e) 
        : AudioProcessorEditor(p), engine(e) 
    {
        setSize(800, 500);
        startTimerHz(30); // 30fps for UI updates
    }

    void paint(juce::Graphics& g) override {
        // Aesthetic: Dark Carbon / Geometric Balance
        auto bg = juce::Colour::fromFloatRGBA(0.06f, 0.07f, 0.08f, 1.0f);
        g.fillAll(bg);

        auto area = getLocalBounds().toFloat();
        
        // --- Sidebar (Glassmorphism) ---
        auto sidebar = area.removeFromLeft(70);
        g.setColour(juce::Colour::fromFloatRGBA(0.12f, 0.13f, 0.15f, 0.8f));
        g.fillRect(sidebar);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawVerticalLine((int)sidebar.getRight(), 0, area.getBottom());

        // --- Header ---
        auto header = area.removeFromTop(60);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(22.0f, juce::Font::bold));
        g.drawText("NOVAMB PRO", header.reduced(30, 0), juce::Justification::centredLeft);
        
        g.setFont(12.0f);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("ULTRA-PRECISION MULTIBAND", header.reduced(30, 0), juce::Justification::centredRight);

        // --- Main Area (Analyzer) ---
        auto body = area.reduced(20);
        auto analyzerArea = body.removeFromTop(body.getHeight() * 0.65f);
        
        // Background for Analyzer
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(analyzerArea, 10.0f);
        
        // Grids
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        float gridStep = analyzerArea.getWidth() / 10.0f;
        for (int i = 1; i < 10; ++i) {
            g.drawVerticalLine((int)(analyzerArea.getX() + i * gridStep), analyzerArea.getY(), analyzerArea.getBottom());
        }

        // Draw Band Overlays (Matching React colors)
        const juce::Colour bandColors[] = { 
            juce::Colour(0xff3b82f6), // Blue
            juce::Colour(0xffec4899), // Pink
            juce::Colour(0xff10b981)  // Green
        };

        float bandWidth = analyzerArea.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i) {
            auto bandArea = analyzerArea.withWidth(bandWidth).withX(analyzerArea.getX() + i * bandWidth);
            
            // Aura / Fill
            g.setColour(bandColors[i].withAlpha(0.1f));
            g.fillRect(bandArea.reduced(2, 0));
            
            // Top Indicator
            g.setColour(bandColors[i]);
            g.fillRect(bandArea.withHeight(3.0f));
            
            // Gain Reduction Indicator (Simulated)
            float gr = engine.getGainReduction(i); // Assume we added this to engine
            if (gr < -0.1f) {
                float grHeight = juce::jmin(std::abs(gr) * 5.0f, bandArea.getHeight());
                g.setColour(juce::Colours::red.withAlpha(0.3f));
                g.fillRect(bandArea.withHeight(grHeight).reduced(10, 0));
            }
        }

        // --- Controls Panel (Glassmorphism Cards) ---
        g.setColour(juce::Colour::fromFloatRGBA(0.15f, 0.16f, 0.18f, 0.5f));
        g.fillRoundedRectangle(body, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawRoundedRectangle(body, 10.0f, 1.0f);
        
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("DYNAMIC PARAMETERS", body.removeFromTop(40).reduced(20, 0), juce::Justification::centredLeft);
    }

    void timerCallback() override {
        repaint(); // Update meters etc
    }

private:
    NovaMB::MultibandEngine& engine;
};

// --- Main Processor Class ---
class NovaMBAudioProcessor : public juce::AudioProcessor {
public:
     NovaMBAudioProcessor() 
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)
            .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
          apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    {}

    static ParameterID getParamID(int band, juce::String name) { return ParameterID(juce::String(band) + "_" + name, 1); }

    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        for (int i = 0; i < 3; ++i) {
            params.push_back(std::make_unique<AudioParameterFloat>(getParamID(i, "threshold"), "Threshold " + juce::String(i+1), -60.0f, 0.0f, -20.0f));
            params.push_back(std::make_unique<AudioParameterFloat>(getParamID(i, "ratio"), "Ratio " + juce::String(i+1), 1.0f, 20.0f, 4.0f));
            params.push_back(std::make_unique<AudioParameterFloat>(getParamID(i, "attack"), "Attack " + juce::String(i+1), 0.1f, 500.0f, 20.0f));
            params.push_back(std::make_unique<AudioParameterFloat>(getParamID(i, "release"), "Release " + juce::String(i+1), 10.0f, 2000.0f, 100.0f));
        }
        return { params.begin(), params.end() };
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, (juce::uint32)getTotalNumOutputChannels() };
        engine.prepare(spec);
    }
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        // Sync parameters from APVTS to Engine
        for (int i = 0; i < 3; ++i) {
            NovaMB::BandParameters b;
            b.threshold = apvts.getRawParameterValue(getParamID(i, "threshold").getParamID())->load();
            b.ratio = apvts.getRawParameterValue(getParamID(i, "ratio").getParamID())->load();
            b.attack = apvts.getRawParameterValue(getParamID(i, "attack").getParamID())->load();
            b.release = apvts.getRawParameterValue(getParamID(i, "release").getParamID())->load();
            engine.updateBand(i, b);
        }

        auto sidechainBuffer = getBusBuffer(buffer, true, 1);
        engine.process(buffer, sidechainBuffer);
    }

    juce::AudioProcessorEditor* createEditor() override { return new NovaMBEditor(*this, engine); }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "NovaMB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    
    void getStateInformation(juce::MemoryBlock& destData) override {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }
    
    void setStateInformation(const void* data, int sizeInBytes) override {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr) apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }

private:
    NovaMB::MultibandEngine engine;
    AudioProcessorValueTreeState apvts;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NovaMBAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NovaMBAudioProcessor(); }
