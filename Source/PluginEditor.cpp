#include "PluginEditor.h"

namespace
{
    void setupSlider(juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 22);
        slider.setRange(0.0, 1.0, 0.001);
    }

    void setupLabel(juce::Label& label, const juce::String& text, float size)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(size, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setJustificationType(juce::Justification::centred);
    }
}

GHOSTAudioProcessorEditor::GHOSTAudioProcessorEditor(GHOSTAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(760, 460);

    setupSlider(ghostSlider);
    setupSlider(attackSlider);
    setupSlider(tailSlider);
    setupSlider(widthSlider);
    setupSlider(mixSlider);

    addAndMakeVisible(ghostSlider);
    addAndMakeVisible(attackSlider);
    addAndMakeVisible(tailSlider);
    addAndMakeVisible(widthSlider);
    addAndMakeVisible(mixSlider);

    setupLabel(title, "GHOST", 30.0f);
    setupLabel(status, "DYNAMIC SHADOW ENGINE", 12.0f);

    addAndMakeVisible(title);
    addAndMakeVisible(status);

    ghostAttachment = std::make_unique<SliderAttachment>(
        processor.getAPVTS(), "ghostAmount", ghostSlider);
    attackAttachment = std::make_unique<SliderAttachment>(
        processor.getAPVTS(), "attack", attackSlider);
    tailAttachment = std::make_unique<SliderAttachment>(
        processor.getAPVTS(), "tail", tailSlider);
    widthAttachment = std::make_unique<SliderAttachment>(
        processor.getAPVTS(), "width", widthSlider);
    mixAttachment = std::make_unique<SliderAttachment>(
        processor.getAPVTS(), "mix", mixSlider);

    startTimerHz(30);
}

void GHOSTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(9, 10, 13));

    auto bounds = getLocalBounds().reduced(24);

    g.setColour(juce::Colour::fromRGB(18, 20, 25));
    g.fillRoundedRectangle(bounds.toFloat(), 18.0f);

    auto header = bounds.removeFromTop(86);
    title.setBounds(header.removeFromTop(44));
    status.setBounds(header);

    g.setColour(juce::Colour::fromRGB(35, 38, 47));
    g.drawRoundedRectangle(bounds.toFloat(), 14.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(85, 90, 105));
    g.setFont(11.0f);
    g.drawText("GHOST RESPONSE", bounds.removeFromTop(26),
               juce::Justification::centredLeft);

    auto meter = bounds.removeFromTop(52).reduced(16, 8);

    const auto ghost = processor.getAPVTS()
                           .getRawParameterValue("ghostAmount")->load();

    g.setColour(juce::Colour::fromRGB(35, 38, 47));
    g.fillRoundedRectangle(meter.toFloat(), 5.0f);

    g.setColour(juce::Colour::fromRGB(190, 195, 210));
    g.fillRoundedRectangle(
        meter.withWidth(juce::roundToInt(meter.getWidth() * ghost)).toFloat(),
        5.0f);

    const auto knobs = bounds.removeFromBottom(210);
    const int gap = 12;
    const int cellWidth = (knobs.getWidth() - gap * 4) / 5;

    auto place = [&] (juce::Slider& slider, int index)
    {
        auto area = knobs.withX(knobs.getX() + index * (cellWidth + gap))
                         .withWidth(cellWidth);
        slider.setBounds(area.reduced(8));
    };

    place(ghostSlider, 0);
    place(attackSlider, 1);
    place(tailSlider, 2);
    place(widthSlider, 3);
    place(mixSlider, 4);

    g.setColour(juce::Colour::fromRGB(85, 90, 105));
    g.setFont(10.0f);

    const auto labels = std::array<const char*, 5>{
        "GHOST", "ATTACK", "TAIL", "WIDTH", "MIX"
    };

    for (int i = 0; i < 5; ++i)
        g.drawText(labels[static_cast<size_t>(i)],
                   knobs.getX() + i * (cellWidth + gap),
                   knobs.getBottom() + 2,
                   cellWidth,
                   18,
                   juce::Justification::centred);
}

void GHOSTAudioProcessorEditor::resized()
{
    repaint();
}

void GHOSTAudioProcessorEditor::timerCallback()
{
    repaint();
}
