#pragma once
#include <JuceHeader.h>
#include <atomic>

class GHOSTAudioProcessor final : public juce::AudioProcessor
{
public:
    GHOSTAudioProcessor();
    ~GHOSTAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    float getTransientMeter() const noexcept { return transientMeter.load(); }
    float getBodyMeter() const noexcept { return bodyMeter.load(); }
    float getTailMeter() const noexcept { return tailMeter.load(); }
    float getGhostMeter() const noexcept { return ghostMeter.load(); }
    float getGhostState() const noexcept { return ghostState; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState apvts;
    double currentSampleRate = 44100.0;

    float fastEnvelope = 0.0f, slowEnvelope = 0.0f, previousEnvelope = 0.0f;
    float ghostState = 0.0f;

    static constexpr int numBands = 6;
    std::array<juce::dsp::IIR::Filter<float>, numBands> bandL;
    std::array<juce::dsp::IIR::Filter<float>, numBands> bandR;
    std::array<float, numBands> bandEnvelope {};
    std::array<float, numBands> bandGain {};
    int dominantBand = 0;

    float bodyL = 0.0f, bodyR = 0.0f;
    float toneL = 0.0f, toneR = 0.0f;
    float fastAttackCoeff = 0.0f, fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
    float bodyCoeff = 0.0f;
    float toneCoeff = 0.0f;
    float ghostRiseCoeff = 0.0f;
    float ghostFallCoeff = 0.0f;
    float bandAttackCoeff = 0.0f;
    float bandReleaseCoeff = 0.0f;
    float bandGainAttackCoeff = 0.0f;
    float bandGainReleaseCoeff = 0.0f;

    std::atomic<float> transientMeter { 0.0f };
    std::atomic<float> bodyMeter { 0.0f };
    std::atomic<float> tailMeter { 0.0f };
    std::atomic<float> ghostMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHOSTAudioProcessor)
};
