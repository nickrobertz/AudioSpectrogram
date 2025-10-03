#include "PluginProcessor.h"
#include "PluginEditor.h"

TelevisionAudioProcessor::TelevisionAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
#else
    : apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
#endif
{
    latestMagnitudes.assign (numBins, 0.0f);
    latestSineMagnitudes.assign (numBins, 0.0f);
    monoFifo.clear();
    sineFifo.clear();
}

TelevisionAudioProcessor::~TelevisionAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
TelevisionAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "sensitivity", "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 1.0f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "sineLevel", "Sine Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

void TelevisionAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSR = sampleRate;
    monoFifo.clear();
    sineFifo.clear();
    samplesSinceLastFFT = 0;
    phase = 0.0;
}

void TelevisionAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TelevisionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto set = layouts.getMainOutputChannelSet();
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
#endif

void TelevisionAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float* left  = buffer.getReadPointer (0);
    const float* right = (numChannels > 1 ? buffer.getReadPointer (1) : nullptr);

    // Feed input FFT
    pushAudioToFifo (left, right, numSamples);
    runFFTIfReady();

    // Sine generation
    float sineLevel = getSineLevel() * 0.2f; // scaled down
    double phaseInc = juce::MathConstants<double>::twoPi * 440.0 / currentSR;

    std::vector<float> sineBuffer ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float s = std::sin (phase) * sineLevel;
        sineBuffer[(size_t) i] = s;

        phase += phaseInc;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) + s);
    }

    // Feed sine FFT
    pushSineToFifo (sineBuffer.data(), numSamples);
    runSineFFTIfReady();
}

void TelevisionAudioProcessor::pushAudioToFifo (const float* left, const float* rightOrNull, int numSamples)
{
    monoFifo.resize (monoFifo.size() + (size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float s = rightOrNull ? 0.5f * (left[i] + rightOrNull[i]) : left[i];
        monoFifo[monoFifo.size() - numSamples + (size_t) i] = s;
    }

    samplesSinceLastFFT += numSamples;
}

void TelevisionAudioProcessor::runFFTIfReady()
{
    while (samplesSinceLastFFT >= hopSize && (int) monoFifo.size() >= fftSize)
    {
        const size_t startIdx = monoFifo.size() - fftSize;

        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = monoFifo[startIdx + (size_t) i];

        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        {
            std::scoped_lock lk (magLock);
            if ((int) latestMagnitudes.size() != numBins)
                latestMagnitudes.assign (numBins, 0.0f);

            for (int i = 0; i < numBins; ++i)
                latestMagnitudes[(size_t) i] = fftData[(size_t) i];
        }

        monoFifo.erase (monoFifo.begin(), monoFifo.begin() + hopSize);
        samplesSinceLastFFT -= hopSize;
    }
}

void TelevisionAudioProcessor::pushSineToFifo (const float* samples, int numSamples)
{
    sineFifo.resize (sineFifo.size() + (size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
        sineFifo[sineFifo.size() - numSamples + (size_t) i] = samples[i];
}

void TelevisionAudioProcessor::runSineFFTIfReady()
{
    while ((int) sineFifo.size() >= fftSize)
    {
        const size_t startIdx = sineFifo.size() - fftSize;

        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = sineFifo[startIdx + (size_t) i];

        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        {
            std::scoped_lock lk (sineLock);
            if ((int) latestSineMagnitudes.size() != numBins)
                latestSineMagnitudes.assign (numBins, 0.0f);

            for (int i = 0; i < numBins; ++i)
                latestSineMagnitudes[(size_t) i] = fftData[(size_t) i];
        }

        sineFifo.erase (sineFifo.begin(), sineFifo.begin() + hopSize);
    }
}

void TelevisionAudioProcessor::getLatestSpectrum (std::vector<float>& outSlice)
{
    std::scoped_lock lk (magLock);
    outSlice = latestMagnitudes;
}

void TelevisionAudioProcessor::getLatestSineSpectrum (std::vector<float>& outSlice)
{
    std::scoped_lock lk (sineLock);
    outSlice = latestSineMagnitudes;
}

juce::AudioProcessorEditor* TelevisionAudioProcessor::createEditor()
{
    return new TelevisionAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TelevisionAudioProcessor();
}

