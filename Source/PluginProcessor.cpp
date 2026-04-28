#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// --- Custom LookAndFeel (Platinum Pro Design) ---
class PlatinumLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PlatinumLookAndFeel() {
        setColour(juce::Slider::thumbColourId, juce::Colours::cyan);
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.7f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle,
                         juce::Slider& slider) override {
        auto outline = juce::Colour::fromFloatRGBA(0.15f, 0.16f, 0.18f, 1.0f);
        auto fill = juce::Colours::cyan.withAlpha(0.8f);

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = 3.5f;
        auto arcRadius = radius - lineW * 0.5f;

        juce::Path backgroundArc;
        backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(outline);
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0) {
            juce::Path valueArc;
            valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
            g.setColour(fill);
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Pointer
        juce::Path p;
        auto pointerLength = radius * 0.4f;
        auto pointerThickness = 2.0f;
        p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(bounds.getCentreX(), bounds.getCentreY()));
        g.setColour(juce::Colours::white);
        g.fillPath(p);
    }
};

// --- Custom Editor Class ---
class NovaMBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    NovaMBEditor(NovaMBAudioProcessor& p, NovaMB::MultibandEngine& e) 
        : AudioProcessorEditor(p), processor(p), engine(e) 
    {
        setLookAndFeel(&platinumLF);
        
        setupSliders();
        selectBand(1); // Default to Mid band
        
        setSize(950, 650);
        startTimerHz(30); 
    }

    ~NovaMBEditor() override {
        setLookAndFeel(nullptr);
    }

    void setupSliders() {
        auto createSlider = [this](juce::Slider& s, juce::String name) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
            s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
            addAndMakeVisible(s);
        };

        createSlider(thresholdSlider, "THR");
        createSlider(ratioSlider, "RAT");
        createSlider(attackSlider, "ATK");
        createSlider(releaseSlider, "REL");
    }

    void selectBand(int bandIndex) {
        selectedBand = juce::jlimit(0, 2, bandIndex);
        
        // Refresh attachments for the selected band
        thresholdAttachment.reset();
        ratioAttachment.reset();
        attackAttachment.reset();
        releaseAttachment.reset();

        auto getID = [this](juce::String name) { return processor.getParamID(selectedBand, name).getParamID(); };

        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("threshold"), thresholdSlider);
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("ratio"), ratioSlider);
        attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("attack"), attackSlider);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("release"), releaseSlider);

        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bg = juce::Colour(0xff0a0b0d);
        g.fillAll(bg);

        auto area = getLocalBounds().toFloat();
        
        // Navigation Sidebar
        auto sidebar = area.removeFromLeft(80);
        g.setColour(juce::Colour(0xff121316));
        g.fillRect(sidebar);
        
        // Logo Accent
        g.setColour(juce::Colours::cyan);
        g.fillEllipse(sidebar.getCentreX() - 15, 30, 30, 30);
        
        // Header
        auto header = area.removeFromTop(80);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font("Inter", 28.0f, juce::Font::bold));
        g.drawText("NOVAMB PRO", header.reduced(30, 0), juce::Justification::centredLeft);
        
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::plain));
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("DIFFERENTIAL ENGINE v2.6 | PLATINUM EDITION", header.reduced(30, 0), juce::Justification::centredRight);

        // --- Spectral Analyzer ---
        auto body = area.reduced(24);
        auto analyzerArea = body.removeFromTop(body.getHeight() * 0.58f);
        
        drawPanel(g, analyzerArea, "REAL-TIME SPECTRAL DYNAMICS");
        auto analyzerDisplay = analyzerArea.reduced(2, 35);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRoundedRectangle(analyzerDisplay, 6.0f);
        
        drawGrids(g, analyzerDisplay);

        // Bands UI
        const juce::Colour bandColors[] = { juce::Colour(0xff3b82f6), juce::Colour(0xffec4899), juce::Colour(0xff10b981) };
        juce::String bandNames[] = { "LOW", "MID", "HIGH" };
        
        float bandW = analyzerDisplay.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i) {
            auto bArea = analyzerDisplay.withWidth(bandW).withX(analyzerDisplay.getX() + i * bandW);
            bool isSelected = (i == selectedBand);
            
            // Subtle Band Highlight
            g.setColour(bandColors[i].withAlpha(isSelected ? 0.12f : 0.03f));
            g.fillRect(bArea.reduced(2, 0));
            
            // Band Top Accent
            g.setColour(bandColors[i].withAlpha(isSelected ? 1.0f : 0.5f));
            g.fillRect(bArea.withHeight(isSelected ? 3.0f : 1.0f));

            // Band Name Label
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.setFont(juce::Font("Inter", 10.0f, juce::Font::bold));
            g.drawText(bandNames[i], bArea.withHeight(25).translated(0, 5), juce::Justification::centred);

            // Gain Reduction Drawing & Numerical Display
            float gr = engine.getGainReduction(i);
            if (std::abs(gr) > 0.05f) {
                // Visual Meter
                float grH = std::fmin(std::abs(gr) * 12.0f, bArea.getHeight());
                g.setColour(juce::Colours::red.withAlpha(0.25f));
                g.fillRect(bArea.withHeight(grH));

                // Numerical GR Value (Pro Feedback)
                g.setColour(juce::Colours::red.withAlpha(0.9f));
                g.setFont(juce::Font("JetBrains Mono", 13.0f, juce::Font::bold));
                g.drawText(juce::String(gr, 1) + " dB", bArea.withHeight(30).withY(bArea.getBottom() - 35), juce::Justification::centred);
            }

            if (isSelected) {
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                g.drawVerticalLine((int)bArea.getCentreX(), bArea.getY() + 30, bArea.getBottom() - 35);
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.fillEllipse(bArea.getCentreX() - 4, bArea.getCentreY() - 4, 8, 8);
            }
        }

        // --- Controls Section ---
        body.removeFromTop(24);
        auto controlsArea = body;
        drawPanel(g, controlsArea, "BAND " + juce::String(selectedBand + 1) + " CONTROLS");
        
        auto labelArea = controlsArea.reduced(80, 40).withHeight(20);
        auto w = labelArea.getWidth() / 4;
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::bold));
        
        juce::String labels[] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE" };
        for (int i = 0; i < 4; ++i) {
            g.drawText(labels[i], labelArea.removeFromLeft(w), juce::Justification::centred);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(24);
        area.removeFromTop(area.getHeight() * 0.65f); 
        
        auto controlGrid = area.reduced(80, 40);
        controlGrid.removeFromTop(25); // Make room for labels
        auto w = controlGrid.getWidth() / 4;
        
        thresholdSlider.setBounds(controlGrid.removeFromLeft(w).reduced(10));
        ratioSlider.setBounds(controlGrid.removeFromLeft(w).reduced(10));
        attackSlider.setBounds(controlGrid.removeFromLeft(w).reduced(10));
        releaseSlider.setBounds(controlGrid.removeFromLeft(w).reduced(10));
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, juce::String title) {
        // High-end Glassmorphism with Gloss
        juce::ColourGradient grad(juce::Colour(0xff1a1c21), r.getX(), r.getY(),
                                 juce::Colour(0xff121316), r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, 12.0f);
        
        // Glossy Reflective top edge
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(r, 12.0f, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine((int)r.getY(), r.getX() + 10, r.getRight() - 10);
        
        g.setColour(juce::Colours::white.withAlpha(0.65f));
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), r.removeFromTop(35).reduced(20, 0), juce::Justification::centredLeft);
    }

    void drawGrids(juce::Graphics& g, juce::Rectangle<float> r) {
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        float stepX = r.getWidth() / 12.0f;
        for (int i = 1; i < 12; ++i) g.drawVerticalLine((int)(r.getX() + i * stepX), r.getY(), r.getBottom());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        auto body = getLocalBounds().toFloat().reduced(24);
        auto analyzerArea = body.removeFromTop(getHeight() * 0.45f);
        if (analyzerArea.contains(e.position)) {
            float relX = (e.position.x - analyzerArea.getX()) / analyzerArea.getWidth();
            selectBand(juce::jlimit(0, 2, (int)(relX * 3)));
        }
    }

    void timerCallback() override { repaint(); }

