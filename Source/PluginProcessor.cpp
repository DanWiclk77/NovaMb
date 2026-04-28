#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// --- Custom Editor Class (Geometric Balance UI - Platinum Version) ---
class NovaMBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    NovaMBEditor(NovaMBAudioProcessor& p, NovaMB::MultibandEngine& e) 
        : AudioProcessorEditor(p), processor(p), engine(e) 
    {
        setSize(850, 550);
        startTimerHz(30); 
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
        
        // Drawing Status Indicators
        g.setColour(juce::Colours::cyan.withAlpha(0.6f));
        g.fillEllipse(header.getX() + 15, header.getY() + 15, 6.0f, 6.0f);
        
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::plain));
        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.drawText("DIFFERENTIAL MULTIBAND ENGINE v2.5.1 [PLATINUM]", header.reduced(30, 0), juce::Justification::centredRight);

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
            
            g.setColour(bandColors[i].withAlpha(0.08f));
            g.fillRect(bandArea.reduced(4, 0));
            
            g.setColour(bandColors[i]);
            g.fillRect(bandArea.withHeight(2.0f));
            
            float gr = engine.getGainReduction(i);
            if (gr < -0.1f) {
                float grNormalized = std::fmin(std::abs(gr) / 20.0f, 1.0f);
                float grHeight = grNormalized * bandArea.getHeight();
                
                juce::ColourGradient grGrad(juce::Colours::red.withAlpha(0.4f), bandArea.getX(), bandArea.getY(),
                                           juce::Colours::red.withAlpha(0.1f), bandArea.getX(), bandArea.getBottom(), false);
                g.setGradientFill(grGrad);
                g.fillRect(bandArea.withHeight(grHeight).reduced(8, 0));
            }
        }

        // Panel 2: Digital Metrics (Bottom)
        body.removeFromTop(20); 
        auto metricsArea = body;
        drawModularPanel(g, metricsArea, "DIGITAL CORE METRICS");
        
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
    NovaMBAudioProcessor& processor;
    NovaMB::MultibandEngine& engine;
};

// --- Processor Implementation ---

NovaMBAudioProcessor::NovaMBAudioProcessor() 
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

NovaMBAudioProcessor::~NovaMBAudioProcessor() {}

const juce::String NovaMBAudioProcessor::getName() const { return "NovaMB"; }
bool NovaMBAudioProcessor::acceptsMidi() const { return false; }
bool NovaMBAudioProcessor::producesMidi() const { return false; }
double NovaMBAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int NovaMBAudioProcessor::getNumPrograms() override { return 1; }
int NovaMBAudioProcessor::getCurrentProgram() override { return 0; }
void NovaMBAudioProcessor::setCurrentProgram(int) override {}
const juce::String NovaMBAudioProcessor::getProgramName(int) override { return {}; }
void NovaMBAudioProcessor::changeProgramName(int, const juce::String&) override {}

juce::ParameterID NovaMBAudioProcessor::getParamID(int band, juce::String name) {
    return juce::ParameterID(juce::String(band) + "_" + name, 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout NovaMBAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    for (int i = 0; i < 3; ++i) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "threshold"), "Threshold " + juce::String(i+1), -60.0f, 0.0f, -20.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "ratio"), "Ratio " + juce::String(i+1), 1.0f, 20.0f, 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "attack"), "Attack " + juce::String(i+1), 0.1f, 500.0f, 20.0f));
        params.push_back(std::make_unique<juce::AudioProcessorParameterGroup>(getParamID(i, "group"), "Band " + juce::String(i+1), "|", 
            std::make_unique<juce::AudioParameterFloat>(getParamID(i, "release"), "Release " + juce::String(i+1), 10.0f, 2000.0f, 100.0f)));
    }
    // Correction: actually let's just push them all normally for simplicity in this turn
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> finalParams;
    for (int i = 0; i < 3; ++i) {
        finalParams.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "threshold"), "Threshold " + juce::String(i+1), -60.0f, 0.0f, -20.0f));
        finalParams.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "ratio"), "Ratio " + juce::String(i+1), 1.0f, 20.0f, 4.0f));
        finalParams.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "attack"), "Attack " + juce::String(i+1), 0.1f, 500.0f, 20.0f));
        finalParams.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "release"), "Release " + juce::String(i+1), 10.0f, 2000.0f, 100.0f));
    }
    return { finalParams.begin(), finalParams.end() };
}

void NovaMBAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, (juce::uint32)getTotalNumOutputChannels() };
    engine.prepare(spec);
}

void NovaMBAudioProcessor::releaseResources() {}

void NovaMBAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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

juce::AudioProcessorEditor* NovaMBAudioProcessor::createEditor() {
    return new NovaMBEditor(*this, engine);
}

bool NovaMBAudioProcessor::hasEditor() const { return true; }

void NovaMBAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NovaMBAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr) apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// Entry point for JUCE plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new NovaMBAudioProcessor();
}

