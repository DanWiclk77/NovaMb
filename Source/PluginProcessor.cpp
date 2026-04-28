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
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        
        bool isToggled = button.getToggleState();
        auto baseColor = isToggled ? juce::Colours::cyan.withAlpha (0.4f) : juce::Colour (0xff1a1a1a);
        
        g.setColour (baseColor);
        g.fillRoundedRectangle (bounds, cornerSize);
        
        g.setColour (isToggled ? juce::Colours::cyan : juce::Colours::white.withAlpha (0.1f));
        g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
        
        if (isToggled) {
            g.setColour(juce::Colours::cyan.withAlpha(0.3f));
            g.drawRoundedRectangle(bounds.expanded(1.0f), cornerSize, 2.5f);
            g.setColour(juce::Colours::cyan.withAlpha(0.15f));
            g.drawRoundedRectangle(bounds.expanded(2.5f), cornerSize, 5.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::Font font (getTextButtonFont (button, button.getHeight()));
        g.setFont (font);
        g.setColour (button.getToggleState() ? juce::Colours::white : juce::Colours::white.withAlpha (0.6f));

        auto yIndent = juce::jmin (4.0f, (float) button.getHeight() * 0.3f);
        auto cornerSize = juce::jmin (button.getWidth(), button.getHeight()) / 2.0f;

        auto fontHeight = font.getHeight();
        auto textBounds = button.getLocalBounds().withTrimmedTop ((button.getHeight() - fontHeight) / 2).withHeight (fontHeight);
        
        g.drawText (button.getButtonText(), textBounds, juce::Justification::centred, true);
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
        createButton(activeToggleButton, "ACTIVE");

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
        activeAttachment.reset();
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
        activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.getAPVTS(), getID("active"), activeToggleButton);
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
        auto sidebar = area.removeFromLeft(80);
        g.setColour(juce::Colour(0xff121316)); g.fillRect(sidebar);
        
        float pulse = std::sin((float)juce::Time::getMillisecondCounterHiRes() * 0.005f) * 0.2f + 0.8f;
        g.setColour(juce::Colours::cyan.withAlpha(pulse));
        g.fillEllipse(sidebar.getCentreX() - 15, 30, 30, 30);
        
        auto header = area.removeFromTop(80);
        g.setColour(juce::Colours::white); g.setFont(juce::Font("Inter", 32.0f, juce::Font::bold));
        g.drawText("NOVAMB PRO", header.reduced(30, 0), juce::Justification::centredLeft);
        
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::plain)); g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("GENETIC DYNAMICS ENGINE v3.5 | STUDIO EDITION", header.reduced(30, 0), juce::Justification::centredRight);

        auto body = area.reduced(24);
        auto analyzerArea = body.removeFromTop(body.getHeight() * 0.53f);
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
            bool isActive = processor.getAPVTS().getRawParameterValue(processor.getParamID(i, "active").getParamID())->load() > 0.5f;
            if (!isActive) continue;
            bool isSelected = (i == selectedBand);
            g.setColour(bandColors[i].withAlpha(isSelected ? 0.15f : 0.05f)); g.fillRect(bandRects[i]);
            g.setColour(bandColors[i].withAlpha(isSelected ? 1.0f : 0.4f)); g.fillRect(bandRects[i].withHeight(isSelected ? 4.0f : 1.0f));
            float gr = engine.getGainReduction(i);
            if (std::abs(gr) > 0.1f) {
                float grHeight = juce::jlimit(0.0f, analyzerDisplay.getHeight(), (std::abs(gr) / 24.0f) * analyzerDisplay.getHeight());
                g.setColour(juce::Colours::red.withAlpha(0.4f));
                g.fillRect(bandRects[i].withHeight(grHeight));
                
                g.setFont(juce::Font("Inter", 12.0f, juce::Font::bold));
                g.setColour(juce::Colours::white);
                g.drawText(juce::String(gr, 1) + " dB", bandRects[i].withHeight(20).withY(analyzerDisplay.getY() + grHeight + 5), juce::Justification::centredTop);
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawVerticalLine((int)x1, analyzerDisplay.getY(), analyzerDisplay.getBottom());
        g.drawVerticalLine((int)x2, analyzerDisplay.getY(), analyzerDisplay.getBottom());
        
        // Frequency Labels
        g.setFont(juce::Font("Inter", 11.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawText(juce::String((int)cross1) + "Hz", x1 - 30, analyzerDisplay.getBottom() + 2, 60, 20, juce::Justification::centred);
        g.drawText(juce::String((int)cross2) + "Hz", x2 - 30, analyzerDisplay.getBottom() + 2, 60, 20, juce::Justification::centred);

        // Band Frequencies
        g.setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawText("20Hz-" + juce::String((int)cross1) + "Hz", bandRects[0].withY(analyzerDisplay.getY() + 10).withHeight(20), juce::Justification::centred);
        g.drawText(juce::String((int)cross1) + "Hz-" + juce::String((int)cross2) + "Hz", bandRects[1].withY(analyzerDisplay.getY() + 10).withHeight(20), juce::Justification::centred);
        g.drawText(juce::String((int)cross2) + "Hz-20kHz", bandRects[2].withY(analyzerDisplay.getY() + 10).withHeight(20), juce::Justification::centred);

        g.setColour(juce::Colours::cyan);
        g.fillEllipse(x1 - 5, analyzerDisplay.getCentreY() - 5, 10, 10);
        g.fillEllipse(x2 - 5, analyzerDisplay.getCentreY() - 5, 10, 10);

        if (draggingCrossover > 0) {
            float activeX = (draggingCrossover == 1) ? x1 : x2;
            g.setColour(juce::Colours::cyan.withAlpha(0.3f));
            g.fillEllipse(activeX - 12, analyzerDisplay.getCentreY() - 12, 24, 24);
        }

        drawGRCurve(g, analyzerDisplay);

        body.removeFromTop(20);
        auto controlsArea = body;
        drawPanel(g, controlsArea, "BAND " + juce::String(selectedBand + 1) + " PARAMETERS");
        auto controlGrid = controlsArea.reduced(20, 20); 
        controlGrid.removeFromTop(35);
        
        g.setColour(juce::Colours::white.withAlpha(0.5f)); 
        g.setFont(juce::Font("Inter", 10.0f, juce::Font::bold));
        
        auto labelArea = controlGrid.removeFromTop(25);
        auto w = labelArea.getWidth() / 4.0f;
        juce::String labels[] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE" };
        for (int i = 0; i < 4; ++i) g.drawText(labels[i], labelArea.getX() + i * w, labelArea.getY(), w, 20, juce::Justification::centred);
        
        controlGrid.removeFromTop(controlGrid.getHeight() * 0.5f); // Knobs area
        
        auto botLabelArea = controlGrid.removeFromTop(25);
        auto bW = botLabelArea.getWidth() / 5.0f;
        g.drawText("MAKEUP", botLabelArea.getX(), botLabelArea.getY(), bW, 20, juce::Justification::centred);
        g.drawText("KNEE", botLabelArea.getX() + bW, botLabelArea.getY(), bW, 20, juce::Justification::centred);
        g.drawText("MODE/SIDECHAIN", botLabelArea.getX() + 2 * bW, botLabelArea.getY(), bW * 1.5f, 20, juce::Justification::centred);
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
        p.lineTo(r.getRight(), r.getBottom()); p.closeSubPath();
        g.setColour(isSidechain ? juce::Colours::orange.withAlpha(0.2f) : juce::Colours::cyan.withAlpha(0.25f));
        g.fillPath(p);
        g.setColour(isSidechain ? juce::Colours::orange.withAlpha(0.6f) : juce::Colours::cyan.withAlpha(0.7f));
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }

    void drawGRCurve(juce::Graphics& g, juce::Rectangle<float> r) {
        juce::Path p; p.startNewSubPath(r.getX(), r.getY());
        for (int i = 0; i < 3; ++i) {
            float gr = engine.getGainReduction(i); float dip = juce::jlimit(0.0f, 1.0f, std::abs(gr) / 24.0f) * 60.0f;
            float x1 = r.getX() + (i * r.getWidth() / 3.0f); float xM = x1 + r.getWidth() / 6.0f; float x2 = x1 + r.getWidth() / 3.0f;
            p.cubicTo(xM - 20, r.getY(), xM - 10, r.getY() + dip, xM, r.getY() + dip);
            p.cubicTo(xM + 10, r.getY() + dip, xM + 20, r.getY(), x2, r.getY());
        }
        g.setColour(juce::Colours::red.withAlpha(0.7f)); g.strokePath(p, juce::PathStrokeType(2.0f));
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, juce::String title) {
        g.setColour(juce::Colour(0xff1a1c21)); g.fillRoundedRectangle(r, 12.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawRoundedRectangle(r, 12.0f, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.65f)); g.setFont(juce::Font("Inter", 11.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), r.removeFromTop(35).reduced(20, 0), juce::Justification::centredLeft);
    }

    void drawGrids(juce::Graphics& g, juce::Rectangle<float> r) {
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        for (int i = 1; i < 12; ++i) g.drawVerticalLine((int)(r.getX() + i * r.getWidth() / 12.0f), r.getY(), r.getBottom());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        auto area = getLocalBounds().toFloat().reduced(24);
        area.removeFromLeft(80); // Correct for sidebar
        auto analyzerArea = area.removeFromTop(area.getHeight() * 0.53f);
        auto analyzerDisplay = analyzerArea.reduced(2, 35);

        if (analyzerDisplay.contains(e.position)) {
            float cross1 = processor.getAPVTS().getRawParameterValue("cross_low_mid")->load();
            float cross2 = processor.getAPVTS().getRawParameterValue("cross_mid_high")->load();
            
            auto freqToX = [&](float f) {
                float norm = (std::log10(f) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
                return analyzerDisplay.getX() + norm * analyzerDisplay.getWidth();
            };

            if (std::abs(e.position.x - freqToX(cross1)) < 30.0f) draggingCrossover = 1;
            else if (std::abs(e.position.x - freqToX(cross2)) < 30.0f) draggingCrossover = 2;
            else {
                float relX = (e.position.x - analyzerDisplay.getX()) / analyzerDisplay.getWidth();
                float f = std::pow(10.0f, std::log10(20.0f) + relX * (std::log10(20000.0f) - std::log10(20.0f)));
                if (f < cross1) selectBand(0);
                else if (f < cross2) selectBand(1);
                else selectBand(2);
                draggingCrossover = 0;
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (draggingCrossover == 0) return;
        auto area = getLocalBounds().toFloat().reduced(24);
        area.removeFromLeft(80); // Correct for sidebar
        auto analyzerArea = area.removeFromTop(area.getHeight() * 0.53f);
        auto analyzerDisplay = analyzerArea.reduced(2, 35);

        float relX = juce::jlimit(0.0f, 1.0f, (e.position.x - analyzerDisplay.getX()) / analyzerDisplay.getWidth());
        float f = std::pow(10.0f, std::log10(20.0f) + relX * (std::log10(20000.0f) - std::log10(20.0f)));
        
        auto& apvts = processor.getAPVTS();
        if (draggingCrossover == 1) {
            float c2 = apvts.getRawParameterValue("cross_mid_high")->load();
            if (f < c2 - 50.0f) {
                auto* p = apvts.getParameter("cross_low_mid");
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(f));
            }
        } else if (draggingCrossover == 2) {
            float c1 = apvts.getRawParameterValue("cross_low_mid")->load();
            if (f > c1 + 50.0f) {
                auto* p = apvts.getParameter("cross_mid_high");
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(p->getNormalisableRange().snapToLegalValue(f)));
            }
        }
    }

    void mouseUp(const juce::MouseEvent&) override { draggingCrossover = 0; }
    void timerCallback() override { repaint(); }

    void resized() override {
        auto area = getLocalBounds().reduced(24); area.removeFromLeft(56); 
        presetButton.setBounds(10, 150, 60, 30); aiButton.setBounds(10, 200, 60, 30);
        
        area.removeFromTop(area.getHeight() * 0.58f); 
        auto controlsArea = area.reduced(20, 20); 
        controlsArea.removeFromTop(40);
        
        auto topHalf = controlsArea.removeFromTop(controlsArea.getHeight() * 0.45f);
        topHalf.removeFromTop(20); // Space for labels
        
        auto knobW = topHalf.getWidth() / 4;
        thresholdSlider.setBounds(topHalf.removeFromLeft(knobW).reduced(10));
        ratioSlider.setBounds(topHalf.removeFromLeft(knobW).reduced(10));
        attackSlider.setBounds(topHalf.removeFromLeft(knobW).reduced(10));
        releaseSlider.setBounds(topHalf.removeFromLeft(knobW).reduced(10));
        
        auto bottomHalf = controlsArea;
        bottomHalf.removeFromTop(20); // Space for labels
        
        auto botKnobW = bottomHalf.getWidth() / 5;
        makeupSlider.setBounds(bottomHalf.removeFromLeft(botKnobW).reduced(10));
        kneeSlider.setBounds(bottomHalf.removeFromLeft(botKnobW).reduced(10));
        
        auto midArea = bottomHalf.removeFromLeft(botKnobW * 1.5f).reduced(5);
        modeSelector.setBounds(midArea.removeFromTop(midArea.getHeight() * 0.5f).reduced(2));
        scSelector.setBounds(midArea.reduced(2));

        auto btnArea = bottomHalf.reduced(10, 0); 
        float btnH = btnArea.getHeight() / 4.0f;
        soloButton.setBounds(btnArea.removeFromTop(btnH).reduced(4));
        muteButton.setBounds(btnArea.removeFromTop(btnH).reduced(4));
        activeToggleButton.setBounds(btnArea.reduced(4));
    }

private:
    NovaMBAudioProcessor& processor; NovaMB::MultibandEngine& engine; PlatinumLookAndFeel platinumLF;
    int selectedBand = 1; int draggingCrossover = 0;
    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, makeupSlider, kneeSlider;
    juce::ComboBox modeSelector, scSelector;
    juce::TextButton soloButton, muteButton, activeToggleButton, aiButton, presetButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment, makeupAttachment, kneeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, muteAttachment, activeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, scAttachment;
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
    auto hzString = [](float v, int) { return juce::String(v, 0) + " Hz"; };

    // Crossovers
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cross_low_mid", 1), "Low-Mid Crossover", juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f, 0.4f), 200.0f, juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(hzString)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cross_mid_high", 1), "Mid-High Crossover", juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.4f), 5000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(hzString)));

        for (int i = 0; i < 3; ++i) {
            juce::String bandName = "Band " + juce::String(i + 1) + " ";
            params.push_back(std::make_unique<juce::AudioParameterBool>(getParamID(i, "active"), bandName + "Active", true));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "threshold"), bandName + "Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f, juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbString)));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "ratio"), bandName + "Ratio", juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 4.0f, juce::AudioParameterFloatAttributes().withLabel(":1").withStringFromValueFunction(ratioString)));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "attack"), bandName + "Attack", juce::NormalisableRange<float>(0.1f, 500.0f, 0.1f), 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "release"), bandName + "Release", juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f), 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "makeup"), bandName + "Makeup Gain", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbString)));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(getParamID(i, "knee"), bandName + "Knee", juce::NormalisableRange<float>(0.0f, 30.0f, 0.1f), 6.0f, juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbString)));
            params.push_back(std::make_unique<juce::AudioParameterBool>(getParamID(i, "solo"), bandName + "Solo", false));
            params.push_back(std::make_unique<juce::AudioParameterBool>(getParamID(i, "mute"), bandName + "Mute", false));
            params.push_back(std::make_unique<juce::AudioParameterChoice>(getParamID(i, "mode"), bandName + "Mode", juce::StringArray("Compress", "Expand"), 0));
            params.push_back(std::make_unique<juce::AudioParameterChoice>(getParamID(i, "sc-source"), bandName + "SC Source", juce::StringArray("Internal", "External"), 0));
        }
    return { params.begin(), params.end() };
}

void NovaMBAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, (juce::uint32)getTotalNumOutputChannels() };
    engine.prepare(spec);
}

void NovaMBAudioProcessor::releaseResources() {}

void NovaMBAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        float cross1 = apvts.getRawParameterValue("cross_low_mid")->load();
        float cross2 = apvts.getRawParameterValue("cross_mid_high")->load();

        for (int i = 0; i < 3; ++i) {
            NovaMB::BandParameters b;
            b.threshold = apvts.getRawParameterValue(getParamID(i, "threshold").getParamID())->load();
            b.ratio = apvts.getRawParameterValue(getParamID(i, "ratio").getParamID())->load();
            b.attack = apvts.getRawParameterValue(getParamID(i, "attack").getParamID())->load();
            b.release = apvts.getRawParameterValue(getParamID(i, "release").getParamID())->load();
            b.makeUpGain = apvts.getRawParameterValue(getParamID(i, "makeup").getParamID())->load();
            b.knee = apvts.getRawParameterValue(getParamID(i, "knee").getParamID())->load();
            b.solo = apvts.getRawParameterValue(getParamID(i, "solo").getParamID())->load() > 0.5f;
            b.mute = apvts.getRawParameterValue(getParamID(i, "mute").getParamID())->load() > 0.5f;
            b.active = apvts.getRawParameterValue(getParamID(i, "active").getParamID())->load() > 0.5f;
            
            int modeIdx = (int)apvts.getRawParameterValue(getParamID(i, "mode").getParamID())->load();
            b.mode = (modeIdx == 1) ? NovaMB::Mode::Expand : NovaMB::Mode::Compress;

            int scIdx = (int)apvts.getRawParameterValue(getParamID(i, "sc-source").getParamID())->load();
            b.sidechainSource = (scIdx == 1) ? NovaMB::SidechainSource::External : NovaMB::SidechainSource::Internal;

            if (i == 0) { b.frequencyLow = 20.0f; b.frequencyHigh = cross1; }
            else if (i == 1) { b.frequencyLow = cross1; b.frequencyHigh = cross2; }
            else { b.frequencyLow = cross2; b.frequencyHigh = 20000.0f; }

            engine.updateBand(i, b);
        }

        auto sidechainBus = getBus(true, 1);
        if (sidechainBus != nullptr && sidechainBus->isEnabled()) {
            auto scBuffer = getBusBuffer(buffer, true, 1);
            engine.process(buffer, scBuffer);
        } else {
            juce::AudioBuffer<float> emptySC(0, buffer.getNumSamples());
            engine.process(buffer, emptySC);
        }
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
