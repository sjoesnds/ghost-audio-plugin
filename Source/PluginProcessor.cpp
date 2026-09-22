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
    ghostState = 0.0f;
    ghostHaloL = ghostHaloR = 0.0f;
    bandEnvelope.fill(0.0f);
    bandGain.fill(1.0f);
    dominantBand = 0;
    bodyL = bodyR = toneL = toneR = 0.0f;

    fastAttackCoeff = coeff(2.5f, currentSampleRate);
    fastReleaseCoeff = coeff(45.0f, currentSampleRate);
    slowAttackCoeff = coeff(18.0f, currentSampleRate);
    slowReleaseCoeff = coeff(260.0f, currentSampleRate);

    bodyCoeff = alpha(650.0f, currentSampleRate);
    toneCoeff = alpha(2200.0f, currentSampleRate);
    ghostRiseCoeff = coeff(7.0f, currentSampleRate);
    ghostFallCoeff = coeff(180.0f, currentSampleRate);
    bandAttackCoeff = coeff(8.0f, currentSampleRate);
    bandReleaseCoeff = coeff(90.0f, currentSampleRate);
    bandGainAttackCoeff = coeff(4.0f, currentSampleRate);
    bandGainReleaseCoeff = coeff(70.0f, currentSampleRate);
    haloAttackCoeff = coeff(1.5f, currentSampleRate);
    haloReleaseCoeff = coeff(95.0f, currentSampleRate);

    ghostShortDelaySamples = juce::jmax(
        1, juce::roundToInt(0.0065f * static_cast<float>(currentSampleRate)));
    ghostLongDelaySamples = juce::jmax(
        2, juce::roundToInt(0.0175f * static_cast<float>(currentSampleRate)));

    const juce::dsp::ProcessSpec delaySpec {
        currentSampleRate,
        512,
        1
    };

    const int maxDelaySamples = juce::jmax(
        8, juce::roundToInt(0.030f * static_cast<float>(currentSampleRate)));

    for (auto* delay : { &ghostDelayShortL, &ghostDelayShortR,
                         &ghostDelayLongL, &ghostDelayLongR })
    {
        delay->setMaximumDelayInSamples(maxDelaySamples);
        delay->prepare(delaySpec);
        delay->reset();
    }

    ghostDelayShortL.setDelay(static_cast<float>(ghostShortDelaySamples));
    ghostDelayShortR.setDelay(static_cast<float>(ghostShortDelaySamples));
    ghostDelayLongL.setDelay(static_cast<float>(ghostLongDelaySamples));
    ghostDelayLongR.setDelay(static_cast<float>(ghostLongDelaySamples));

    const float bandFrequencies[numBands] = { 90.0f, 240.0f, 600.0f,
                                               1500.0f, 3400.0f, 7600.0f };
    for (int i = 0; i < numBands; ++i)
    {
        const float safeFrequency = juce::jmin(
            bandFrequencies[i], static_cast<float>(currentSampleRate * 0.40));
        const float q = (i == numBands - 1) ? 0.80f : 0.95f;
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            currentSampleRate, safeFrequency, q);
        bandL[static_cast<size_t>(i)].coefficients = coefficients;
        bandR[static_cast<size_t>(i)].coefficients = coefficients;
        bandL[static_cast<size_t>(i)].reset();
        bandR[static_cast<size_t>(i)].reset();
    }

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
    const float mix = juce::jlimit(0.0f, 1.0f,
                                   apvts.getRawParameterValue("mix")->load());

    const int channels = b.getNumChannels();
    const int samples = b.getNumSamples();

    if (channels == 0 || samples == 0)
        return;

    // Smooth controls only change detector timing. The actual audio path stays
    // sample-continuous so turning a knob cannot create a block-sized jump.
    fastAttackCoeff = coeff(1.5f + smooth * 8.0f, currentSampleRate);
    fastReleaseCoeff = coeff(22.0f + smooth * 85.0f, currentSampleRate);
    slowAttackCoeff = coeff(10.0f + smooth * 35.0f, currentSampleRate);
    slowReleaseCoeff = coeff(120.0f + smooth * 380.0f, currentSampleRate);

    float peakT = 0.0f;
    float peakB = 0.0f;
    float peakTail = 0.0f;
    float peakG = 0.0f;

    for (int n = 0; n < samples; ++n)
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

        const float reference = juce::jmax(slowEnvelope, 0.0005f);
        const float attackRatio = fastEnvelope / reference;
        const float transient = juce::jlimit(
            0.0f, 1.0f, (attackRatio - 1.0f) * 2.75f);

        const float tailRatio = slowEnvelope
                              / juce::jmax(fastEnvelope, 0.0005f);
        const float tailState = juce::jlimit(
            0.0f, 1.0f, (tailRatio - 1.0f) * 1.8f);

        const float bodyState = juce::jlimit(
            0.0f, 1.0f, slowEnvelope * 4.0f);

        const float targetMotion = juce::jlimit(
            0.0f, 1.0f,
            0.55f * transient
            + 0.18f * body * bodyState
            + 0.30f * tail * tailState);

        // GHOST is a real master control. At zero, the effect state is zero
        // and the final output is mathematically dry when MIX is 100%.
        const float ghostTarget = targetMotion * ghost;

        if (ghostTarget > ghostState)
            ghostState = ghostRiseCoeff * ghostState
                       + (1.0f - ghostRiseCoeff) * ghostTarget;
        else
            ghostState = ghostFallCoeff * ghostState
                       + (1.0f - ghostFallCoeff) * ghostTarget;

        peakT = juce::jmax(peakT, transient);
        peakB = juce::jmax(peakB, bodyState);
        peakTail = juce::jmax(peakTail, tailState);
        peakG = juce::jmax(peakG, ghostState);

        // --- Stable broad-band decomposition -----------------------------------------
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

        // --- Perceptual spectral focus -----------------------------------------------
        float bandSum = 0.0f;
        float strongest = 0.0f;
        int strongestIndex = dominantBand;

        std::array<float, numBands> bandSamplesL {};
        std::array<float, numBands> bandSamplesR {};

        for (int i = 0; i < numBands; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const float bl = bandL[index].processSample(inL);
            const float br = bandR[index].processSample(inR);
            bandSamplesL[index] = bl;
            bandSamplesR[index] = br;

            const float energy = 0.5f * (std::abs(bl) + std::abs(br));
            auto& env = bandEnvelope[index];

            if (energy > env)
                env = bandAttackCoeff * env
                    + (1.0f - bandAttackCoeff) * energy;
            else
                env = bandReleaseCoeff * env
                    + (1.0f - bandReleaseCoeff) * energy;

            bandSum += env;

            if (env > strongest)
            {
                strongest = env;
                strongestIndex = i;
            }
        }

        dominantBand = strongestIndex;

        const float uniformShare = 1.0f / static_cast<float>(numBands);
        const float dominantShare =
            strongest / juce::jmax(bandSum, 1.0e-5f);
        const float dominance = juce::jlimit(
            0.0f, 1.0f,
            (dominantShare - uniformShare) / (0.42f - uniformShare));

        const float focusPulse =
            juce::jlimit(0.0f, 1.0f,
                transient * (0.50f + 0.85f * attack)
                + ghostState * 0.20f)
            * dominance * ghost;

        std::array<float, numBands> desiredBandGain {};
        float totalCorrection = 0.0f;

        for (int i = 0; i < numBands; ++i)
        {
            const float distance = std::abs(
                static_cast<float>(i - dominantBand));

            const float focusWeight =
                std::exp(-1.20f * distance * distance);
            const float shadowWeight =
                distance > 2.5f
                    ? 0.0f
                    : std::exp(-0.78f * distance * distance);

            float correction =
                + focusWeight * attack * focusPulse * 0.22f
                - shadowWeight * tail * ghostState * 0.08f
                - shadowWeight * focusPulse * 0.12f;

            if (i == numBands - 1)
                correction += air * focusPulse * 0.10f;

            desiredBandGain[static_cast<size_t>(i)] =
                juce::jlimit(0.78f, 1.28f, 1.0f + correction);
            totalCorrection += desiredBandGain[static_cast<size_t>(i)] - 1.0f;
        }

        const float compensation =
            -0.55f * totalCorrection / static_cast<float>(numBands);

        for (int i = 0; i < numBands; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const float target = juce::jlimit(
                0.76f, 1.25f, desiredBandGain[index] + compensation);

            auto& current = bandGain[index];
            const float smoothing = target > current
                ? bandGainAttackCoeff
                : bandGainReleaseCoeff;

            current = smoothing * current
                    + (1.0f - smoothing) * target;
        }

        // --- Ghost Halo --------------------------------------------------------------
        // Two different micro-reflections create an after-image. The later tap
        // is cross-fed so the shadow can detach from the original stereo position.
        const float shadowInputL = 0.60f * highL + 0.40f * midL;
        const float shadowInputR = 0.60f * highR + 0.40f * midR;

        ghostDelayShortL.pushSample(0, shadowInputL);
        ghostDelayShortR.pushSample(0, shadowInputR);
        ghostDelayLongL.pushSample(0, shadowInputL);
        ghostDelayLongR.pushSample(0, shadowInputR);

        const float shortL = ghostDelayShortL.popSample(0);
        const float shortR = ghostDelayShortR.popSample(0);
        const float longL = ghostDelayLongL.popSample(0);
        const float longR = ghostDelayLongR.popSample(0);

        const float reflectionL =
            0.78f * shortL + 0.34f * longR;
        const float reflectionR =
            0.78f * shortR + 0.34f * longL;

        const float haloTarget = juce::jlimit(
            0.0f, 1.0f,
            transient * ghost * (0.55f + 0.90f * attack)
            + ghostState * 0.18f);

        if (haloTarget > ghostHaloL)
            ghostHaloL = haloAttackCoeff * ghostHaloL
                       + (1.0f - haloAttackCoeff) * haloTarget;
        else
            ghostHaloL = haloReleaseCoeff * ghostHaloL
                       + (1.0f - haloReleaseCoeff) * haloTarget;

        if (haloTarget > ghostHaloR)
            ghostHaloR = haloAttackCoeff * ghostHaloR
                       + (1.0f - haloAttackCoeff) * haloTarget;
        else
            ghostHaloR = haloReleaseCoeff * ghostHaloR
                       + (1.0f - haloReleaseCoeff) * haloTarget;

        const float haloState = 0.5f * (ghostHaloL + ghostHaloR);
        const float haloAmount =
            width * ghost * 0.72f * haloState;

        // --- Musical / perceptual movement -------------------------------------------
        // The dry decomposition sums exactly back to the input. Every deviation
        // below is scaled by GHOST, which keeps MIX predictable and bypass clean.
        const float attackPulse =
            transient * ghost * (0.30f + 0.95f * attack);
        const float bodyPulse =
            bodyState * ghost * (0.25f + 0.75f * body);
        const float tailPulse =
            tailState * ghost * (0.30f + 0.70f * tail);

        const float lowGain =
            1.0f + body * bodyPulse * 0.18f;
        const float midGain =
            1.0f + attack * attackPulse * 0.46f
                  - tail * tailPulse * 0.12f;
        const float highGain =
            1.0f + air * attackPulse * 0.62f
                  - air * tailPulse * 0.12f;

        float spectralDeltaL = 0.0f;
        float spectralDeltaR = 0.0f;

        for (int i = 0; i < numBands; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const float delta = bandGain[index] - 1.0f;
            spectralDeltaL += bandSamplesL[index] * delta;
            spectralDeltaR += bandSamplesR[index] * delta;
        }

        spectralDeltaL *= ghost;
        spectralDeltaR *= ghost;

        float wetL =
            lowL * lowGain
            + midL * midGain
            + highL * highGain
            + spectralDeltaL;

        float wetR =
            lowR * lowGain
            + midR * midGain
            + highR * highGain
            + spectralDeltaR;

        if (channels > 1)
        {
            // Width acts on the ghost contribution, not on the dry image.
            // This prevents the plugin from becoming a conventional stereo widener.
            const float midReflection =
                0.5f * (reflectionL + reflectionR);
            const float sideReflection =
                0.5f * (reflectionL - reflectionR)
                * (0.35f + 1.65f * width);

            const float spatialL =
                0.20f * midReflection + sideReflection;
            const float spatialR =
                0.20f * midReflection - sideReflection;

            wetL += spatialL * haloAmount;
            wetR += spatialR * haloAmount;
        }
        else
        {
            wetL += 0.28f * 0.5f * (reflectionL + reflectionR)
                  * haloAmount;
        }

        // A restrained event-dependent soft clip is used only on the wet path.
        // It adds a small density change to strong transients without acting
        // like a permanent saturator.
        if (ghost > 0.001f)
        {
            const float drive =
                1.0f + 0.65f * ghostState * transient;
            const float normalizer = std::tanh(drive);
            wetL = std::tanh(wetL * drive) / normalizer;
            wetR = std::tanh(wetR * drive) / normalizer;
        }

        const float dryAmount = 1.0f - mix;
        b.setSample(0, n, clampDenormal(
            inL * dryAmount + wetL * mix));
        if (channels > 1)
            b.setSample(1, n, clampDenormal(
                inR * dryAmount + wetR * mix));
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
