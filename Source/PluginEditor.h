#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GHOSTAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit GHOSTAudioProcessorEditor(GHOSTAudioProcessor&);
    ~GHOSTAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupSlider(juce::Slider&);

    GHOSTAudioProcessor& processor;

    juce::Slider ghostSlider, attackSlider, bodySlider, tailSlider;
    juce::Slider widthSlider, airSlider, smoothSlider, mixSlider;
    juce::Label title, subtitle;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> ghostAttachment, attackAttachment;
    std::unique_ptr<SliderAttachment> bodyAttachment, tailAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment, airAttachment;
    std::unique_ptr<SliderAttachment> smoothAttachment, mixAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHOSTAudioProcessorEditor)
};
