#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    inline float coeff(float ms, double sr)
    {
        return std::exp(-1.0f / (juce::jmax(0.0001f, ms) * 0.001f
                                 * static_cast<float>(sr)));
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

    inline float clampDenormal(float value)
    {
        return std::abs(value) < 1.0e-12f ? 0.0f : value;
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

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ghostAmount", 1}, "Ghost", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"body", 1}, "Body", r, 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"tail", 1}, "Tail", r, 0.50f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"width", 1}, "Width", r, 0.45f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"air", 1}, "Air", r, 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"smooth", 1}, "Smooth", r, 0.55f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", r, 1.00f));
    return { p.begin(), p.end() };
}

void GHOSTAudioProcessor::prepareToPlay(double sr, int)
{
    currentSampleRate = juce::jmax(8000.0, sr);

    fastEnvelope = slowEnvelope = previousEnvelope = 0.0f;
    bodyL = bodyR = toneL = toneR = 0.0f;

    fastAttackCoeff = coeff(2.5f, currentSampleRate);
    fastReleaseCoeff = coeff(45.0f, currentSampleRate);
    slowAttackCoeff = coeff(18.0f, currentSampleRate);
    slowReleaseCoeff = coeff(260.0f, currentSampleRate);

    bodyCoeff = alpha(650.0f, currentSampleRate);
    toneCoeff = alpha(2200.0f, currentSampleRate);

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

void GHOSTAudioProcessor::processBlock(juce::AudioBuffer<float>& b,
                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float ghost = shape(apvts.getRawParameterValue("ghostAmount")->load());
    const float attack = shape(apvts.getRawParameterValue("attack")->load());
    const float body = shape(apvts.getRawParameterValue("body")->load());
    const float tail = shape(apvts.getRawParameterValue("tail")->load());
    const float width = shape(apvts.getRawParameterValue("width")->load());
    const float air = shape(apvts.getRawParameterValue("air")->load());
    const float smooth = shape(apvts.getRawParameterValue("smooth")->load());
    const float mix = apvts.getRawParameterValue("mix")->load();

    // Smooth changes detector response time without introducing block-level jumps.
    const float fastAttackMs = 1.5f + smooth * 8.0f;
    const float fastReleaseMs = 22.0f + smooth * 85.0f;
    const float slowAttackMs = 10.0f + smooth * 35.0f;
    const float slowReleaseMs = 120.0f + smooth * 380.0f;

    fastAttackCoeff = coeff(fastAttackMs, currentSampleRate);
    fastReleaseCoeff = coeff(fastReleaseMs, currentSampleRate);
    slowAttackCoeff = coeff(slowAttackMs, currentSampleRate);
    slowReleaseCoeff = coeff(slowReleaseMs, currentSampleRate);

    const int channels = b.getNumChannels();
    if (channels == 0 || b.getNumSamples() == 0)
        return;

    float peakT = 0.0f, peakB = 0.0f, peakTail = 0.0f, peakG = 0.0f;

    for (int n = 0; n < b.getNumSamples(); ++n)
    {
        const float inL = b.getSample(0, n);
        const float inR = channels > 1 ? b.getSample(1, n) : inL;
        const float detector = 0.5f * (std::abs(inL) + std::abs(inR));

        if (detector > fastEnvelope)
            fastEnvelope = fastAttackCoeff * fastEnvelope
                         + (1.0f - fastAttackCoeff) * detector;
        else
            fastEnvelope = fastReleaseCoeff * fastEnvelope
                         + (1.0f - fastReleaseCoeff) * detector;

        if (detector > slowEnvelope)
            slowEnvelope = slowAttackCoeff * slowEnvelope
                         + (1.0f - slowAttackCoeff) * detector;
        else
            slowEnvelope = slowReleaseCoeff * slowEnvelope
                         + (1.0f - slowReleaseCoeff) * detector;

        // Ratio detector: much more reliable for quiet or heavily compressed material.
        const float reference = juce::jmax(slowEnvelope, 0.0005f);
        const float attackRatio = fastEnvelope / reference;
        const float transient = juce::jlimit(
            0.0f, 1.0f, (attackRatio - 1.0f) * 2.75f);

        const float tailRatio = slowEnvelope / juce::jmax(fastEnvelope, 0.0005f);
        const float tailState = juce::jlimit(
            0.0f, 1.0f, (tailRatio - 1.0f) * 1.8f);

        const float bodyState = juce::jlimit(
            0.0f, 1.0f, slowEnvelope * 4.0f);

        previousEnvelope = fastEnvelope;

        const float motion = juce::jlimit(
            0.0f, 1.0f,
            0.58f * transient
            + 0.24f * body * bodyState
            + 0.30f * tail * tailState) * (0.10f + 0.90f * ghost);

        peakT = juce::jmax(peakT, transient);
        peakB = juce::jmax(peakB, bodyState);
        peakTail = juce::jmax(peakTail, tailState);
        peakG = juce::jmax(peakG, motion);

        bodyL += bodyCoeff * (inL - bodyL);
        bodyR += bodyCoeff * (inR - bodyR);

        toneL += toneCoeff * (inL - toneL);
        toneR += toneCoeff * (inR - toneR);

        const float lowL = bodyL;
        const float lowR = bodyR;
        const float midL = toneL - bodyL;
        const float midR = toneR - bodyR;
        const float highL = inL - toneL;
        const float highR = inR - toneR;

        const float transientBoost =
            1.0f + attack * ghost * transient * 1.35f;

        const float bodyBoost =
            1.0f + body * ghost * bodyState * 0.38f;

        const float tailCut =
            1.0f - tail * ghost * tailState * 0.42f;

        const float airBoost =
            1.0f + air * ghost * transient * 1.80f
                  - air * ghost * tailState * 0.35f;

        const float lowLProcessed = lowL * bodyBoost;
        const float lowRProcessed = lowR * bodyBoost;
        const float midLProcessed = midL * transientBoost * tailCut;
        const float midRProcessed = midR * transientBoost * tailCut;
        const float highLProcessed = highL * airBoost * tailCut;
        const float highRProcessed = highR * airBoost * tailCut;

        if (channels > 1)
        {
            const float mid = 0.5f * (lowLProcessed + lowRProcessed);
            const float sideLow = 0.5f * (lowLProcessed - lowRProcessed);
            const float sideMid = 0.5f * (midLProcessed - midRProcessed);
            const float sideHigh = 0.5f * (highLProcessed - highRProcessed);

            // GHOST opens on attacks and gently collapses during the tail.
            const float widthFactor = juce::jlimit(
                0.60f, 2.20f,
                1.0f + width * ghost
                    * (1.55f * transient - 0.75f * tailState));

            const float outL =
                mid + sideLow
                + 0.72f * midLProcessed
                + sideMid * widthFactor
                + highLProcessed;

            const float outR =
                mid - sideLow
                + 0.72f * midRProcessed
                - sideMid * widthFactor
                + highRProcessed;

            // Tiny dynamic saturation prevents the ghost response from feeling like
            // a static EQ move when the source has strong transient energy.
            const float drive = 1.0f + 1.10f * ghost * transient;
            const float wetL = std::tanh(outL * drive) / std::tanh(drive);
            const float wetR = std::tanh(outR * drive) / std::tanh(drive);

            b.setSample(0, n, clampDenormal(inL + (wetL - inL) * mix));
            b.setSample(1, n, clampDenormal(inR + (wetR - inR) * mix));
        }
        else
        {
            const float out =
                lowLProcessed
                + 0.85f * midLProcessed
                + highLProcessed;

            const float drive = 1.0f + 1.10f * ghost * transient;
            const float wet = std::tanh(out * drive) / std::tanh(drive);

            b.setSample(0, n, clampDenormal(inL + (wet - inL) * mix));
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GHOSTAudioProcessor();
}
