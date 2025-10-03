#include "PluginEditor.h"
#include <cmath>

// ======================= SpectrogramComponent ==========================
SpectrogramComponent::SpectrogramComponent (TelevisionAudioProcessor& p)
    : audio (p)
{
   #if HAS_FROG_PNG
    frogLogo = juce::ImageFileFormat::loadFrom (BinaryData::Frog_png, BinaryData::Frog_pngSize);
   #endif

    auto setupSlider = [this](juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.0);
        s.setAlpha (0.0f);
        addAndMakeVisible (s);
    };

    setupSlider (sensitivitySlider);
    setupSlider (sineLevelSlider);
    setupSlider (dummySpeedSlider);

    sensAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audio.apvts, "sensitivity", sensitivitySlider);

    sineAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audio.apvts, "sineLevel", sineLevelSlider);

    startTimerHz (45);
}

void SpectrogramComponent::resized()
{
    layoutRects();
    rebuildOverlayIfNeeded();
}

void SpectrogramComponent::layoutRects()
{
    auto r = getLocalBounds().reduced (100, 40);
    crtBounds = r;

    const int bezel = juce::jmax (18, crtBounds.getWidth() / 22);
    screenBounds = crtBounds.reduced (bezel);

    const int panelW = juce::jmax (24, screenBounds.getWidth() / 9);
    panelBounds = screenBounds.withX (screenBounds.getRight() - panelW)
                              .withWidth (panelW);
}

void SpectrogramComponent::rebuildOverlayIfNeeded()
{
    const int specW = screenBounds.getWidth() - panelBounds.getWidth();
    const int specH = screenBounds.getHeight();

    if (specW == lastOverlayW && specH == lastOverlayH)
        return;

    lastOverlayW = specW; lastOverlayH = specH;

    overlayImage = juce::Image (juce::Image::ARGB, specW, specH, true);
    juce::Graphics g (overlayImage);

    g.fillAll (juce::Colours::transparentBlack);
    juce::ColourGradient grad (juce::Colours::black.withAlpha (0.45f), (float) (specW * 0.5), 0.0f,
                               juce::Colours::transparentBlack, (float) (specW * 0.5), (float) (specH * 0.6), false);
    grad.addColour (0.2, juce::Colours::black.withAlpha (0.25f));
    grad.addColour (0.6, juce::Colours::transparentBlack);
    g.setGradientFill (grad);
    g.fillAll();

    g.setColour (juce::Colours::black.withAlpha (0.06f));
    for (int y = 0; y < specH; y += 2)
        g.fillRect (0, y, specW, 1);
}

juce::Colour SpectrogramComponent::dbToWhitePink (float db, float dynDb)
{
    float t = juce::jlimit (0.0f, 1.0f, (db + dynDb) / dynDb);
    t *= (float) sensitivitySlider.getValue();

    auto lerp = [] (float a, float b, float u) { return a + (b - a) * u; };
    float r = 1.0f;
    float g = lerp (1.0f, 0.20f, t);
    float b = lerp (1.0f, 0.65f, t);
    return juce::Colour::fromFloatRGBA (r, g, b, 1.0f);
}

