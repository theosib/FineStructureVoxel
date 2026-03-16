Avoid names or any other implementation detail that resembles Minecraft/Mojang intellectual property.

Non-hot-path systems (events, configs, UI, commands) should use flexible serializable data (finescript Values, DataContainer, CBOR) rather than rigid C++ structs — this enables scriptability, multiplayer serialization, and mod extensibility. Hot paths (mesh generation, physics, light propagation) remain optimized C++.

Keep code and docs in sync.

See [docs/INDEX.md](docs/INDEX.md) for project documentation.
