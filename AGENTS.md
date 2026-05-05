# Agent Memory: JUCE Plugin Development & CI/CD Guidelines

This file serves as a memory for the AI Agent to avoid repeating past mistakes during the development of NovaMB and other JUCE-based plugins.

## 1. GitHub Actions & CMake (Windows Runners)

### The Error
`cmake -B build -G "Visual Studio 17 2022" -A x64` fails because the runner has a newer version of Visual Studio (e.g., VS 18 / 2025) and the specific generator "Visual Studio 17 2022" is not found.

### The Solution
Do **NOT** specify the generator version (`-G`). Let CMake auto-detect the installed Visual Studio while keeping the architecture flag:
```yaml
# Correct command
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release
```

---

## 2. Audio Processing (DSP) & Stability

### Distortion and Clicks
**Mistake**: Applying `block.multiplyBy(gain)` or direct sample multiplication in a single block update.
**Solution**: Always use `juce::LinearSmoothedValue<float>` for any gain changes (Gain Reduction, Makeup, etc.) to smoothly ramp the volume and avoid "zipper noise" or clicks.

### Sidechain Safety
**Mistake**: Assuming the sidechain buffer has the same number of channels or samples as the main buffer.
**Solution**: Always check `sidechainBuffer.getNumChannels() > 0` and use `juce::jmin` to clamp the number of channels processed. Clear the internal sidechain buffers (`scBuffer.clear()`) before copying.

### Real-time Safety (No Allocations)
**Mistake**: Creating `juce::AudioBuffer` or `std::vector` inside `processBlock`.
**Solution**: Pre-allocate all buffers in `prepareToPlay` (or a `prepare` method called from it). Ensure `scBuffer.setSize` is called before use.

---

## 3. JUCE Framework Peculiarities

### AudioBlock & Channel Subsets
**Mistake**: Using `AudioBlock::getSubsetChannels` if the JUCE version or custom wrapper doesn't support it consistently.
**Solution**: Create a manual `AudioBlock` by passing the write pointers and the specific channel count/samples:
```cpp
juce::dsp::AudioBlock<float> subBlock(buffer.getArrayOfWritePointers(), (juce::uint32)channelsToProcess, (size_t)numSamples);
```

### UI Interaction (Buttons)
**Mistake**: Mute/Solo buttons not staying pressed.
**Solution**: Ensure `setToggleable(true)` AND `setClickingTogglesState(true)` are called during button initialization.

---

## 4. Design & Visualization

- **Z-Order**: Place text labels and UI background elements far enough from interactive knobs/sliders to avoid overlap or "hidden" labels.
- **Spectrum Analyzer**: When using Sidechain, do not overlap multiple spectra unless specifically requested, as it clutters the "Precision Spectral Analyzer" view. Keep it clean with just the main signal and the Dynamic Gain Reduction curve.