void SpectrogramComponent::updateSpectrogramImage()
{
    std::vector<float> latestSlice;
    audio.getLatestSpectrum (latestSlice);
    if (latestSlice.empty())
        return;

    const int numBins = (int) latestSlice.size();
    const float dynDb = audio.getDynDb();

    if (spectrogramImage.getWidth()  != audio.getTimeBins()
     || spectrogramImage.getHeight() != numBins)
        spectrogramImage = juce::Image (juce::Image::RGB, audio.getTimeBins(), numBins, true);

    const int w = spectrogramImage.getWidth();
    const int h = spectrogramImage.getHeight();

    // Scroll and clear new column to white
    spectrogramImage.moveImageSection (0, 0, 1, 0, w - 1, h);
    juce::Graphics g (spectrogramImage);
    g.setColour (juce::Colours::white);
    g.fillRect (w - 1, 0, 1, h);

    const int x = w - 1;
    // Input spectrum (pink/white)
    for (int y = 0; y < numBins; ++y)
    {
        const float mag = latestSlice[(size_t) y];
        const float db  = (mag > 1.0e-12f ? 20.0f * std::log10 (mag) : -dynDb * 2.0f);
        g.setColour (dbToWhitePink (db, dynDb));
        g.fillRect (x, (numBins - 1) - y, 1, 1);
    }

    // Overlay sine spectrum (white → green depending on level)
    std::vector<float> sineSlice;
    audio.getLatestSineSpectrum (sineSlice);
    if (! sineSlice.empty())
    {
        const float dynDb = audio.getDynDb();
        for (int y = 0; y < numBins; ++y)
        {
            const float mag = sineSlice[(size_t) y];
            if (mag > 1.0e-12f)
            {
                float db = 20.0f * std::log10 (mag);
                if (db > -60.0f)
                {
                    // map dB to 0–1 range
                    float t = juce::jlimit (0.0f, 1.0f, (db + dynDb) / dynDb);

                    // interpolate white → green
                    float r = 1.0f;
                    float gcol = 1.0f - (1.0f - 0.0f) * t; // fades red down
                    float gval = juce::jmap (t, 0.0f, 1.0f, 1.0f, 1.0f); // stays 1
                    float bcol = 1.0f - t; // fades blue down

                    juce::Colour c = juce::Colour::fromFloatRGBA (r * (1.0f - t),
                                                                  gval,
                                                                  bcol,
                                                                  1.0f);
                    // simpler: lerp white→green
                    c = juce::Colour::fromFloatRGBA (1.0f - t, 1.0f, 1.0f - t, 1.0f);

                    g.setColour (c);
                    g.fillRect (x, (numBins - 1) - y, 1, 1);
                }
            }
        }
    }
}

void SpectrogramComponent::timerCallback()
{
    updateSpectrogramImage();
    repaint();
}

void SpectrogramComponent::drawControlPanel (juce::Graphics& g)
{
    auto workingArea = panelBounds;

    g.setColour (juce::Colour::fromRGB (235, 235, 235));
    g.fillRect (workingArea);

    g.setColour (juce::Colours::black);
    g.drawLine ((float) workingArea.getX(), (float) screenBounds.getY(),
                (float) workingArea.getX(), (float) screenBounds.getBottom(), 3.0f);

    auto logoArea = workingArea.removeFromTop (workingArea.getHeight() / 4).reduced (0, 4);
   #if HAS_FROG_PNG
    if (! frogLogo.isNull())
        g.drawImageWithin (frogLogo, logoArea.getX(), logoArea.getY(),
                           logoArea.getWidth(), logoArea.getHeight(),
                           juce::RectanglePlacement::centred, false);
   #endif

    auto knobZone = workingArea.removeFromBottom (workingArea.getHeight() * 0.65f);
    const int knobDiam = juce::jmax (12, workingArea.getWidth() - 26);
    const int spacing  = (knobZone.getHeight() - (3 * knobDiam)) / 4;
    int ky             = knobZone.getY() + spacing;
    const int cx       = knobZone.getCentreX();
    const int r        = knobDiam / 2;

    auto drawKnob = [&] (int cy, juce::Slider& slider)
    {
        juce::Colour c1 = juce::Colour::fromRGB (70, 70, 70);
        juce::Colour c2 = juce::Colour::fromRGB (110, 110, 110);
        juce::ColourGradient kg (c2, (float) cx, (float) (cy - r),
                                 c1, (float) cx, (float) (cy + r), false);
        g.setGradientFill (kg);
        g.fillEllipse ((float) (cx - r), (float) (cy - r), (float) knobDiam, (float) knobDiam);

        g.setColour (juce::Colours::black);
        g.drawEllipse ((float) (cx - r), (float) (cy - r), (float) knobDiam, (float) knobDiam, 2.0f);

        const float minA = juce::MathConstants<float>::pi * 0.75f;
        const float maxA = juce::MathConstants<float>::pi * 2.25f;

        auto tick = [&] (float a)
        {
            const float inner = r * 1.05f;
            const float outer = r * 1.20f;
            g.setColour (juce::Colours::black);
            g.drawLine ((float) cx + inner * std::cos (a),
                        (float) cy + inner * std::sin (a),
                        (float) cx + outer * std::cos (a),
                        (float) cy + outer * std::sin (a), 2.0f);
        };
        tick (minA);
        tick (maxA);

        float angle = juce::jmap ((float) slider.getValue(), 0.0f, 1.0f, minA, maxA);
        const float len = r * 0.65f;
        g.setColour (juce::Colours::white);
        g.drawLine ((float) cx, (float) cy,
                    (float) cx + len * std::cos (angle),
                    (float) cy + len * std::sin (angle), 2.0f);

        slider.setBounds (juce::Rectangle<int> (cx - r, cy - r, knobDiam, knobDiam));
    };

    drawKnob (ky + r, sensitivitySlider); ky += knobDiam + spacing;
    drawKnob (ky + r, sineLevelSlider);   ky += knobDiam + spacing;
    drawKnob (ky + r, dummySpeedSlider);
}

void SpectrogramComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);

    // ===== CRT body =====
    juce::Path body;
    const float bodyRadius = juce::jmin (crtBounds.getWidth(), crtBounds.getHeight()) * 0.08f;
    body.addRoundedRectangle (crtBounds.toFloat(), bodyRadius);

    juce::ColourGradient outerGrad (juce::Colour::fromRGB (245, 245, 245),
                                    (float) crtBounds.getX(), (float) crtBounds.getY(),
                                    juce::Colour::fromRGB (225, 225, 225),
                                    (float) crtBounds.getRight(), (float) crtBounds.getBottom(), false);
    g.setGradientFill (outerGrad);
    g.fillPath (body);

    g.setColour (juce::Colours::black);
    g.strokePath (body, juce::PathStrokeType (5.0f));

    // ===== Stand =====
    {
        const int baseHeight = 10;
        const int cornerR    = 6;

        int standWidth = (int) (crtBounds.getWidth() * 0.6f);
        int standX     = crtBounds.getCentreX() - standWidth / 2;
        auto baseBounds = juce::Rectangle<int> (standX, crtBounds.getBottom(),
                                                standWidth, baseHeight);

        juce::Path basePath;
        basePath.startNewSubPath ((float) baseBounds.getX(), (float) baseBounds.getY());
        basePath.lineTo ((float) baseBounds.getRight(), (float) baseBounds.getY());
        basePath.lineTo ((float) baseBounds.getRight(), (float) baseBounds.getBottom() - cornerR);
        basePath.quadraticTo ((float) baseBounds.getRight(), (float) baseBounds.getBottom(),
                              (float) baseBounds.getRight() - cornerR, (float) baseBounds.getBottom());
        basePath.lineTo ((float) baseBounds.getX() + cornerR, (float) baseBounds.getBottom());
        basePath.quadraticTo ((float) baseBounds.getX(), (float) baseBounds.getBottom(),
                              (float) baseBounds.getX(), (float) baseBounds.getBottom() - cornerR);
        basePath.lineTo ((float) baseBounds.getX(), (float) baseBounds.getY());
        basePath.closeSubPath();

        g.setColour (juce::Colour::fromRGB (90, 90, 90));
        g.fillPath (basePath);

        g.setColour (juce::Colours::black);
        g.strokePath (basePath, juce::PathStrokeType (5.0f));
    }

    // ===== Screen =====
    auto inlay = screenBounds.expanded (10);
    g.setColour (juce::Colour::fromRGB (210, 210, 210));
    g.fillRoundedRectangle (inlay.toFloat(), bodyRadius * 0.5f);

    const float screenRadius = bodyRadius * 0.55f;
    juce::Path glass; glass.addRoundedRectangle (screenBounds.toFloat(), screenRadius);

    g.setColour (juce::Colours::white);
    g.fillPath (glass);

    g.saveState();
    g.reduceClipRegion (glass);

    auto specBounds = screenBounds.withRight (panelBounds.getX());
    if (! spectrogramImage.isNull())
    {
        g.drawImageWithin (spectrogramImage,
                           specBounds.getX(), specBounds.getY(),
                           specBounds.getWidth(), specBounds.getHeight(),
                           juce::RectanglePlacement::stretchToFit,
                           false);
    }

    if (! overlayImage.isNull())
        g.drawImageAt (overlayImage, specBounds.getX(), specBounds.getY());

    drawControlPanel (g);

    g.restoreState();

    g.setColour (juce::Colours::black);
    g.strokePath (glass, juce::PathStrokeType (4.0f));
}

// ======================= Editor (window) ================================
TelevisionAudioProcessorEditor::TelevisionAudioProcessorEditor (TelevisionAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), content (p)
{
    setSize (900, 600);
    addAndMakeVisible (content);
}

void TelevisionAudioProcessorEditor::resized()
{
    content.setBounds (getLocalBounds());
}

void TelevisionAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
}

