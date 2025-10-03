#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#if __has_include("BinaryData.h")
  #include "BinaryData.h"
  #define HAS_FROG_PNG 1
#else
  #define HAS_FROG_PNG 0
#endif

class SpectrogramComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit SpectrogramComponent (TelevisionAudioProcessor&);

    void paint    (juce::Graphics&) override;
    void resized  () override;

private:
    TelevisionAudioProcessor& audio;

    juce::Image spectrogramImage { juce::Image::RGB, TelevisionAudioProcessor::timeCols,
                                   TelevisionAudioProcessor::numBins, true };

    juce::Rectangle<int> crtBounds, screenBounds, panelBounds;

    juce::Image overlayImage;
    int lastOverlayW = 0, lastOverlayH = 0;

    juce::Image frogLogo;

    // Knobs
    juce::Slider sensitivitySlider, sineLevelSlider, dummySpeedSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sineAttach;

    void timerCallback() override;
    void updateSpectrogramImage();
    void layoutRects();
    void rebuildOverlayIfNeeded();
    void drawControlPanel (juce::Graphics& g);

    juce::Colour dbToWhitePink (float db, float dynDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramComponent)
};

class TelevisionAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TelevisionAudioProcessorEditor (TelevisionAudioProcessor&);
    ~TelevisionAudioProcessorEditor() override = default;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    TelevisionAudioProcessor& audioProcessor;
    SpectrogramComponent      content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelevisionAudioProcessorEditor)
};

