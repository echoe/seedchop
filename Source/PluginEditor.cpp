#include "PluginEditor.h"

SeedChopAudioProcessorEditor::SeedChopAudioProcessorEditor (SeedChopAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), timeline (p)
{
    auto& modeRow = addComboRow ("sourceMode", "Source");
    sourceModeCombo = modeRow.combo.get();

    buildSampleRows();

    addSliderRow ("seed", "Seed");
    addComboRow  ("chopMultiplier", "Chop Multiplier (ext. mode)");
    addSliderRow ("mySlot", "My Slot (ext. mode)");
    addSliderRow ("sliceLengthMs", "Slice Length (ms)");
    addSliderRow ("lengthRandom", "Length Randomness (%)");
    addSliderRow ("skipProbability", "Skip Probability (%)");
    addSliderRow ("fadeTimeMs", "Fade Time (ms)");
    addComboRow  ("fadeShape", "Fade Shape");
    addSliderRow ("dryWet", "Dry/Wet (%)");

    addAndMakeVisible (timeline);

    setSize (460, 60 + SeedChopAudioProcessor::kMaxSamples * 26 + (int) paramRows.size() * 30 + 150);
    setResizable (true, true);
    setResizeLimits (420, 500, 900, 1200);

    startTimerHz (5); // periodically refresh sample-slot labels
}

//==============================================================================
SeedChopAudioProcessorEditor::ParamRow& SeedChopAudioProcessorEditor::addSliderRow (const juce::String& paramID,
                                                                                     const juce::String& labelText)
{
    auto row = std::make_unique<ParamRow>();
    row->label.setText (labelText, juce::dontSendNotification);
    row->label.setJustificationType (juce::Justification::right);
    addAndMakeVisible (row->label);

    row->slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    addAndMakeVisible (*row->slider);

    row->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, paramID, *row->slider);

    paramRows.push_back (std::move (row));
    return *paramRows.back();
}

SeedChopAudioProcessorEditor::ParamRow& SeedChopAudioProcessorEditor::addComboRow (const juce::String& paramID,
                                                                                    const juce::String& labelText)
{
    auto row = std::make_unique<ParamRow>();
    row->label.setText (labelText, juce::dontSendNotification);
    row->label.setJustificationType (juce::Justification::right);
    addAndMakeVisible (row->label);

    row->combo = std::make_unique<juce::ComboBox>();
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (paramID)))
    {
        int idx = 1;
        for (auto& choice : choiceParam->choices)
            row->combo->addItem (choice, idx++);
    }
    addAndMakeVisible (*row->combo);

    row->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.apvts, paramID, *row->combo);

    paramRows.push_back (std::move (row));
    return *paramRows.back();
}

//==============================================================================
void SeedChopAudioProcessorEditor::buildSampleRows()
{
    for (int i = 0; i < SeedChopAudioProcessor::kMaxSamples; ++i)
    {
        auto row = std::make_unique<SampleRow>();
        row->swatch.colour = chop::slotColour (i);
        addAndMakeVisible (row->swatch);

        row->nameLabel.setText ("(empty)", juce::dontSendNotification);
        row->nameLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (row->nameLabel);

        row->loadButton.onClick = [this, i] { openChooserForSlot (i); };
        addAndMakeVisible (row->loadButton);

        row->clearButton.onClick = [this, i]
        {
            audioProcessor.clearSample (i);
            refreshSampleRowLabels();
        };
        addAndMakeVisible (row->clearButton);

        sampleRows[(size_t) i] = std::move (row);
    }
    refreshSampleRowLabels();
}

void SeedChopAudioProcessorEditor::openChooserForSlot (int slotIndex)
{
    choosers[(size_t) slotIndex] = std::make_unique<juce::FileChooser> (
        "Load sample into slot " + juce::String (slotIndex + 1),
        juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    choosers[(size_t) slotIndex]->launchAsync (flags, [this, slotIndex] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.loadSample (slotIndex, file);
        refreshSampleRowLabels();
    });
}

void SeedChopAudioProcessorEditor::refreshSampleRowLabels()
{
    for (int i = 0; i < SeedChopAudioProcessor::kMaxSamples; ++i)
    {
        auto& row = *sampleRows[(size_t) i];
        if (audioProcessor.isSampleLoaded (i))
        {
            row.nameLabel.setText (audioProcessor.getSampleName (i), juce::dontSendNotification);
            row.nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        }
        else
        {
            row.nameLabel.setText ("(empty)", juce::dontSendNotification);
            row.nameLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
        }
    }
}

void SeedChopAudioProcessorEditor::timerCallback()
{
    refreshSampleRowLabels();
}

//==============================================================================
void SeedChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawText ("SeedChop", getLocalBounds().removeFromTop (30), juce::Justification::centred);
}

void SeedChopAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop (34).reduced (10);

    // Source mode row lives in paramRows[0] (added first) but we want it up top
    // followed by the sample list, so lay it out manually.
    auto modeRowBounds = bounds.removeFromTop (28);
    paramRows[0]->label.setBounds (modeRowBounds.removeFromLeft (150));
    modeRowBounds.removeFromLeft (6);
    paramRows[0]->combo->setBounds (modeRowBounds);
    bounds.removeFromTop (8);

    // Sample slot rows
    for (auto& row : sampleRows)
    {
        auto r = bounds.removeFromTop (24);
        row->swatch.setBounds (r.removeFromLeft (16));
        r.removeFromLeft (6);
        row->clearButton.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        row->loadButton.setBounds (r.removeFromRight (64));
        r.removeFromRight (6);
        row->nameLabel.setBounds (r);
        bounds.removeFromTop (2);
    }
    bounds.removeFromTop (8);

    // Remaining parameter rows (skip index 0, the source-mode row already placed)
    for (size_t i = 1; i < paramRows.size(); ++i)
    {
        auto& row = paramRows[i];
        auto r = bounds.removeFromTop (28);
        row->label.setBounds (r.removeFromLeft (170));
        r.removeFromLeft (6);
        if (row->slider != nullptr)
            row->slider->setBounds (r);
        else if (row->combo != nullptr)
            row->combo->setBounds (r);
        bounds.removeFromTop (2);
    }

    bounds.removeFromTop (8);
    timeline.setBounds (bounds.removeFromBottom (juce::jmax (100, bounds.getHeight())));
}
