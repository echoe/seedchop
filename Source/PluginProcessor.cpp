#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Index must match the order of choices in "chopMultiplier".
    constexpr int kDenominators[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };

    float computeFadeGain (double timeIntoSlice, double timeToSliceEnd, double fadeTimeSec, bool sineFade)
    {
        float gain = 1.0f;
        if (fadeTimeSec > 0.0)
        {
            if (timeIntoSlice < fadeTimeSec)
            {
                double p = timeIntoSlice / fadeTimeSec;
                gain = (float) (sineFade ? std::sin (p * juce::MathConstants<double>::halfPi) : p);
            }
            else if (timeToSliceEnd < fadeTimeSec)
            {
                double p = timeToSliceEnd / fadeTimeSec;
                gain = (float) (sineFade ? std::sin (p * juce::MathConstants<double>::halfPi) : p);
            }
        }
        return gain;
    }

    // Reads `src` at the position implied by `timeSec`, looping continuously
    // at the sample's own native duration — no stored playhead, so this is
    // just as stateless/deterministic as the slice timeline itself. Adds
    // (not overwrites) into dst so callers can just clear() once per block.
    void addInterpolatedSample (const juce::AudioBuffer<float>& src, double srcRate,
                                 double timeSec, juce::AudioBuffer<float>& dst,
                                 int dstSampleIndex, float gain)
    {
        const int srcChannels = src.getNumChannels();
        const int srcLength = src.getNumSamples();
        if (srcLength <= 0 || srcChannels <= 0 || srcRate <= 0.0)
            return;

        double durationSec = (double) srcLength / srcRate;
        double posInSource = std::fmod (timeSec, durationSec);
        if (posInSource < 0.0)
            posInSource += durationSec;
        double posSamples = posInSource * srcRate;

        int i0 = (int) posSamples;
        if (i0 >= srcLength) i0 = srcLength - 1;
        int i1 = (i0 + 1) % srcLength;
        float frac = (float) (posSamples - (double) i0);

        const int dstChannels = dst.getNumChannels();
        for (int ch = 0; ch < dstChannels; ++ch)
        {
            int srcCh = juce::jmin (ch, srcChannels - 1); // mono->all, stereo direct, extra channels reuse last
            float s0 = src.getSample (srcCh, i0);
            float s1 = src.getSample (srcCh, i1);
            float value = s0 + frac * (s1 - s0);
            dst.getWritePointer (ch)[dstSampleIndex] += value * gain;
        }
    }
}

SeedChopAudioProcessor::SeedChopAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    seedParam         = apvts.getRawParameterValue ("seed");
    multiplierParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("chopMultiplier"));
    mySlotParam       = apvts.getRawParameterValue ("mySlot");
    sliceLengthParam  = apvts.getRawParameterValue ("sliceLengthMs");
    lengthRandomParam = apvts.getRawParameterValue ("lengthRandom");
    skipProbParam     = apvts.getRawParameterValue ("skipProbability");
    fadeTimeParam     = apvts.getRawParameterValue ("fadeTimeMs");
    fadeShapeParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("fadeShape"));
    sourceModeParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("sourceMode"));
    dryWetParam       = apvts.getRawParameterValue ("dryWet");

    formatManager.registerBasicFormats();
}

juce::AudioProcessorValueTreeState::ParameterLayout SeedChopAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "sourceMode", 1 }, "Source",
        juce::StringArray { "External Audio (Gate)", "Loaded Samples" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 }, "Seed", 0, 999999, 0));

    // Only used in External Audio mode; in Loaded Samples mode the number of
    // slots is derived from how many samples are loaded instead.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "chopMultiplier", 1 }, "Chop Multiplier",
        juce::StringArray { "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32" },
        2));

    // Only used in External Audio mode: which slot this instance owns.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "mySlot", 1 }, "My Slot", 0, 31, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sliceLengthMs", 1 }, "Slice Length",
        juce::NormalisableRange<float> (20.0f, 4000.0f, 1.0f, 0.4f), 500.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lengthRandom", 1 }, "Length Randomness",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "skipProbability", 1 }, "Skip Probability",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fadeTimeMs", 1 }, "Fade Time",
        juce::NormalisableRange<float> (0.0f, 500.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fadeShape", 1 }, "Fade Shape",
        juce::StringArray { "Linear", "Sine" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dryWet", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return { params.begin(), params.end() };
}

void SeedChopAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    samplesElapsedFallback = 0;
    engine.reset();
    dryBuffer.setSize (getTotalNumInputChannels(), samplesPerBlock);
}

