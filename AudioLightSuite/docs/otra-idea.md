# Task: Design an Autonomous Real-Time Musical Structure Analysis System

## Context

We are developing a real-time, audio-reactive lighting system intended to work with electronic music, including live DJ sets, remixes, transitions, and improvised performances.

The ultimate goal is to create a **fully autonomous lighting system capable of interpreting musical structure and producing coherent, musically appropriate lighting effects without human intervention**.

Perfect musical interpretation is not required. However, the system should prioritize musical coherence, stability, responsiveness, and visually meaningful decisions over reacting to every detected sound.

The existing application already includes real-time audio processing and some spectral analysis and instrument/event detection capabilities.

Audio is typically captured at **48 kHz and delivered in blocks of 480 frames (10 ms)**.

The system ultimately controls lighting devices, including DMX fixtures and ESP32-based LED controllers.

We want to evolve the current audio processing architecture into a higher-level musical interpretation system.

## Primary Objective

Analyze the existing codebase and propose a detailed, incremental implementation plan for a new **Musical Structure Analyzer**.

This component should interpret the evolving musical context using audio features, temporal history, rhythmic patterns, statistical analysis, and potentially predictive techniques.

It must remain independent of the actual lighting hardware and lighting effect generation.

**Important: Do not implement anything yet. First inspect the existing architecture and produce an actionable technical plan.**

## 1. Architectural Separation

The proposed architecture should clearly separate three responsibilities.

### Layer 1 — Audio Perception

Extract low-level audio features and events, potentially reusing existing implementations:

- RMS and energy across frequency bands.
- Spectral flux and onset detection.
- Kick, snare, hi-hat, and cymbal activity.
- Bass and high-frequency activity.
- Spectral centroid and spectral distribution.
- Transient density.
- Changes in instrument activity.

Inspect what is already implemented before proposing additional algorithms.

Avoid redundant feature extraction or unnecessary computational overhead.

### Layer 2 — Musical Structure Interpretation

Introduce a higher-level component responsible for interpreting musical behavior over time.

It should estimate:

- BPM and beat confidence.
- Beat phase and rhythmic synchronization.
- Groove stability.
- Musical energy and its evolution.
- Rhythmic density and changes.
- Repetitive patterns.
- Musical novelty and structural changes.
- Build-up probability.
- Drop probability.
- Breakdown probability.
- Section transitions.
- Musical phrase boundaries, where reasonably inferable.

Musical states should not necessarily be mutually exclusive.

For example, a stable groove may coexist with an emerging build-up.

Prefer continuous metrics and confidence scores over rigid classifications whenever appropriate.

### Layer 3 — Autonomous Lighting Director

This layer is outside the immediate implementation scope, but the Musical Structure Analyzer should expose information that a future autonomous lighting director can consume.

The director will eventually handle:

- Scene selection.
- Musical phrase-aligned transitions.
- Beat-synchronized effects.
- Color palette management.
- Lighting intensity and movement.
- Effect variation and repetition avoidance.
- Visual continuity and artistic coherence.

Do not tightly couple musical analysis to specific lighting hardware, DMX channels, or effect implementations.

## 2. Multi-Scale Temporal Analysis

A key requirement is the ability to interpret musical events across multiple time scales.

Investigate an architecture supporting approximately:

| Time scale | Purpose |
|---|---|
| 10–50 ms | Transient and onset detection |
| 100–500 ms | Local energy and spectral evolution |
| 1–4 seconds | Rhythm, beat tracking, short-term patterns |
| 4–16 seconds | Repetition, instrumentation changes, emerging trends |
| 16–64 seconds | Musical sections, structural evolution, longer-term context |

These ranges are initial guidelines, not fixed requirements.

Evaluate efficient implementations using rolling statistics, ring buffers, feature aggregation, and multiresolution analysis.

Avoid storing raw audio unnecessarily.

The analyzer should maintain enough history to recognize musical evolution without introducing excessive memory usage or processing latency.

## 3. Beat Tracking and Musical Timing

Investigate how to estimate and maintain a stable musical clock from the incoming audio.

Consider:

- BPM estimation.
- Beat phase tracking.
- Confidence estimation.
- Tempo changes.
- Half-time and double-time ambiguity.
- Missing or ambiguous beats.
- Beat prediction.
- Synchronization corrections.
- Musical meter and bar boundaries, when detectable.

A major goal is to move beyond purely reactive behavior.

