#pragma once
#include <juce_graphics/juce_graphics.h>

namespace chop
{
    // Fixed palette so a given slot index always reads as the same colour
    // across the sample-slot list and the timeline view.
    inline juce::Colour slotColour (int index)
    {
        static const juce::Colour palette[] = {
            juce::Colour (0xffE94F37), juce::Colour (0xff3F88C5), juce::Colour (0xffFFC857),
            juce::Colour (0xff44AF69), juce::Colour (0xffB980F0), juce::Colour (0xffF77F00),
            juce::Colour (0xff2EC4B6), juce::Colour (0xffE84393)
        };
        int i = ((index % 8) + 8) % 8;
        return palette[(size_t) i];
    }
}
