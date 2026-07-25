#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include "SliceEngine.h"

class SeedChopAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxSamples = 8;

    SeedChopAudioProcessor();
    ~SeedChopAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SeedChop"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Sample bank — used when "Source" == "Loaded Samples". Loading is done on
    // the message thread (via loadSample, typically from a FileChooser
    // callback); a SpinLock guards the brief pointer swap against the audio
    // thread's read.
    bool loadSample (int slotIndex, const juce::File& file);
    void clearSample (int slotIndex);
    juce::String getSampleName (int slotIndex) const;
    bool isSampleLoaded (int slotIndex) const;

    // Number of slots the engine should currently use: for external-gate mode
    // this comes from the Chop Multiplier param; for sample mode it's the
    // count of contiguously-loaded sample slots (starting at slot 0).
    int getEffectiveNumSlots() const;

    // Updated at the start of every processBlock; safe to read from the
    // message thread for GUI drawing.
    std::atomic<double> currentTimeSec { 0.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateEngineFromParams();

    chop::SliceEngine engine;
    double sampleRate = 44100.0;
    juce::int64 samplesElapsedFallback = 0;

    std::atomic<float>* seedParam        = nullptr;
    juce::AudioParameterChoice* multiplierParam = nullptr;
    std::atomic<float>* mySlotParam      = nullptr;
    std::atomic<float>* sliceLengthParam = nullptr;
    std::atomic<float>* lengthRandomParam = nullptr;
    std::atomic<float>* skipProbParam    = nullptr;
    std::atomic<float>* fadeTimeParam    = nullptr;
    juce::AudioParameterChoice* fadeShapeParam  = nullptr;
    juce::AudioParameterChoice* sourceModeParam = nullptr;

    struct SampleSlot
    {
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        double sourceSampleRate = 44100.0;
        juce::String name;
    };

    std::array<SampleSlot, kMaxSamples> sampleSlots;
    mutable juce::SpinLock sampleLock;
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedChopAudioProcessor)
};
