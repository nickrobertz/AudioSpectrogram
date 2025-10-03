#pragma once

#include <JuceHeader.h>
#include <vector>
#include <deque>
#include <mutex>

class TelevisionAudioProcessor : public juce::AudioProcessor
{
public:
    TelevisionAudioProcessor();
    ~TelevisionAudioProcessor() override;

    // ===== JUCE overrides =====
    void prepareToPlay (double sampleRate, int samplesPerBlockExpected) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                             { return true; }

    const juce::String getName() const override                 { return JucePlugin_Name; }
    bool acceptsMidi() const override                           { return false; }
    bool producesMidi() const override                          { return false; }
    bool isMidiEffect() const override                          { return false; }
    double getTailLengthSeconds() const override                { return 0.0; }

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock&) override      {}
    void setStateInformation (const void*, int) override        {}

    // ===== Parameters =====
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ===== Visual config exposed to editor =====
    static constexpr int fftOrder = 10;                // 2^10 = 1024
    static constexpr int fftSize  = 1 << fftOrder;     // 1024
    static constexpr int hopSize  = fftSize / 4;       // 256 (25% hop)
    static constexpr int numBins  = fftSize / 2;       // 512
    static constexpr int timeCols = 300;               // spectrogram width (pixels/columns)

    int   getNumBins()   const noexcept { return numBins; }
    int   getTimeBins()  const noexcept { return timeCols; }
    float getDynDb()     const noexcept { return 80.0f;   }
    double getSampleRateHz() const noexcept { return currentSR; }

    float getSensitivity() const
    {
        return apvts.getRawParameterValue ("sensitivity")->load();
    }

    float getSineLevel() const
    {
        return apvts.getRawParameterValue ("sineLevel")->load();
    }

    // Spectrum data getters
    void getLatestSpectrum (std::vector<float>& outSlice);
    void getLatestSineSpectrum (std::vector<float>& outSlice);

private:
    // ===== FFT & window =====
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window;

    // ===== Audio accumulation =====
    std::deque<float> monoFifo;
    int samplesSinceLastFFT = 0;
    double currentSR = 44100.0;

    // ===== Output to UI =====
    std::vector<float> latestMagnitudes;
    std::mutex         magLock;

    // ===== Sine generation =====
    double phase = 0.0;
    std::deque<float> sineFifo;
    std::vector<float> latestSineMagnitudes;
    std::mutex sineLock;

    // Helpers
    void pushAudioToFifo (const float* left, const float* rightOrNull, int numSamples);
    void runFFTIfReady();

    void pushSineToFifo (const float* samples, int numSamples);
    void runSineFFTIfReady();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelevisionAudioProcessor)
};

