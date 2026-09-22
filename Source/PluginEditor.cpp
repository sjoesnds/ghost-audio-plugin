#include "PluginEditor.h"

namespace
{
    const auto bg = juce::Colour::fromRGB(7, 8, 11);
    const auto panel = juce::Colour::fromRGB(14, 16, 21);
    const auto edge = juce::Colour::fromRGB(34, 37, 46);
    const auto text = juce::Colour::fromRGB(228, 230, 236);
    const auto muted = juce::Colour::fromRGB(110, 115, 128);
    const auto ghost = juce::Colour::fromRGB(181, 187, 202);
}

GHOSTAudioProcessorEditor::GHOSTAudioProcessorEditor(GHOSTAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setResizable(true, true);
    setResizeLimits(680, 440, 1300, 850);
    setSize(820, 570);

    setupSlider(ghostSlider);
    setupSlider(attackSlider);
    setupSlider(bodySlider);
    setupSlider(tailSlider);
    setupSlider(widthSlider);
    setupSlider(airSlider);
    setupSlider(smoothSlider);
    setupSlider(mixSlider);

    for (auto* s : { &ghostSlider, &attackSlider, &bodySlider, &tailSlider,
                     &widthSlider, &airSlider, &smoothSlider, &mixSlider })
        addAndMakeVisible(*s);

    title.setText("GHOST", juce::dontSendNotification);
    title.setFont(juce::Font(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, text);
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);

    subtitle.setText("DYNAMIC SHADOW ENGINE", juce::dontSendNotification);
    subtitle.setFont(juce::Font(10.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, muted);
    subtitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitle);

    auto& state = processor.getAPVTS();
    ghostAttachment = std::make_unique<SliderAttachment>(state, "ghostAmount", ghostSlider);
    attackAttachment = std::make_unique<SliderAttachment>(state, "attack", attackSlider);
    bodyAttachment = std::make_unique<SliderAttachment>(state, "body", bodySlider);
    tailAttachment = std::make_unique<SliderAttachment>(state, "tail", tailSlider);
    widthAttachment = std::make_unique<SliderAttachment>(state, "width", widthSlider);
    airAttachment = std::make_unique<SliderAttachment>(state, "air", airSlider);
    smoothAttachment = std::make_unique<SliderAttachment>(state, "smooth", smoothSlider);
    mixAttachment = std::make_unique<SliderAttachment>(state, "mix", mixSlider);

    startTimerHz(30);
}

void GHOSTAudioProcessorEditor::setupSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setRange(0.0, 1.0, 0.001);
    slider.setDoubleClickReturnValue(true, 0.5);
    slider.setColour(juce::Slider::rotarySliderFillColourId, ghost);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     juce::Colour::fromRGB(46, 50, 60));
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     juce::Colours::transparentBlack);
}

void GHOSTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);

    auto root = getLocalBounds().reduced(18);
    g.setColour(panel);
    g.fillRoundedRectangle(root.toFloat(), 18.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(root.toFloat(), 18.0f, 1.0f);

    auto header = root.removeFromTop(62);
    title.setBounds(header.removeFromTop(38).reduced(20, 0));
    subtitle.setBounds(header.reduced(22, 0));

    auto display = root.removeFromTop(226).reduced(20, 8);
    g.setColour(juce::Colour::fromRGB(10, 12, 16));
    g.fillRoundedRectangle(display.toFloat(), 14.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(display.toFloat(), 14.0f, 1.0f);

    const auto c = display.getCentre().toFloat();
    const float radius = 72.0f;
    const float t = processor.getTransientMeter();
    const float b = processor.getBodyMeter();
    const float tail = processor.getTailMeter();
    const float m = processor.getGhostMeter();

    g.setColour(juce::Colour::fromRGB(32, 35, 43));
    g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    g.setColour(ghost.withAlpha(0.06f + m * 0.18f));
    g.fillEllipse(c.x - radius * (0.68f + 0.10f * b),
                  c.y - radius * (0.68f + 0.10f * b),
                  radius * 2.0f * (0.68f + 0.10f * b),
                  radius * 2.0f * (0.68f + 0.10f * b));

    auto arc = [&] (float value, float start, float turns, float width, float alpha)
    {
        juce::Path p;
        p.addCentredArc(c.x, c.y, radius, radius, 0.0f,
                        juce::MathConstants<float>::twoPi * start,
                        juce::MathConstants<float>::twoPi * (start + turns * value), true);
        g.setColour(ghost.withAlpha(alpha));
        g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved));
    };

    arc(t, 0.66f, 0.34f, 4.0f, 0.98f);
    arc(b, 0.13f, 0.24f, 3.0f, 0.60f);
    arc(tail, 0.93f, 0.24f, 3.0f, 0.42f);

    g.setColour(text);
    g.setFont(18.0f);
    g.drawText("GHOST RESPONSE", display.removeFromTop(30).reduced(20, 0),
               juce::Justification::centredLeft);

    g.setColour(muted);
    g.setFont(10.0f);
    const auto readout =
        "TRANSIENT " + juce::String(t * 100.0f, 0) + "%   "
        + "BODY " + juce::String(b * 100.0f, 0) + "%   "
        + "TAIL " + juce::String(tail * 100.0f, 0) + "%   "
        + "MOTION " + juce::String(m * 100.0f, 0) + "%";
    g.drawText(readout, display.removeFromBottom(24).reduced(20, 0),
               juce::Justification::centred);

    auto grid = root.reduced(8, 4);
    constexpr int columns = 4;
    const int gap = 10;
    const int cellW = (grid.getWidth() - gap * 3) / columns;
    const int cellH = (grid.getHeight() - gap) / 2;

    juce::Slider* s[] = { &ghostSlider, &attackSlider, &bodySlider, &tailSlider,
                          &widthSlider, &airSlider, &smoothSlider, &mixSlider };
    const char* names[] = { "GHOST", "ATTACK", "BODY", "TAIL",
                            "WIDTH", "AIR", "SMOOTH", "MIX" };

    for (int i = 0; i < 8; ++i)
    {
        const int row = i / columns, col = i % columns;
        auto cell = grid.withX(grid.getX() + col * (cellW + gap))
                        .withY(grid.getY() + row * (cellH + gap))
                        .withWidth(cellW).withHeight(cellH);

        auto area = cell.reduced(10, 4);
        s[i]->setBounds(area.removeFromTop(juce::jmax(74, area.getHeight() - 22)));

        g.setColour(muted);
        g.setFont(10.0f);
        g.drawText(names[i], area.removeFromBottom(18),
                   juce::Justification::centred);
    }
}

void GHOSTAudioProcessorEditor::resized()
{
    repaint();
}

void GHOSTAudioProcessorEditor::timerCallback()
{
    repaint();
}
