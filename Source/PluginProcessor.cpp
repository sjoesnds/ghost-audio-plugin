#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float minWidth = 1.0f;
    constexpr float maxWidth = 2.0f;

    inline float fastCoefficient(float seconds, double sampleRate)
    {
        return std::exp(-1.0f / (seconds * static_cast<float>(sampleRate)));
    }
}

GHOSTAudioProcessor::GHOSTAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout GHOSTAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ghostAmount",
        "Ghost",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack",
        "Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tail",
        "Tail",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width",
        "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix",
        "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f));

    return { params.begin(), params.end() };
}

void GHOSTAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    envelope = 0.0f;
    widthState = 0.0f;

    attackCoeff = fastCoefficient(0.005f, sampleRate);
    releaseCoeff = fastCoefficient(0.120f, sampleRate);
}

void GHOSTAudioProcessor::releaseResources()
{
}

bool GHOSTAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainIn == juce::AudioChannelSet::mono()
        || mainIn == juce::AudioChannelSet::stereo();
}

void GHOSTAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto ghost = apvts.getRawParameterValue("ghostAmount")->load();
    const auto attackAmount = apvts.getRawParameterValue("attack")->load();
    const auto tailAmount = apvts.getRawParameterValue("tail")->load();
    const auto widthAmount = apvts.getRawParameterValue("width")->load();
    const auto mix = apvts.getRawParameterValue("mix")->load();

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float sumAbs = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
            sumAbs += std::abs(buffer.getReadPointer(channel)[sample]);

        const float detector = sumAbs / static_cast<float>(numChannels);

        if (detector > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * detector;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * detector;

        const float attackShape = juce::jlimit(0.0f, 1.0f,
                                               (detector - envelope * 0.75f) * 8.0f);
        const float tailShape = 1.0f - juce::jlimit(0.0f, 1.0f, envelope * 3.0f);

        const float ghostDepth = ghost * 0.85f;
        const float dynamicWidth = minWidth
            + (maxWidth - minWidth)
                * widthAmount
                * ghostDepth
                * attackShape;

        if (numChannels >= 2)
        {
            const float dryL = buffer.getSample(0, sample);
            const float dryR = buffer.getSample(1, sample);

            const float mid = 0.5f * (dryL + dryR);
            const float side = 0.5f * (dryL - dryR);

            const float shapedSide = side * dynamicWidth;

            const float ghostGain =
                1.0f
                + (attackShape * attackAmount * ghostDepth * 0.35f)
                - (tailShape * tailAmount * ghostDepth * 0.12f);

            const float wetL = (mid + shapedSide) * ghostGain;
            const float wetR = (mid - shapedSide) * ghostGain;

            buffer.setSample(0, sample, dryL + (wetL - dryL) * mix);
            buffer.setSample(1, sample, dryR + (wetR - dryR) * mix);
        }
        else
        {
            const float dry = buffer.getSample(0, sample);
            const float ghostGain =
                1.0f
                + (attackShape * attackAmount * ghostDepth * 0.35f)
                - (tailShape * tailAmount * ghostDepth * 0.12f);

            const float wet = dry * ghostGain;
            buffer.setSample(0, sample, dry + (wet - dry) * mix);
        }
    }
}

juce::AudioProcessorEditor* GHOSTAudioProcessor::createEditor()
{
    return new GHOSTAudioProcessorEditor(*this);
}

void GHOSTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState().createXml())
        copyXmlToBinary(*state, destData);
}

void GHOSTAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto state = getXmlFromBinary(data, sizeInBytes))
    {
        if (state->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*state));
    }
}
