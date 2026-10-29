# Spectrum-driven lighting ideas

## Current inputs

- `CAudioAnalyzer` produces 14 musically selected RMS bands for stable low, mid, and high-frequency response.
- It also runs an FFT and reduces its non-negative frequency bins to 76 magnitude groups, one per terminal graph column.
- The 76 values are normalized relative to the strongest group in each FFT frame. They express spectral shape, not absolute loudness.
- Existing spectral metrics include range energy, flux, broadband coverage, high-to-mid ratio, air ratio, transient strength,
  percussive ratio, envelopes, and treble score.

## Proposed lighting uses

- Keep RMS bands as the stable base for brightness, bass response, and broad low/mid/high color balance.
- The Streamer's `BandSpectrum` processor exposes the 14 normalized RMS bands as a raw RGB strip frame. It assigns low-to-high bands a
  fixed red-to-violet palette and expands their contiguous zones to the requested LED count.
- Use spectral flux and transient strength to trigger short movement, flashes, or bursts.
- Require broadband coverage and/or percussive ratio before strong white or strobe-style flashes.
- Use high-to-mid and air ratios to increase cool white, sparkle, or fine-detail effects during treble-rich passages.
- Use energy or flux in selectable frequency ranges to target effects such as cymbal, vocal-presence, or bass accents.
- Optionally map aggregated FFT ranges to LED zones or colors for a frequency-shaped visualizer effect.

## Higher-resolution spectrum option

- At a typical 48 kHz sample rate, the analyzer uses a 1024-sample FFT, with 513 unique bins from DC through Nyquist.
- A 512-point display can show one bar per non-DC bin, with about 46.875 Hz per bin at 48 kHz.
- Expose pre-grouped per-bin magnitudes if this display is needed; the existing 76 groups cannot be expanded back to 512 bins.
- Use grouped or smoothed frequency ranges for lighting decisions rather than reacting to individual bins.

## Design principle

The display resolution and the lighting logic are independent. A 512-point spectrum is valuable for diagnostics or visualization;
derived metrics and intentionally aggregated ranges are the preferred controls for robust lighting behavior.
