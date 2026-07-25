#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <cmath>
#include <vector>

namespace chop
{
    //==============================================================================
    // SplitMix64 — fast, well-distributed, stateless hash. Given the same input,
    // every plugin instance (on every track) computes the exact same output, in
    // any order, with no shared memory. That's the entire sync mechanism.
    inline uint64_t splitmix64 (uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Combine (seed, sliceIndex, salt) into one deterministic 64-bit value.
    inline uint64_t hashSlice (uint32_t seed, int64_t sliceIndex, uint32_t salt)
    {
        uint64_t h = splitmix64 ((uint64_t) seed);
        h = splitmix64 (h ^ (uint64_t) sliceIndex);
        h = splitmix64 (h ^ (uint64_t) salt);
        return h;
    }

    // Map a 64-bit hash to [0, 1)
    inline double hashToUnit (uint64_t h)
    {
        return (double) (h >> 11) * (1.0 / 9007199254740992.0); // 2^53
    }

    enum class FadeShape { Linear = 0, Sine = 1 };

    //==============================================================================
    struct SliceBoundary
    {
        double startSec  = 0.0;
        double lengthSec = 0.0;
        int    activeSlot = 0;
    };

    // Result of "what should THIS instance (owning mySlot) be doing right now?"
    // — used by the original multi-track / external-audio-gate mode.
    struct GateState
    {
        bool   active         = false;
        double timeIntoSlice  = 0.0;
        double timeToSliceEnd = 0.0;
        double sliceLength    = 0.0;
    };

    // Result of "which slot is active right now, full stop?" — used by the
    // internal multi-sample mode, where one instance owns every slot itself.
    struct SlotQuery
    {
        int    activeSlot     = 0;
        bool   skipped         = false;
        double timeIntoSlice  = 0.0;
        double timeToSliceEnd = 0.0;
        double sliceLength    = 0.0;
    };

    // A slice as reported for GUI/visualization purposes.
    struct VisSlice
    {
        double startSec  = 0.0;
        double lengthSec = 0.0;
        int    activeSlot = 0;
        bool   skipped     = false;
    };

    //==============================================================================
    // Deterministic seeded slice timeline. Every SliceEngine sharing the same
    // seed, numSlots, baseLengthSec and lengthRandomOctaves computes an
    // IDENTICAL sequence of slice boundaries and active-slot assignments —
    // independent of buffer size, processing order, or which instance/thread
    // asks first. This makes it safe to run a second, independent SliceEngine
    // purely for GUI drawing without any locking against the audio thread's copy.
    class SliceEngine
    {
    public:
        void setSeed (int s)
        {
            auto v = (uint32_t) s;
            if (v != seed) { seed = v; cache.clear(); }
        }

        void setNumSlots (int n)
        {
            n = juce::jlimit (1, 32, n);
            if (n != numSlots) { numSlots = n; cache.clear(); }
        }

        // Which slot this instance owns (external-gate mode only).
        void setMySlot (int s) { mySlot = s; }

        void setBaseLengthSec (double s)
        {
            s = juce::jmax (0.01, s);
            if (std::abs (s - baseLengthSec) > 1.0e-9) { baseLengthSec = s; cache.clear(); }
        }

        void setLengthRandomAmount (double amt01)
        {
            double v = juce::jlimit (0.0, 4.0, amt01 * 4.0);
            if (std::abs (v - lengthRandomOctaves) > 1.0e-9) { lengthRandomOctaves = v; cache.clear(); }
        }

        void setSkipProbability (double p01) { skipProbability = juce::jlimit (0.0, 1.0, p01); }

        void reset() { cache.clear(); }

        int getNumSlots() const { return numSlots; }

        bool isSliceSkipped (int sliceIndex) const
        {
            if (skipProbability <= 0.0)
                return false;
            double u = hashToUnit (hashSlice (seed, sliceIndex, 777u));
            return u < skipProbability;
        }

        // External-gate mode: "is MY slot the one playing right now?"
        GateState queryAtTime (double timeSec)
        {
            int idx = findSliceIndexForTime (timeSec);
            const SliceBoundary& b = getSlice (idx);
            int effSlot = ((mySlot % numSlots) + numSlots) % numSlots;

            GateState g;
            g.sliceLength    = b.lengthSec;
            g.timeIntoSlice  = timeSec - b.startSec;
            g.timeToSliceEnd = b.lengthSec - g.timeIntoSlice;

            bool isMine = (b.activeSlot == effSlot);
            bool skip = isMine && isSliceSkipped (idx);

            g.active = isMine && ! skip;
            return g;
        }

        // Internal multi-sample mode: "which slot is active right now?"
        SlotQuery queryActiveSlot (double timeSec)
        {
            int idx = findSliceIndexForTime (timeSec);
            const SliceBoundary& b = getSlice (idx);

            SlotQuery q;
            q.activeSlot     = b.activeSlot;
            q.sliceLength    = b.lengthSec;
            q.timeIntoSlice  = timeSec - b.startSec;
            q.timeToSliceEnd = b.lengthSec - q.timeIntoSlice;
            q.skipped        = isSliceSkipped (idx);
            return q;
        }

        // For GUI drawing: every slice whose span overlaps [startSec, endSec].
        std::vector<VisSlice> getSlicesInRange (double startSec, double endSec)
        {
            std::vector<VisSlice> result;
            if (endSec <= startSec)
                return result;

            int idx = findSliceIndexForTime (juce::jmax (0.0, startSec));
            while (true)
            {
                const SliceBoundary& b = getSlice (idx);
                if (b.startSec > endSec)
                    break;

                VisSlice v;
                v.startSec   = b.startSec;
                v.lengthSec  = b.lengthSec;
                v.activeSlot = b.activeSlot;
                v.skipped    = isSliceSkipped (idx);
                result.push_back (v);

                if (b.startSec + b.lengthSec > endSec)
                    break;
                ++idx;
                if (result.size() > 1000) // safety cap
                    break;
            }
            return result;
        }

    private:
        const SliceBoundary& getSlice (int idx)
        {
            if (idx < (int) cache.size())
                return cache[(size_t) idx];

            double t = cache.empty() ? 0.0 : (cache.back().startSec + cache.back().lengthSec);
            int start = (int) cache.size();

            for (int i = start; i <= idx; ++i)
            {
                SliceBoundary b;
                b.startSec = t;

                uint64_t hLen = hashSlice (seed, i, 1u);
                double u = hashToUnit (hLen) * 2.0 - 1.0; // [-1, 1)
                double factor = std::pow (2.0, u * lengthRandomOctaves);
                b.lengthSec = juce::jmax (0.01, baseLengthSec * factor);

                uint64_t hSlot = hashSlice (seed, i, 2u);
                b.activeSlot = (int) (hSlot % (uint64_t) juce::jmax (1, numSlots));

                cache.push_back (b);
                t += b.lengthSec;
            }

            return cache[(size_t) idx];
        }

        int findSliceIndexForTime (double timeSec)
        {
            if (timeSec < 0.0)
                timeSec = 0.0;

            if (! cache.empty() && timeSec < cache.front().startSec)
                cache.clear();

            int idx = cache.empty() ? 0 : (int) cache.size() - 1;
            while (true)
            {
                const SliceBoundary& b = getSlice (idx);
                if (timeSec < b.startSec + b.lengthSec)
                    return idx;
                ++idx;
            }
        }

        uint32_t seed = 0;
        int numSlots = 3;
        int mySlot = 0;
        double baseLengthSec = 0.5;
        double lengthRandomOctaves = 0.0;
        double skipProbability = 0.0;

        std::vector<SliceBoundary> cache;
    };
}
