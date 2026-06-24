# Pipeline Map

## Entry Flow

- `src\Preprocessor\AudioLightPreprocessor.cpp`: CLI bootstrap, Winsock and COM init, `MFStartup`, `ExtractFeatures()`, `MFShutdown`.
- `src\Preprocessor\CommandLine.*`: `--input` and `--output`.
- `src\Preprocessor\FeatureExtractor.cpp`: decode to mono PCM, analyze at a fixed cadence, derive beats and sections, and write the binary feature file.
- `src\Common\Audio\AudioAnalysis.*`: shared band definitions and raw analysis used by both offline and live code paths.

## Current Output Contract

- The default output path replaces the input extension with `.alcfeat`.
- The file header uses magic `ALCFEAT\0`.
- The current format version is `2`.
- Analysis cadence is `100` ms per frame.
- Stored payload blocks are bands, frames, beats, and sections.

## Edit Checklist

- Keep Media Foundation decode Windows-native and dependency-light unless the task explicitly requires a different backend.
- Preserve the downmix-to-mono step unless downstream consumers are redesigned together.
- If band count or band definitions change, review `FEATURE_MAX_BANDS`, header serialization, frame serialization, and all consumers of `CAudioAnalyzer::BAND_COUNT`.
- If normalization or novelty logic changes, review beat and section detection because both consume normalized frame data.
- If the binary layout changes, update the file format version and keep header counts consistent with the payload vectors.
- If CLI behavior changes, preserve explicit HRESULT-based diagnostics on `stderr`.

## Validation

- Prefer a project build after code changes.
- If a sample media file is available, run the tool with explicit `--input` and optional `--output` and verify the output file is written.
- For format changes, inspect the written file size and verify header counts match the serialized vectors.
