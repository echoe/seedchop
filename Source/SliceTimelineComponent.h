#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "SliceEngine.h"
#include "SlotColours.h"

// Draws a scrolling strip of upcoming/past slices colour-coded by active
// slot, a live playhead, and a legend mapping colours to slot/sample names.
//
// Runs its own independent SliceEngine, reseeded from the processor's APVTS
// values every repaint. Because slice data is a pure function of
// (seed, sliceIndex, params), this engine always agrees with the audio
// thread's copy without needing any locking between them.
class SliceTimelineComponent : public juce::Component, private juce::Timer
{
public:
    explicit SliceTimelineComponent (SeedChopAudioProcessor& p) : processor (p)
    {
        startTimerHz (30);
    }

    ~SliceTimelineComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        auto legendArea = bounds.removeFromBottom (26);
        auto timelineArea = bounds.reduced (0, 2);

        g.fillAll (juce::Colour (0xff141414));

        syncEngineFromParams();

        const double now = processor.currentTimeSec.load();
        const double lookback = 1.0;
        const double lookahead = 4.0;
        const double windowStart = juce::jmax (0.0, now - lookback);
        const double windowEnd = now + lookahead;
        const double pxPerSec = (double) timelineArea.getWidth() / (lookback + lookahead);

        auto slices = visEngine.getSlicesInRange (windowStart, windowEnd);
        for (auto& s : slices)
        {
            float x0 = (float) timelineArea.getX() + (float) ((s.startSec - windowStart) * pxPerSec);
            float w  = (float) (s.lengthSec * pxPerSec);
            juce::Rectangle<float> r (x0, (float) timelineArea.getY(),
                                       juce::jmax (1.0f, w - 1.0f), (float) timelineArea.getHeight());

            auto colour = s.skipped ? juce::Colours::black.brighter (0.15f) : chop::slotColour (s.activeSlot);
            g.setColour (colour);
            g.fillRect (r);

            if (s.skipped)
            {
                g.setColour (juce::Colours::white.withAlpha (0.15f));
                for (float x = r.getX(); x < r.getRight(); x += 6.0f)
                    g.drawLine (x, r.getY(), x, r.getBottom(), 1.0f);
            }
        }

        // Playhead
        float playX = (float) timelineArea.getX() + (float) (lookback * pxPerSec);
        g.setColour (juce::Colours::white);
        g.drawLine (playX, (float) timelineArea.getY(), playX, (float) timelineArea.getBottom(), 2.0f);

        // Legend
        const bool sampleMode = processor.apvts.getParameter ("sourceMode") != nullptr
                                 && dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("sourceMode"))->getIndex() == 1;
        int numSlots = processor.getEffectiveNumSlots();
        int mySlot = (int) *processor.apvts.getRawParameterValue ("mySlot");

        int x = legendArea.getX();
        const int swatchSize = 12;
        g.setFont (12.0f);
        for (int i = 0; i < numSlots && i < SeedChopAudioProcessor::kMaxSamples; ++i)
        {
            juce::Rectangle<int> swatch (x, legendArea.getCentreY() - swatchSize / 2, swatchSize, swatchSize);
            g.setColour (chop::slotColour (i));
            g.fillRect (swatch);

            if (! sampleMode && i == (((mySlot % numSlots) + numSlots) % numSlots))
            {
                g.setColour (juce::Colours::white);
                g.drawRect (swatch.expanded (2), 1);
            }

            x += swatchSize + 4;

            juce::String label = sampleMode ? processor.getSampleName (i) : ("Slot " + juce::String (i));
            if (label.isEmpty())
                label = "Slot " + juce::String (i);

            int textWidth = juce::jmin (120, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 4);
            g.setColour (juce::Colours::lightgrey);
            g.drawText (label, x, legendArea.getY(), textWidth, legendArea.getHeight(),
                        juce::Justification::centredLeft, true);
            x += textWidth + 14;

            if (x > legendArea.getRight() - 20)
                break;
        }
    }

private:
    void timerCallback() override { repaint(); }

    void syncEngineFromParams()
    {
        auto& apvts = processor.apvts;
        visEngine.setSeed ((int) *apvts.getRawParameterValue ("seed"));
        visEngine.setNumSlots (processor.getEffectiveNumSlots());
        visEngine.setBaseLengthSec ((double) *apvts.getRawParameterValue ("sliceLengthMs") / 1000.0);
        visEngine.setLengthRandomAmount ((double) *apvts.getRawParameterValue ("lengthRandom") / 100.0);
        visEngine.setSkipProbability ((double) *apvts.getRawParameterValue ("skipProbability") / 100.0);
    }

    SeedChopAudioProcessor& processor;
    chop::SliceEngine visEngine; // independent copy, never touched by the audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceTimelineComponent)
};
