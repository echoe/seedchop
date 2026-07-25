#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SliceTimelineComponent.h"
#include "SlotColours.h"

class SeedChopAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SeedChopAudioProcessorEditor (SeedChopAudioProcessor&);
    ~SeedChopAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override; // refreshes sample-slot labels/enablement

    SeedChopAudioProcessor& audioProcessor;

    // --- generic parameter rows (slider or combo) ---
    struct ParamRow
    {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    };
    std::vector<std::unique_ptr<ParamRow>> paramRows;
    ParamRow& addSliderRow (const juce::String& paramID, const juce::String& labelText);
    ParamRow& addComboRow (const juce::String& paramID, const juce::String& labelText);

    juce::ComboBox* sourceModeCombo = nullptr;

    // --- sample slot rows ---
    struct ColourSwatch : public juce::Component
    {
        juce::Colour colour;
        void paint (juce::Graphics& g) override
        {
            g.setColour (colour);
            g.fillRect (getLocalBounds().reduced (2));
        }
    };

    struct SampleRow
    {
        ColourSwatch swatch;
        juce::Label nameLabel;
        juce::TextButton loadButton { "Load..." };
        juce::TextButton clearButton { "X" };
    };
    std::array<std::unique_ptr<SampleRow>, SeedChopAudioProcessor::kMaxSamples> sampleRows;
    std::array<std::unique_ptr<juce::FileChooser>, SeedChopAudioProcessor::kMaxSamples> choosers;

    void buildSampleRows();
    void openChooserForSlot (int slotIndex);
    void refreshSampleRowLabels();

    // --- timeline ---
    SliceTimelineComponent timeline;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedChopAudioProcessorEditor)
};
