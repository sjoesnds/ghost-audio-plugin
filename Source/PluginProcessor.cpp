#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    inline float coeff(float ms, double sr)
    {
        return std::exp(-1.0f / (juce::jmax(0.0001f, ms) * 0.001f * static_cast<float>(sr)));
    }

    inline float alpha(float cutoff, double sr)
    {
        const float omega = 2.0f * juce::MathConstants<float>::pi
                          * cutoff / static_cast<float>(sr);
        return juce::jlimit(0.001f, 0.999f, omega / (1.0f + omega));
    }

    inline float shape(float x)
    {
        x = juce::jlimit(0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
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
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    const juce::NormalisableRange<float> r(0.0f, 1.0f, 0.001f);

    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"ghostAmount",1}, "Ghost", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"attack",1}, "Attack", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"body",1}, "Body", r, 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tail",1}, "Tail", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"width",1}, "Width", r, 0.45f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"air",1}, "Air", r, 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"smooth",1}, "Smooth", r, 0.55f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1}, "Mix", r, 1.00f));
    return { p.begin(), p.end() };
}

void GHOSTAudioProcessor::prepareToPlay(double sr, int)
{
    currentSampleRate = juce::jmax(8000.0, sr);
    fastEnvelope = slowEnvelope = previousEnvelope = 0.0f;
    lowpassL = lowpassR = 0.0f;
    fastAttackCoeff = coeff(2.5f, currentSampleRate);
    fastReleaseCoeff = coeff(45.0f, currentSampleRate);
    slowAttackCoeff = coeff(18.0f, currentSampleRate);
    slowReleaseCoeff = coeff(260.0f, currentSampleRate);
    toneCoeff = alpha(1800.0f, currentSampleRate);
    transientMeter.store(0.0f);
    bodyMeter.store(0.0f);
    tailMeter.store(0.0f);
    ghostMeter.store(0.0f);
}

void GHOSTAudioProcessor::releaseResources() {}

bool GHOSTAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono()
                      || in == juce::AudioChannelSet::stereo());
}

void GHOSTAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float ghost = shape(apvts.getRawParameterValue("ghostAmount")->load()) * 0.90f;
    const float attack = shape(apvts.getRawParameterValue("attack")->load());
    const float body = shape(apvts.getRawParameterValue("body")->load());
    const float tail = shape(apvts.getRawParameterValue("tail")->load());
    const float width = shape(apvts.getRawParameterValue("width")->load());
    const float air = shape(apvts.getRawParameterValue("air")->load());
    const float mix = apvts.getRawParameterValue("mix")->load();

    const int channels = b.getNumChannels();
    if (channels == 0 || b.getNumSamples() == 0) return;

    float peakT = 0.0f, peakB = 0.0f, peakTail = 0.0f, peakG = 0.0f;

    for (int n = 0; n < b.getNumSamples(); ++n)
    {
        const float left = b.getSample(0, n);
        const float right = channels > 1 ? b.getSample(1, n) : left;
        const float d = 0.5f * (std::abs(left) + std::abs(right));

        fastEnvelope = d > fastEnvelope
            ? fastAttackCoeff * fastEnvelope + (1.0f - fastAttackCoeff) * d
            : fastReleaseCoeff * fastEnvelope + (1.0f - fastReleaseCoeff) * d;

        slowEnvelope = d > slowEnvelope
            ? slowAttackCoeff * slowEnvelope + (1.0f - slowAttackCoeff) * d
            : slowReleaseCoeff * slowEnvelope + (1.0f - slowReleaseCoeff) * d;

        const float transient = juce::jlimit(0.0f, 1.0f,
            (fastEnvelope - slowEnvelope) * 10.0f);
        const float bodyState = juce::jlimit(0.0f, 1.0f, slowEnvelope * 3.0f);
        const float tailState = juce::jlimit(0.0f, 1.0f,
            (previousEnvelope - fastEnvelope) * 32.0f
            + (1.0f - bodyState) * 0.12f);
        previousEnvelope = fastEnvelope;

        const float motion = juce::jlimit(0.0f, 1.0f,
            transient * (0.65f + 0.80f * attack)
            + bodyState * body * 0.22f
            + tailState * tail * 0.28f) * ghost;

        peakT = juce::jmax(peakT, transient);
        peakB = juce::jmax(peakB, bodyState);
        peakTail = juce::jmax(peakTail, tailState);
        peakG = juce::jmax(peakG, motion);

        if (channels > 1)
        {
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);

            lowpassL += toneCoeff * (left - lowpassL);
            lowpassR += toneCoeff * (right - lowpassR);

            const float highL = left - lowpassL;
            const float highR = right - lowpassR;

            const float gain =
                1.0f + attack * transient * ghost * 0.55f
                     + body * bodyState * ghost * 0.18f;

            const float tailDamp =
                1.0f - tail * tailState * ghost * 0.24f;

            const float airGain =
                1.0f + air * transient * ghost * 0.65f
                     - tail * tailState * ghost * 0.20f;

            const float dynamicWidth = juce::jlimit(0.45f, 1.85f,
                1.0f + width * ghost * (1.20f * transient - 0.55f * tailState));

            const float wm = mid * gain;
            const float ws = side * dynamicWidth;
            const float wetL = wm + ws + highL * (airGain * tailDamp - 1.0f) * 0.70f;
            const float wetR = wm - ws + highR * (airGain * tailDamp - 1.0f) * 0.70f;

            b.setSample(0, n, left + (wetL - left) * mix);
            b.setSample(1, n, right + (wetR - right) * mix);
        }
        else
        {
            lowpassL += toneCoeff * (left - lowpassL);
            const float high = left - lowpassL;
            const float gain =
                1.0f + attack * transient * ghost * 0.55f
                     + body * bodyState * ghost * 0.18f
                     - tail * tailState * ghost * 0.15f;
            const float airGain =
                1.0f + air * transient * ghost * 0.60f
                     - tail * tailState * ghost * 0.18f;

            const float wet = left * gain + high * (airGain - 1.0f) * 0.70f;
            b.setSample(0, n, left + (wet - left) * mix);
        }
    }

    transientMeter.store(peakT);
    bodyMeter.store(peakB);
    tailMeter.store(peakTail);
    ghostMeter.store(peakG);
}

juce::AudioProcessorEditor* GHOSTAudioProcessor::createEditor()
{
    return new GHOSTAudioProcessorEditor(*this);
}

void GHOSTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GHOSTAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