private:
    NovaMBAudioProcessor& processor;
    NovaMB::MultibandEngine& engine;
    PlatinumLookAndFeel platinumLF;
    int selectedBand = 1;

    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NovaMBEditor)
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
int NovaMBAudioProcessor::getNumPrograms() { return 1; }
int NovaMBAudioProcessor::getCurrentProgram() { return 0; }
void NovaMBAudioProcessor::setCurrentProgram(int) {}
const juce::String NovaMBAudioProcessor::getProgramName(int) { return {}; }
void NovaMBAudioProcessor::changeProgramName(int, const juce::String&) {}

juce::ParameterID NovaMBAudioProcessor::getParamID(int band, juce::String name) {
    return juce::ParameterID(juce::String(band) + "_" + name, 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout NovaMBAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    auto dbString = [](float v, int) { return juce::String(v, 1) + " dB"; };
    auto ratioString = [](float v, int) { return juce::String(v, 1) + ":1"; };
    auto msString = [](float v, int) { return juce::String(v, 0) + " ms"; };

    for (int i = 0; i < 3; ++i) {
        juce::String bandName = "Band " + juce::String(i + 1) + " ";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "threshold"), bandName + "Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f, juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbString)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "ratio"), bandName + "Ratio", juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 4.0f, juce::AudioParameterFloatAttributes().withLabel(":1").withStringFromValueFunction(ratioString)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "attack"), bandName + "Attack", juce::NormalisableRange<float>(0.1f, 500.0f, 0.1f), 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "release"), bandName + "Release", juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f), 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));
    }
    return { params.begin(), params.end() };
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