int SeedChopAudioProcessor::getEffectiveNumSlots() const
{
    bool sampleMode = sourceModeParam->getIndex() == 1;
    if (! sampleMode)
    {
        int denomIdx = multiplierParam->getIndex();
        return kDenominators[juce::jlimit (0, (int) juce::numElementsInArray (kDenominators) - 1, denomIdx)];
    }

    int count = 0;
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    for (auto& s : sampleSlots)
    {
        if (s.buffer == nullptr)
            break; // slots must be filled contiguously from 0
        ++count;
    }
    return juce::jmax (1, count);
}

void SeedChopAudioProcessor::updateEngineFromParams()
{
    engine.setSeed ((int) *seedParam);
    engine.setNumSlots (getEffectiveNumSlots());
    engine.setMySlot ((int) *mySlotParam);
    engine.setBaseLengthSec ((double) *sliceLengthParam / 1000.0);
    engine.setLengthRandomAmount ((double) *lengthRandomParam / 100.0);
    engine.setSkipProbability ((double) *skipProbParam / 100.0);
}

void SeedChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateEngineFromParams();

    const int numSamples = buffer.getNumSamples();

    double blockStartTimeSec = (double) samplesElapsedFallback / sampleRate;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            auto t = pos->getTimeInSeconds();
            if (t.hasValue())
                blockStartTimeSec = *t;
        }
    }
    currentTimeSec.store (blockStartTimeSec);

    const double fadeTimeSec = juce::jmax (0.0, (double) fadeTimeParam->load() / 1000.0);
    const bool sineFade = fadeShapeParam->getIndex() == 1;
    const bool sampleMode = sourceModeParam->getIndex() == 1;
    const float wetMix = juce::jlimit (0.0f, 1.0f, dryWetParam->load() / 100.0f);
    const bool needsDryBlend = wetMix < 1.0f;

    if (needsDryBlend)
        dryBuffer.makeCopyOf (buffer, true);

    if (sampleMode)
    {
        buffer.clear();

        const juce::SpinLock::ScopedTryLockType tryLock (sampleLock);
        if (tryLock.isLocked()) // if a sample is mid-load, just output silence this block
        {
            for (int s = 0; s < numSamples; ++s)
            {
                const double t = blockStartTimeSec + (double) s / sampleRate;
                auto q = engine.queryActiveSlot (t);
                if (q.skipped)
                    continue;

                auto& slot = sampleSlots[(size_t) juce::jlimit (0, kMaxSamples - 1, q.activeSlot)];
                if (slot.buffer == nullptr)
                    continue;

                float gain = computeFadeGain (q.timeIntoSlice, q.timeToSliceEnd, fadeTimeSec, sineFade);
                addInterpolatedSample (*slot.buffer, slot.sourceSampleRate, t, buffer, s, gain);
            }
        }
    }
    else
    {
        const int numChannels = buffer.getNumChannels();
        for (int s = 0; s < numSamples; ++s)
        {
            const double t = blockStartTimeSec + (double) s / sampleRate;
            auto gate = engine.queryAtTime (t);
            float gain = gate.active
                             ? computeFadeGain (gate.timeIntoSlice, gate.timeToSliceEnd, fadeTimeSec, sineFade)
                             : 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[s] *= gain;
        }
    }

    if (needsDryBlend)
    {
        const float dryMix = 1.0f - wetMix;
        const int numChannels = buffer.getNumChannels();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer (ch);
            auto* dry = dryBuffer.getReadPointer (ch);
            for (int s = 0; s < numSamples; ++s)
                wet[s] = wet[s] * wetMix + dry[s] * dryMix;
        }
    }

    samplesElapsedFallback += numSamples;
}

