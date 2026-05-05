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

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto cornerSize = 4.0f;
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        
        bool isToggled = button.getToggleState();
        juce::Colour baseColor;
        
        if (button.getButtonText().containsIgnoreCase("MUTE")) {
            baseColor = isToggled ? juce::Colours::red.withAlpha(0.7f) : juce::Colour(0xff1a1c21);
        } else if (button.getButtonText().containsIgnoreCase("SOLO")) {
            baseColor = isToggled ? juce::Colours::cyan.withAlpha(0.7f) : juce::Colour(0xff1a1c21);
        } else {
            baseColor = isToggled ? juce::Colours::white.withAlpha(0.18f) : juce::Colour(0xff1a1c21);
        }
        
        // Add subtle vertical gradient for "metal" feel
        juce::ColourGradient grad(baseColor.brighter(0.1f), 0, bounds.getY(),
                                 baseColor.darker(0.1f), 0, bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle (bounds, cornerSize);
        
        if (isToggled) {
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.fillRoundedRectangle(bounds, cornerSize);
            
            auto glowColor = button.getButtonText().containsIgnoreCase("MUTE") ? juce::Colours::red : juce::Colours::cyan;
            g.setColour(glowColor.withAlpha(0.4f));
            g.drawRoundedRectangle(bounds.expanded(1.0f), cornerSize, 1.8f);
        }

        g.setColour (isToggled ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle (bounds, cornerSize, 1.3f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        g.setFont (juce::Font("Inter", 12.0f, juce::Font::bold));
        g.setColour (button.getToggleState() ? juce::Colours::white : juce::Colours::white.withAlpha (0.6f));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
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
        selectBand(1);
        setSize(950, 700);
        startTimerHz(30); 
    }

    ~NovaMBEditor() override { setLookAndFeel(nullptr); }

    void setupSliders() {
        auto createSlider = [this](juce::Slider& s) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
            s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
            addAndMakeVisible(s);
        };

        createSlider(thresholdSlider); createSlider(ratioSlider);
        createSlider(attackSlider); createSlider(releaseSlider);
        createSlider(makeupSlider); createSlider(kneeSlider);

        auto createButton = [this](juce::TextButton& b, juce::String label) {
            b.setButtonText(label); b.setToggleable(true); addAndMakeVisible(b);
        };

        createButton(soloButton, "SOLO"); createButton(muteButton, "MUTE");

        addAndMakeVisible(modeSelector); modeSelector.addItem("COMPRESS", 1); modeSelector.addItem("EXPAND", 2);
        addAndMakeVisible(scSelector); scSelector.addItem("INTERNAL SC", 1); scSelector.addItem("EXTERNAL SC", 2);

        addAndMakeVisible(aiButton); aiButton.setButtonText("AI MAGIC");
        aiButton.onClick = [this] { runAIAssistant(); };

        addAndMakeVisible(presetButton); presetButton.setButtonText("PRESETS");
        presetButton.onClick = [this] { showPresetMenu(); };
    }

    void selectBand(int bandIndex) {
        selectedBand = juce::jlimit(0, 2, bandIndex);
        thresholdAttachment.reset(); ratioAttachment.reset();
        attackAttachment.reset(); releaseAttachment.reset();
        makeupAttachment.reset(); kneeAttachment.reset();
        soloAttachment.reset(); muteAttachment.reset();
        modeAttachment.reset(); scAttachment.reset();

        auto getID = [this](juce::String name) { return processor.getParamID(selectedBand, name).getParamID(); };

        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("threshold"), thresholdSlider);
        ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("ratio"), ratioSlider);
        attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("attack"), attackSlider);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("release"), releaseSlider);
        makeupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("makeup"), makeupSlider);
        kneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), getID("knee"), kneeSlider);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.getAPVTS(), getID("solo"), soloButton);
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.getAPVTS(), getID("mute"), muteButton);
        modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.getAPVTS(), getID("mode"), modeSelector);
        scAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.getAPVTS(), getID("sc-source"), scSelector);

        repaint();
    }

    void runAIAssistant() {
        auto& apvts = processor.getAPVTS();
        // Set "Smart" Pro Defaults based on band
        float targetThreshold = -18.0f;
        float targetRatio = 4.0f;
        float targetAttack = 30.0f;
        
        auto setParam = [&](juce::String name, float val) {
            if (auto* p = apvts.getParameter(processor.getParamID(selectedBand, name).getParamID()))
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(val));
        };

        setParam("threshold", targetThreshold);
        setParam("ratio", targetRatio);
        setParam("attack", targetAttack);
        setParam("makeup", 2.0f); // Slight boost
        setParam("knee", 6.0f);
    }

    void showPresetMenu() {
        juce::PopupMenu m;
        m.addItem(1, "Clean Master"); m.addItem(2, "Punchy Drums"); m.addItem(3, "Silky Vocals");
        
        juce::PopupMenu genreMenu;
        genreMenu.addItem(100, "Techno Peak"); genreMenu.addItem(101, "Melodic Techno");
        genreMenu.addItem(102, "House Classic"); genreMenu.addItem(103, "Tech House");
        genreMenu.addItem(104, "Acid House"); genreMenu.addItem(105, "Psytrance");
        genreMenu.addItem(106, "Uplifting Trance"); genreMenu.addItem(107, "Acid Techno");
        genreMenu.addItem(108, "Rock Arena"); genreMenu.addItem(109, "Jazz Club");
        genreMenu.addItem(110, "Blues Soul"); genreMenu.addItem(111, "Disco Disco");
        genreMenu.addItem(112, "Pop Radio"); genreMenu.addItem(113, "Hip Hop West");
        genreMenu.addItem(114, "Rap Mainstage"); genreMenu.addItem(115, "Future House");
        genreMenu.addItem(116, "Cyberpunk 2077"); genreMenu.addItem(117, "Drum & Bass Liquid");
        m.addSubMenu("Genres", genreMenu);
        
        juce::PopupMenu busMenu;
        busMenu.addItem(200, "Drum Bus Pro"); busMenu.addItem(201, "Vocal Bus Glue");
        busMenu.addItem(202, "Master Chain"); busMenu.addItem(203, "Mix Bus Air");
        m.addSubMenu("Buses", busMenu);
        
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
            if (result == 0) return;
            auto& apvts = processor.getAPVTS();
            auto setAll = [&](float t, float r, float a, float rel) {
                for (int i = 0; i < 3; ++i) {
                    auto setP = [&](juce::String n, float v) {
                        if (auto* p = apvts.getParameter(processor.getParamID(i, n).getParamID()))
                            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(v));
                    };
                    setP("threshold", t); setP("ratio", r); setP("attack", a); setP("release", rel);
                }
            };
            if (result == 1) setAll(-12, 1.5, 50, 200);
            else if (result == 2) setAll(-18, 4, 30, 100);
            else if (result == 3) setAll(-15, 3, 40, 150);
            else if (result == 100) setAll(-10, 2.5, 5, 40);    // Techno
            else if (result == 101) setAll(-12, 3, 20, 80);     // Melodic Techno
            else if (result == 102) setAll(-14, 2, 30, 100);    // House
            else if (result == 103) setAll(-13, 3, 25, 90);     // Tech House
            else if (result == 105) setAll(-16, 6, 10, 50);     // Psytrance
            else if (result == 106) setAll(-12, 1.8, 60, 250);  // Trance
            else if (result == 107) setAll(-11, 4, 15, 60);     // Acid
            else if (result == 108) setAll(-12, 3, 40, 150);    // Rock
            else if (result == 113) setAll(-14, 4, 20, 80);     // Hip Hop
            else if (result == 116) setAll(-15, 8, 2, 20);      // Cyberpunk
            else if (result == 117) setAll(-14, 5, 20, 60);     // D&B
            else if (result == 200) setAll(-24, 6, 40, 100);    // Drum Bus
            else if (result == 201) setAll(-10, 1.5, 100, 300); // Vocal Glue
            else if (result == 202) setAll(-4, 1.2, 80, 400);   // Master Chain
            else if (result == 203) setAll(-8, 2, 50, 250);     // Mix Bus
        });
    }

    void paint(juce::Graphics& g) override {
        auto bg = juce::Colour(0xff0a0b0d); g.fillAll(bg);
        auto area = getLocalBounds().toFloat();
        auto sidebar = area.removeFromLeft(100); 
        g.setColour(juce::Colour(0xff121316)); g.fillRect(sidebar);
        
        float pulse = std::sin((float)juce::Time::getMillisecondCounterHiRes() * 0.006f) * 0.35f + 0.65f;
        g.setColour(juce::Colours::cyan.withAlpha(pulse));
        g.fillEllipse(sidebar.getCentreX() - 15, 30, 30, 30);
        
        // Add a "core" to the sidebar icon
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(sidebar.getCentreX() - 4, 41, 8, 8);
        
        auto header = area.removeFromTop(80);
        g.setColour(juce::Colours::white); g.setFont(juce::Font("Inter", 32.0f, juce::Font::bold));
        g.drawText("NOVAMB PRO", header.reduced(30, 0), juce::Justification::centredLeft);
        
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::plain)); g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("GENETIC DYNAMICS ENGINE v3.5 | STUDIO EDITION", header.reduced(30, 0), juce::Justification::centredRight);

        auto body = area.reduced(24);
        auto analyzerArea = body.removeFromTop(body.getHeight() * 0.52f);
        drawPanel(g, analyzerArea, "PRECISION SPECTRAL ANALYZER");
        auto analyzerDisplay = analyzerArea.reduced(2, 35);
        g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillRoundedRectangle(analyzerDisplay, 6.0f);
        drawGrids(g, analyzerDisplay);

        drawSpectrum(g, analyzerDisplay, false);
        drawSpectrum(g, analyzerDisplay, true);

        float cross1 = processor.getAPVTS().getRawParameterValue("cross_low_mid")->load();
        float cross2 = processor.getAPVTS().getRawParameterValue("cross_mid_high")->load();

        auto freqToX = [&](float f) {
            float norm = (std::log10(f) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
            return analyzerDisplay.getX() + norm * analyzerDisplay.getWidth();
        };

        float x1 = freqToX(cross1); float x2 = freqToX(cross2);
        juce::Rectangle<float> bandRects[] = { analyzerDisplay.withRight(x1), analyzerDisplay.withLeft(x1).withRight(x2), analyzerDisplay.withLeft(x2) };
        const juce::Colour bandColors[] = { juce::Colour(0xff3b82f6), juce::Colour(0xffec4899), juce::Colour(0xff10b981) };

        for (int i = 0; i < 3; ++i) {
            bool isSelected = (i == selectedBand);
            g.setColour(bandColors[i].withAlpha(isSelected ? 0.25f : 0.08f)); g.fillRect(bandRects[i]);
            g.setColour(bandColors[i].withAlpha(isSelected ? 1.0f : 0.4f)); 
            g.fillRect(bandRects[i].withHeight(isSelected ? 4.0f : 1.2f));
            
            float gr = engine.getGainReduction(i);
            if (std::abs(gr) > 0.1f) {
                float grHeight = juce::jlimit(0.0f, analyzerDisplay.getHeight(), (std::abs(gr) / 32.0f) * analyzerDisplay.getHeight());
                
                juce::Rectangle<float> grBar = bandRects[i].withHeight(grHeight);
                
                juce::Colour grColor = juce::Colours::red.withAlpha(0.5f);
                g.setColour(grColor);
                g.fillRect(grBar);
                
                juce::ColourGradient grad(grColor.withAlpha(0.8f), 0, grBar.getY(),
                                         juce::Colours::transparentBlack, 0, grBar.getBottom(), false);
                g.setGradientFill(grad);
                g.fillRect(grBar);

                g.setFont(juce::Font("Inter", 12.0f, juce::Font::bold));
                g.setColour(juce::Colours::white);
                float textY = analyzerDisplay.getY() + grHeight + 4;
                if (textY > analyzerDisplay.getBottom() - 25) textY = analyzerDisplay.getBottom() - 25;
                g.drawText(juce::String(gr, 1) + " dB", bandRects[i].reduced(2,0).withY((int)textY).withHeight(20), juce::Justification::centredTop);
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawVerticalLine((int)x1, analyzerDisplay.getY(), analyzerDisplay.getBottom());
        g.drawVerticalLine((int)x2, analyzerDisplay.getY(), analyzerDisplay.getBottom());
        
        // Frequency Labels and circles
        g.setFont(juce::Font("Inter", 11.5f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawText(juce::String((int)cross1) + "Hz", (int)x1 - 35, (int)analyzerDisplay.getBottom() + 3, 70, 20, juce::Justification::centred);
        g.drawText(juce::String((int)cross2) + "Hz", (int)x2 - 35, (int)analyzerDisplay.getBottom() + 3, 70, 20, juce::Justification::centred);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(x1 - 5, analyzerDisplay.getCentreY() - 5, 10, 10);
        g.fillEllipse(x2 - 5, analyzerDisplay.getCentreY() - 5, 10, 10);

        if (draggingCrossover > 0) {
            float activeX = (draggingCrossover == 1) ? x1 : x2;
            g.setColour(juce::Colours::cyan.withAlpha(0.3f));
            g.fillEllipse(activeX - 12, analyzerDisplay.getCentreY() - 12, 24, 24);
        }

        body.removeFromTop(20);
        auto controlsArea = body;
        drawPanel(g, controlsArea, "BAND " + juce::String(selectedBand + 1) + " PARAMETERS");
        
        // Draw labels above knobs
        g.setFont(juce::Font("Inter", 10.5f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        
        auto drawLabelAbove = [&](juce::Component& c, juce::String label) {
            auto b = c.getBounds().toFloat();
            if (b.getWidth() > 0)
                g.drawText(label, b.getX(), b.getY() - 22, b.getWidth(), 15, juce::Justification::centred);
        };

        drawLabelAbove(thresholdSlider, "THRESHOLD");
        drawLabelAbove(ratioSlider, "RATIO");
        drawLabelAbove(attackSlider, "ATTACK");
        drawLabelAbove(releaseSlider, "RELEASE");
        drawLabelAbove(makeupSlider, "MAKEUP");
        drawLabelAbove(kneeSlider, "KNEE");

        auto botArea = controlsArea.reduced(25, 25).withTrimmedTop(controlsArea.getHeight() * 0.72f);
        auto bW = botArea.getWidth() / 5.0f;
        g.drawText("DYNAMICS MODE & SC", botArea.getX() + 2 * bW, botArea.getY() - 18, bW * 1.5f, 15, juce::Justification::centred);
        g.drawText("TOGGLES", botArea.getX() + 3.5f * bW, botArea.getY() - 18, bW * 1.5f, 15, juce::Justification::centred);
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> r, bool isSidechain) {
        std::vector<float> data(processor.getFFTSize() / 2);
        if (isSidechain) processor.getSidechainFFTData(data.data()); else processor.getFFTData(data.data());
        juce::Path p; p.startNewSubPath(r.getX(), r.getBottom());
        for (int i = 0; i < (int)data.size(); ++i) {
            float x = r.getX() + ((float)i / (float)data.size() * r.getWidth());
            float level = juce::jlimit(0.0f, 1.0f, juce::Decibels::gainToDecibels(data[i] + 0.0001f) / 100.0f + 1.0f);
            p.lineTo(x, r.getBottom() - (level * r.getHeight()));
        }
        // Smooth out the bottom
        p.lineTo(r.getRight(), r.getBottom());
        p.lineTo(r.getX(), r.getBottom());
        p.closeSubPath();

        if (isSidechain) {
            g.setColour(juce::
