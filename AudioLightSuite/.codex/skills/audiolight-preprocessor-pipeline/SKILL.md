---
name: audiolight-preprocessor-pipeline
description: "Analyze and modify the AudioLightSuite offline preprocessing pipeline: command-line parsing, Media Foundation decoding, shared audio analysis, and `.alcfeat` feature file generation. Use when Codex is changing files under src\\Preprocessor or shared analysis code that affects offline extraction, feature semantics, or feature file compatibility."
---

# AudioLight Preprocessor Pipeline

## Workflow

1. Read [pipeline-map.md](references/pipeline-map.md) before editing.
   Determine whether the task is about CLI handling, decode behavior, shared analysis semantics, beat or section derivation, or `.alcfeat` file format changes.
2. Preserve the tool lifecycle.
   Keep the `wmain` flow intact: parse CLI, initialize Winsock and COM, start Media Foundation, run `ExtractFeatures()`, then shut Media Foundation and COM down in the reverse order.
3. Preserve file-format compatibility deliberately.
   Treat `.alcfeat` output as a versioned binary contract. If header fields, counts, or payload layouts change, update the format version and review every writer-side assumption together.
4. Keep shared analyzer semantics intentional.
   `FeatureExtractor.cpp` depends on `..\Common\Audio\AudioAnalysis.*`. If band definitions, normalization behavior, or grouped energies change, decide whether the live streamer must change with it and use `$audiolight-streamer-runtime` when needed.
5. Preserve console-style error reporting.
   Keep HRESULT-based messages on `stderr` and success output on `stdout`.

## References

- Read [pipeline-map.md](references/pipeline-map.md) before substantial preprocessor work.