bool SeedChopAudioProcessor::loadSample (int slotIndex, const juce::File& file)
{
    if (slotIndex < 0 || slotIndex >= kMaxSamples)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    auto newBuffer = std::make_unique<juce::AudioBuffer<float>> ((int) reader->numChannels,
                                                                   (int) reader->lengthInSamples);
    reader->read (newBuffer.get(), 0, (int) reader->lengthInSamples, 0, true, true);

    const juce::SpinLock::ScopedLockType sl (sampleLock);
    sampleSlots[(size_t) slotIndex].buffer = std::move (newBuffer);
    sampleSlots[(size_t) slotIndex].sourceSampleRate = reader->sampleRate;
    sampleSlots[(size_t) slotIndex].name = file.getFileNameWithoutExtension();
    return true;
}

void SeedChopAudioProcessor::clearSample (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kMaxSamples)
        return;
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    sampleSlots[(size_t) slotIndex].buffer.reset();
    sampleSlots[(size_t) slotIndex].name.clear();
}

juce::String SeedChopAudioProcessor::getSampleName (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kMaxSamples)
        return {};
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return sampleSlots[(size_t) slotIndex].name;
}

bool SeedChopAudioProcessor::isSampleLoaded (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kMaxSamples)
        return false;
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return sampleSlots[(size_t) slotIndex].buffer != nullptr;
}

juce::AudioProcessorEditor* SeedChopAudioProcessor::createEditor()
{
    return new SeedChopAudioProcessorEditor (*this);
}

void SeedChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Samples are embedded as base64 WAV, same pattern as OAO's sample
    // operator — keeps the plugin state self-contained (no dangling file
    // paths if the original files move or the project is shared).
    auto* samplesXml = xml->createNewChildElement ("SAMPLES");
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        for (int i = 0; i < kMaxSamples; ++i)
        {
            auto& slot = sampleSlots[(size_t) i];
            if (slot.buffer == nullptr)
                continue;

            juce::MemoryBlock mb;
            auto stream = std::make_unique<juce::MemoryOutputStream> (mb, false);
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFormat.createWriterFor (std::move (stream),
                    juce::AudioFormatWriterOptions()
                        .withSampleRate (slot.sourceSampleRate)
                        .withNumChannels (slot.buffer->getNumChannels())
                        .withBitsPerSample (16)));
            if (writer != nullptr)
            {
                writer->writeFromAudioSampleBuffer (*slot.buffer, 0, slot.buffer->getNumSamples());
                writer.reset(); // flushes; mb now holds the WAV bytes

                auto* sampleXml = samplesXml->createNewChildElement ("SAMPLE");
                sampleXml->setAttribute ("slot", i);
                sampleXml->setAttribute ("name", slot.name);
                sampleXml->addTextElement (juce::Base64::toBase64 (mb.getData(), mb.getSize()));
            }
        }
    }

    copyXmlToBinary (*xml, destData);
}

void SeedChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (auto* samplesXml = xml->getChildByName ("SAMPLES"))
    {
        for (auto* sampleXml : samplesXml->getChildIterator())
        {
            int slotIndex = sampleXml->getIntAttribute ("slot", -1);
            if (slotIndex < 0 || slotIndex >= kMaxSamples)
                continue;

            juce::MemoryBlock mb;
            mb.fromBase64Encoding (sampleXml->getAllSubText());

            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatReader> reader (
                wavFormat.createReaderFor (new juce::MemoryInputStream (mb, false), true));
            if (reader == nullptr)
                continue;

            auto newBuffer = std::make_unique<juce::AudioBuffer<float>> ((int) reader->numChannels,
                                                                           (int) reader->lengthInSamples);
            reader->read (newBuffer.get(), 0, (int) reader->lengthInSamples, 0, true, true);

            const juce::SpinLock::ScopedLockType sl (sampleLock);
            sampleSlots[(size_t) slotIndex].buffer = std::move (newBuffer);
            sampleSlots[(size_t) slotIndex].sourceSampleRate = reader->sampleRate;
            sampleSlots[(size_t) slotIndex].name = sampleXml->getStringAttribute ("name");
        }
    }
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SeedChopAudioProcessor();
}
