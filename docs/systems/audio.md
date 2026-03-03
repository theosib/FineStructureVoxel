# System: Audio

**Library:** `finevox_audio` (`finevox::audio::`)
**Option:** `FINEVOX_BUILD_AUDIO` (optional; audio module may be absent)
**Headers:** `include/finevox/audio/`
**Core sound types** (in `finevox` core): `SoundSetId`, `SoundEvent`, `SoundEventQueue`, `SoundRegistry`
**Old docs:** [old_docs/AI-NOTES.md](../../old_docs/AI-NOTES.md) — Phase 16 section

---

## Overview

The audio system uses **miniaudio** as the backend. Core sound types (events, registry, IDs) live in `finevox` core so game code can trigger sounds without depending on the audio module. The `finevox_audio` library handles actual playback. `FootstepTracker` handles step sounds based on player movement. Sounds are fire-and-forget with thread-safe cleanup.

---

## Key Types

### In `finevox` core (always available)

| Type | Description |
|------|-------------|
| `SoundSetId` | Interned ID for a set of sound variants |
| `SoundEvent` | Sound trigger with SetId, position, volume/pitch variance, category |
| `SoundEventQueue` | Thread-safe queue; game thread pushes, audio thread consumes |
| `SoundRegistry` | Maps block types / actions to SoundSetIds; loaded from `.sound` files |
| `SoundAction` enum | `Place`, `Break`, `Step`, `Interact`, `Ambient`, `Splash`, `Swim` |

### In `finevox_audio`

| Type | Description |
|------|-------------|
| `AudioEngine` | Owns miniaudio context; manages active sounds; main update loop |
| `SoundLoader` | Loads audio files (OGG/WAV/MP3) via miniaudio; caches decoded data |
| `FootstepTracker` | Tracks player position; triggers step sounds at configurable stride |

---

## Key APIs

```cpp
// SoundRegistry — registered sounds
SoundRegistry& reg = SoundRegistry::global();
reg.loadDirectory("resources/sounds/");  // loads all .sound files
SoundSetId id = reg.getActionSound(blockTypeId, SoundAction::Break);

// Triggering sounds (game thread)
SoundEvent event;
event.soundSetId = id;
event.position = blockPos;
event.volume = 1.0f;
event.pitchVariance = 0.1f;  // ±10% pitch randomization
soundQueue.push(event);

// AudioEngine setup
AudioEngine engine;
engine.init();
engine.setSoundQueue(&soundQueue);

// Per-frame audio update (main thread)
engine.update();  // drains finished sounds from cleanup queue

// FootstepTracker
FootstepTracker tracker;
tracker.update(playerPos, isOnGround, blockBelowType);  // triggers step sounds
tracker.setStepDistance(0.5f);  // blocks per step sound
```

---

## Sound File Format (`.sound`)

```
# resources/sounds/stone.sound
name: finevox:stone
place: audio/block/stone/place1.ogg audio/block/stone/place2.ogg
break: audio/block/stone/break1.ogg audio/block/stone/break2.ogg
step:  audio/block/stone/step1.ogg  audio/block/stone/step2.ogg  audio/block/stone/step3.ogg
```

Multiple files per action = random selection on each trigger.

---

## Sound Events (Phase 21 additions)

```cpp
// Splash/swim sounds for fluid entry
SoundAction::Splash  // entity enters fluid (dry→wet transition)
SoundAction::Swim    // entity moving through fluid
```

`EntityManager::physicsPass()` detects dry→wet transitions and pushes `SoundAction::Splash` events.

---

## Gotchas

**CRITICAL:** Never call `ma_sound_uninit()` from the audio completion callback (miniaudio audio thread). This causes a **deadlock**.

```cpp
// CORRECT pattern — queue for main thread cleanup
void onSoundComplete(ma_sound* sound) {
    finishedSoundQueue_.push(sound);  // thread-safe
}
void AudioEngine::update() {
    ma_sound* s;
    while (finishedSoundQueue_.try_pop(s)) {
        ma_sound_uninit(s);  // safe — main thread
        delete s;
    }
}
```

- `#define MINIAUDIO_IMPLEMENTATION` in exactly ONE file: `src/audio/audio_engine.cpp`
- `FINEVOX_BUILD_AUDIO` CMake option — if disabled, `finevox_audio` is not built; game code using core `SoundEvent` still compiles
- 3D spatialization: `SoundEvent.position` must be in world space; `AudioEngine` handles listener transform
- Volume categories: master, music, effects, ambient (all configurable at runtime)
- Supported formats: OGG Vorbis, WAV, MP3 (via miniaudio built-ins)

---

## Resource Files

```
resources/sounds/
├── stone.sound
├── dirt.sound
├── wood.sound
├── water.sound
└── ... (one .sound file per block type or sound set)
```