Once a reliable beat clock is established, downstream systems should be able to schedule events aligned with upcoming beats rather than waiting for detected transients.

Assess whether a phase-locked loop (PLL), probabilistic beat tracker, or another approach is appropriate.

Consider the differences between beat tracking and higher-level downbeat or phrase tracking.

## 4. Musical Structure Recognition

Investigate techniques for recognizing the following musical situations.

### Stable Groove

Identify sustained rhythmic patterns, relatively stable energy, and recurring instrument activity.

### Build-up

Detect sustained trends that may indicate increasing musical tension, such as:

- Increasing energy.
- Increasing onset density.
- Faster rhythmic subdivisions.
- Increasing high-frequency activity.
- Repeated or accelerating percussion patterns.
- Changes in bass activity.
- Increasing spectral complexity.

No single feature should be considered definitive.

### Drop

Identify significant musical transitions, potentially involving:

- Sudden energy increases.
- Reintroduction of kick or bass.
- Abrupt spectral changes.
- Changes following a build-up or breakdown.
- Strong rhythmic or structural novelty.

Distinguish between a genuine musical drop and an isolated loud transient.

### Breakdown

Identify sustained reductions in rhythmic density, percussion activity, or energy, including situations where melodic or atmospheric content remains prominent.

### Transitions

Detect changes in instrumentation, rhythm, spectral content, or overall musical character.

The system must accommodate DJ mixing, overlapping tracks, and gradual transitions.

## 5. Musical Novelty and Change-Point Detection

Investigate whether novelty detection can provide a useful foundation for recognizing musical structure.

Consider techniques such as:

- Feature-vector similarity.
- Spectral novelty.
- Online change-point detection.
- Repetition analysis.
- Self-similarity approaches adapted for streaming audio.
- Statistical comparisons between recent and historical feature distributions.

Traditional offline structural analysis techniques may rely on future audio.

We need causal, real-time-compatible alternatives.

Explain the tradeoffs between detection accuracy, memory consumption, and latency.

## 6. Probabilistic Interpretation and Temporal Stability

The system should avoid rapidly switching musical states because of isolated events or noisy detections.

Evaluate:

- Confidence-weighted feature fusion.
- Temporal smoothing.
- Hysteresis.
- Minimum state durations.
- State-transition probabilities.
- Evidence accumulation.
- Adaptive thresholds.
- Detection cooldowns.
- Context-aware interpretation.

Consider whether a hierarchical state model, hidden Markov model, or simpler probabilistic state machine would be appropriate.

Prefer interpretable, deterministic or statistical techniques initially.

Machine learning should only be introduced if it offers a clear, measurable advantage.

## 7. Anticipation and Prediction

An important long-term objective is to anticipate musical events rather than merely detect them after they occur.

Investigate the feasibility of predicting:

- Upcoming beats.
- Bar boundaries.
- Phrase boundaries.
- Possible drops following build-ups.
- Likely transitions based on repeating musical structures.

Clearly distinguish between:

1. Events that can be predicted with relatively high confidence, such as the next beat under stable tempo.
2. Events that can only be estimated probabilistically, such as a future drop.
3. Events that cannot be reliably anticipated from causal audio analysis alone.

The system should gracefully handle incorrect predictions.

For example, it may prepare a lighting transition during a suspected build-up but should avoid committing to a dramatic effect until sufficient evidence exists.

Consider a configurable confidence threshold for predictive actions.

## 8. Real-Time Performance Requirements

The current processing pipeline typically receives 480 audio frames every 10 ms at 48 kHz.

The design must preserve real-time performance.

Consider:

- Separating high-frequency feature extraction from slower structural analysis.
- Running expensive calculations at reduced update rates.
- Avoiding unnecessary allocations in time-sensitive processing paths.
- Using bounded history buffers.
- Maintaining predictable computational costs.
- Avoiding blocking operations.
- Supporting continuous playback for hours.
- Handling audio discontinuities and stream resets.
- Handling temporary silence and abrupt track changes.

Not every musical metric needs to update every 10 ms.

Propose appropriate update frequencies for each processing stage.

Also distinguish analysis latency from event scheduling accuracy.

## 9. Proposed Output Interface

Design a hardware-independent output interface that describes the current musical context.

For example:

```json
{
  "bpm": 128.0,
  "beat_phase": 0.72,
  "beat_confidence": 0.94,
  "energy": 0.81,
  "groove_stability": 0.89,
  "build_up_probability": 0.76,
  "drop_probability": 0.12,
  "breakdown_probability": 0.04,
  "novelty": 0.35,
  "section": "BUILD_UP",
  "section_confidence": 0.78
}
```

