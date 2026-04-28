#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MultibandEngine.h"

// --- Custom Editor Class (Geometric Balance UI - Platinum Version) ---
class NovaMBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    NovaMBEditor(juce::AudioProcessor& p, NovaMB::MultibandEngine& e) 
        : AudioProcessorEditor(p), engine(e) 
    {
        setSize(850, 550);
        startTimerHz(30); // 30fps for UI updates
    }

    void paint(juce::Graphics& g) override {
        // Aesthetic: Platinum Dark Carbon Theme
        auto bgPrimary = juce::Colour::fromFloatRGBA(0.04f, 0.05f, 0.06f, 1.0f);
        auto bgSecondary = juce::Colour::fromFloatRGBA(0.08f, 0.09f, 0.11f, 1.0f);
        g.fillAll(bgPrimary);

        auto area = getLocalBounds().toFloat();
        
        // --- Sidebar (Modular Panel: Navigation) ---
        auto sidebar = area.removeFromLeft(70);
        g.setColour(bgSecondary);
        g.fillRect(sidebar);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawVerticalLine((int)sidebar.getRight(), 0, area.getBottom());

        // --- Header ---
        auto header = area.removeFromTop(70);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font("Inter", 24.0f, juce::Font::bold));
        g.drawText("NOVAMB PRO", header.reduced(30, 0), juce::Justification::centredLeft);
        
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::plain));
        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.drawText("DIFFERENTIAL MULTIBAND ENGINE v2.5", header.reduced(30, 0), juce::Justification::centredRight);

        // --- Main Layout: Modular Panels ---
        auto body = area.reduced(24);
        
        // Panel 1: Spectral Analyzer (Top)
        auto analyzerArea = body.removeFromTop(body.getHeight() * 0.60f);
        drawModularPanel(g, analyzerArea, "REAL-TIME SPECTRAL ANALYSIS");
        
        auto analyzerContent = analyzerArea.reduced(10, 30);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(analyzerContent, 6.0f);
        
        drawReferenceGrids(g, analyzerContent);

        // Draw Band Overlays
        const juce::Colour bandColors[] = { 
            juce::Colour(0xff3b82f6), // Blue
            juce::Colour(0xffec4899), // Pink
            juce::Colour(0xff10b981)  // Green
        };

        float bandWidth = analyzerContent.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i) {
            auto bandArea = analyzerContent.withWidth(bandWidth).withX(analyzerContent.getX() + i * bandWidth);
            
            // Interaction Layer
            g.setColour(bandColors[i].withAlpha(0.08f));
            g.fillRect(bandArea.reduced(4, 0));
            
            // Neon Border (Top)
            g.setColour(bandColors[i]);
            g.fillRect(bandArea.withHeight(2.0f));
            
            // Gain Reduction Meter (Analog Style)
            float gr = engine.getGainReduction(i);
            if (gr < -0.1f) {
                // Using explicit float math functions (SonicMeter requirement)
                float grNormalized = std::fmin(std::abs(gr) / 20.0f, 1.0f);
                float grHeight = grNormalized * bandArea.getHeight();
                
                juce::ColourGradient grGrad(juce::Colours::red.withAlpha(0.4f), bandArea.getX(), bandArea.getY(),
                                           juce::Colours::red.withAlpha(0.1f), bandArea.getX(), bandArea.getBottom(), false);
                g.setGradientFill(grGrad);
                g.fillRect(bandArea.withHeight(grHeight).reduced(8, 0));
            }
        }

        // Panel 2: Digital Metrics (Bottom)
        body.removeFromTop(20); // Spacer
        auto metricsArea = body;
        drawModularPanel(g, metricsArea, "DIGITAL CORE METRICS");
        
        // Draw decorative technical details
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.setFont(juce::Font("JetBrains Mono", 10.0f, juce::Font::plain));
        g.drawText("LOCK: PHASE-LINEAR | MODE: ANALOG-MODELLING", metricsArea.reduced(20, 10), juce::Justification::bottomRight);
    }

    void drawModularPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::String title) {
        g.setColour(juce::Colour::fromFloatRGBA(0.12f, 0.13f, 0.15f, 0.4f));
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(area, 8.0f, 1.0f);
        
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::Font("Inter", 10.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), area.removeFromTop(30).reduced(15, 0), juce::Justification::centredLeft);
    }

    void drawReferenceGrids(juce::Graphics& g, juce::Rectangle<float> area) {
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        float stepX = area.getWidth() / 12.0f;
        for (int i = 1; i < 12; ++i) {
            g.drawVerticalLine((int)(area.getX() + i * stepX), area.getY(), area.getBottom());
        }
        float stepY = area.getHeight() / 6.0f;
        for (int i = 1; i < 6; ++i) {
            g.drawHorizontalLine((int)(area.getY() + i * stepY), area.getX(), area.getRight());
        }
    }

    void timerCallback() override {
        repaint();
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
    
    void releaseResources() override {}

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
