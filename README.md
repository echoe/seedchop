# SeedChop

<img src=https://raw.githubusercontent.com/echoe/seedchop/refs/heads/main/picture.png width="450" height="360" />

An idea from https://freesound.org/people/Sadiquecat/ that I was able to just turn into something! It's a seeded, slot-based chopper/tremolo/muter. Put one instance on each of a number of sounds, give them the same **Seed** / **Chop Multiplier** / **Slice Length** /
**Length Randomness**, and different **My Slot** values — only one sound
plays at any moment, cycling deterministically between them.

## How it works

Every instance walks the same imaginary timeline: it's cut into "slices"
whose length and "which slot owns it" are both derived from
`hash(seed, sliceIndex)`. Because it's a pure function (not a running RNG
each instance advances independently), any instance can compute any slice's
data on demand and every instance agrees — no sync cable, no host
automation link needed. Change the seed and you get a different pattern;
same seed always reproduces the same pattern.

## Two modes

**External Audio (Gate)** — the original design: run one instance per track,
each gating that track's incoming audio. Use **My Slot** to assign which
slot each instance owns; **Chop Multiplier** sets the number of slots.

**Loaded Samples** — one instance owns everything. Load up to 8 audio files
into the sample slots (top of the GUI); the plugin generates its own output,
switching between them according to the same seeded slice timeline. Each
loaded sample plays continuously in the background, looping at its own
native length from a position computed directly from host time — so when a
slot becomes active again later, it resumes exactly where it "would have
been," the same way independent looping tracks behave in the Audacity
technique. No separate track routing needed. Samples are embedded in the
plugin's saved state (base64 WAV, same approach as OAO's sample operator),
so projects stay portable even if the original files move.

In this mode, **Chop Multiplier** and **My Slot** are ignored — the number
of slots is just the number of loaded samples (fill slots 0, 1, 2... with no
gaps).

## GUI

The bottom strip is a live timeline: upcoming/past slices colour-coded by
active slot, a white playhead marker, and skipped (rested) slices shown
dark and hatched. The legend beneath it maps each colour to a slot number
or loaded sample name; in External Audio mode, this instance's own slot is
outlined.

## Parameters

- **Source** — External Audio (Gate) or Loaded Samples.
- **Seed** — the pattern identity. Same seed across instances = synced.
- **Chop Multiplier** — number of slots (1/3 = 3 slots = ~33% average
  spotlight time per instance, matching your Audacity workflow).
- **My Slot** — which slot *this* instance owns (0, 1, 2, ... — this is
  the generalized version of your 0°/180° phase trick).
- **Slice Length** — base duration of a slice.
- **Length Randomness** — 0% = every slice is exactly Slice Length.
  Higher values allow multiplicative (octave-style) jitter, so you can get
  everything from a clean 1s cycle to "0.1s then 4s" chaos from one knob.
- **Skip Probability** — chance that, on a slice that belongs to your slot,
  it rests anyway (true silence that turn, not handed to another instance).
- **Fade Time / Fade Shape** — edge crossfade at slice boundaries (0 = hard
  cut; Linear or Sine ramps for funkier
  tremolo-style transitions).
- **Wet / Dry ** - sets the wet or dry amount of the plugin per instance.

## Workflow

1. Drop one instance on each of 2–4 tracks you want to interleave.
2. Set the same Seed, Chop Multiplier, Slice Length, and Length Randomness
   on all of them.
3. Set My Slot to 0, 1, 2... (one distinct value per track).
4. Chop Multiplier should generally equal the number of tracks (3 tracks →
   1/3) so the slots exactly partition the timeline — though nothing stops
   you from mismatching them for weirder overlap/gap behavior.

## Sync notes

- Uses the host's transport time (seconds) as the shared clock, so sync
  holds regardless of buffer size or plugin processing order — as long as
  all instances share the same host transport (normal DAW use).
- In a host without playhead info (or standalone), it falls back to an
  internal sample counter — still consistent across instances as long as
  they're all started together.
- Seeking far backward in a long track triggers a cache rebuild from t=0
  in that instance; cheap per slice, but could be optimized further
  (analytic index estimate + local search) if you're scrubbing long files
  a lot.

## Build

```
cmake -B build -G Ninja
cmake --build build
```
