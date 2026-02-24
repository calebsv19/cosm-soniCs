# Effects Panel Standardization Status

This document tracks effects that have been migrated to the spec-driven panel and preview UI.

## Status legend
- Spec UI: uses spec-driven panel widgets.
- Preview: supports the bottom preview panel (history/visualization).

## Effects

### Basics
- Gain (id 1): Spec UI = yes, Preview = no
- DC Block (id 2): Spec UI = yes, Preview = no
- Pan (id 3): Spec UI = yes, Preview = no
- Mute (id 4): Spec UI = yes, Preview = no
- Mono Maker Low (id 5): Spec UI = yes, Preview = no
- Stereo Blend (id 6): Spec UI = yes, Preview = no
- AutoTrim (id 7): Spec UI = yes, Preview = yes (gain adjustment history)

### Dynamics
- Compressor (id 20): Spec UI = yes, Preview = yes (gain reduction history)
- Limiter (id 21): Spec UI = yes, Preview = yes (gain reduction history)
- Gate (id 22): Spec UI = yes, Preview = yes (gain reduction history)
- DeEsser (id 23): Spec UI = yes, Preview = yes (gain reduction history)
- SCCompressor (id 24): Spec UI = yes, Preview = yes (history)
- UpwardComp (id 25): Spec UI = yes, Preview = yes (history)
- Expander (id 26): Spec UI = yes, Preview = yes (history)
- TransientShaper (id 27): Spec UI = yes, Preview = yes (history)

### EQ
- BiquadEQ (id 30): Spec UI = yes, Preview = yes (response)
- EQ_Fixed3 (id 31): Spec UI = yes, Preview = yes (response)
- EQ_Notch (id 32): Spec UI = yes, Preview = yes (response)
- EQ_Tilt (id 33): Spec UI = yes, Preview = yes (response)

### Filter & Tone
- SVF (id 40): Spec UI = yes, Preview = yes (response)
- AutoWah (id 41): Spec UI = yes, Preview = yes (response)
- StereoWidth (id 42): Spec UI = yes, Preview = no
- TiltEQ (id 43): Spec UI = yes, Preview = yes (response)

### Delay
- Delay (id 50): Spec UI = yes, Preview = yes (echoes)
- PingPongDelay (id 51): Spec UI = yes, Preview = yes (echoes)
- MultiTapDelay (id 52): Spec UI = yes, Preview = yes (taps)
- TapeEcho (id 53): Spec UI = yes, Preview = yes (echoes)
- DiffusionDelay (id 54): Spec UI = yes, Preview = yes (echoes)

### Distortion
- HardClip (id 60): Spec UI = yes, Preview = yes (curve)
- SoftSaturation (id 61): Spec UI = yes, Preview = yes (curve)
- BitCrusher (id 62): Spec UI = yes, Preview = yes (curve)
- Overdrive (id 63): Spec UI = yes, Preview = yes (curve)
- Waveshaper (id 64): Spec UI = yes, Preview = yes (curve)
- Decimator (id 65): Spec UI = yes, Preview = yes (curve)

### Modulation
- TremoloPan (id 70): Spec UI = yes, Preview = yes (lfo)
- Chorus (id 71): Spec UI = yes, Preview = yes (lfo)
- Flanger (id 72): Spec UI = yes, Preview = yes (lfo)
- Vibrato (id 73): Spec UI = yes, Preview = yes (lfo)
- RingMod (id 74): Spec UI = yes, Preview = yes (lfo)
- AutoPan (id 75): Spec UI = yes, Preview = yes (lfo)
- BarberpolePhaser (id 76): Spec UI = yes, Preview = yes (lfo)

### Reverb
- Reverb (id 90): Spec UI = yes, Preview = yes (tail)
- EarlyReflections (id 91): Spec UI = yes, Preview = yes (taps)
- PlateLite (id 92): Spec UI = yes, Preview = yes (tail)
- GatedReverb (id 93): Spec UI = yes, Preview = yes (tail)

### Metering
- Not started

## Preview candidates (future)
- AutoTrim (id 7): would benefit from gain adjustment history (implemented).