This is only a conceptual example.

Evaluate whether a combination of continuous state snapshots and discrete timestamped events would be more appropriate.

Consider exposing:

- Current musical context.
- Detected structural transitions.
- Beat and bar timing.
- Confidence scores.
- Predicted events and their estimated timing.
- Timestamp information.
- Analyzer readiness and synchronization state.

Avoid unnecessary serialization within the internal processing pipeline.

## 10. Validation and Observability

The proposed architecture must be testable independently of lighting hardware.

Investigate ways to:

- Record and replay audio features.
- Visualize musical metrics over time.
- Inspect state transitions.
- Compare analyzer output against manually annotated music.
- Identify false-positive drops and build-ups.
- Evaluate beat tracking stability.
- Measure detection latency.
- Measure computational overhead.
- Test different electronic music genres.
- Test DJ transitions and overlapping tracks.

Propose objective metrics wherever possible.

Include test scenarios involving:

- Stable four-on-the-floor rhythms.
- Breakdowns without complete silence.
- Build-ups without subsequent drops.
- Sudden drops without preceding build-ups.
- Half-time and double-time rhythms.
- Tracks with changing BPM.
- Gradual DJ crossfades.
- Unexpected transitions.
- Music containing strong vocals or misleading transients.

## 11. Implementation Strategy

We do not want to build everything simultaneously.

Propose a phased implementation roadmap that delivers independently testable functionality.

A possible progression is:

**Phase 1 — Architecture and existing feature inventory**

Inspect the current codebase, identify reusable components, and define interfaces.

**Phase 2 — Temporal feature aggregation**

Introduce multi-scale history and statistical trend analysis.

**Phase 3 — Beat tracking and musical clock**

Implement stable BPM estimation, beat phase tracking, and beat prediction.

**Phase 4 — Musical novelty and structural change detection**

Recognize meaningful changes in musical behavior.

**Phase 5 — Musical state interpretation**

Estimate groove, build-up, drop, breakdown, and transition probabilities.

**Phase 6 — Predictive interpretation**

Introduce conservative anticipation of upcoming musical events.

**Phase 7 — Lighting director integration contract**

Define how a future autonomous lighting director should consume the analysis results.

You may reorganize these phases if the existing codebase suggests a better implementation sequence.

Each phase should include:

- Objectives.
- Dependencies.
- Required architectural changes.
- Proposed algorithms.
- Relevant existing components.
- New components or interfaces.
- Testing strategy.
- Acceptance criteria.
- Performance considerations.
- Expected limitations.

## 12. Important Design Principles

Follow these principles throughout the proposal:

1. **Autonomy:** The system must operate without a human lighting operator, preprocessed tracks, DJ software integration, or external timecode.
2. **Causal processing:** Assume only current and historical audio is available.
3. **Musical coherence:** Prioritize stable, meaningful interpretation over reacting to every transient.
4. **Graceful uncertainty:** Low-confidence interpretations should not trigger aggressive decisions.
5. **Modularity:** Separate audio perception, musical interpretation, and lighting control.
6. **Performance:** Preserve predictable real-time processing.
7. **Reusability:** Prefer existing codebase functionality where appropriate.
8. **Observability:** Every important interpretation should be measurable and debuggable.
9. **Incremental development:** Each phase should produce testable results.
10. **Extensibility:** Allow future integration of machine learning or external musical timing sources without requiring major architectural changes.

## Expected Deliverable

First, inspect the existing repository and identify the current audio-processing architecture, relevant components, existing algorithms, and reusable interfaces.

Then produce a comprehensive technical implementation plan.

The plan should include:

- An assessment of the existing architecture.
- A proposed component architecture.
- Recommended algorithms and justification.
- Data flow and timing considerations.
- Proposed interfaces and data structures.
- Multi-scale history management.
- Computational and latency considerations.
- A phased implementation roadmap.
- Testing and validation strategies.
- Risks, uncertainties, and alternative approaches.
- Clear acceptance criteria for each phase.

Explicitly distinguish between functionality already implemented, functionality that can be reused or extended, and functionality that must be developed from scratch.

Highlight any architectural decisions that should be resolved before implementation.

**Do not modify source files or implement the solution yet. The immediate objective is to produce a technically sound, actionable plan based on the actual existing codebase, not a generic architecture proposal.**
