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

    GHOSTAudioProcessor& processor;

    juce::Slider ghostSlider;
    juce::Slider attackSlider;
    juce::Slider tailSlider;
    juce::Slider widthSlider;
    juce::Slider mixSlider;

    juce::Label title;
    juce::Label status;

    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> ghostAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> tailAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHOSTAudioProcessorEditor)
};
