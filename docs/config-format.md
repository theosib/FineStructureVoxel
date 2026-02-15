# Config File Format Reference

This document describes the line-oriented configuration format used by FineStructureVoxel and the C++ parser that reads it. The format is YAML-inspired but simpler — no nested objects, no arrays, no quoting rules. It is designed for declarative resource files (block models, geometry, collision shapes, biome definitions, etc.).

## Format Specification

### Basic Syntax

```
# This is a comment
key: value
key:suffix: value
key:suffix:
    1.0 2.0 3.0
    4.0 5.0 6.0
include: other_file
```

**Rules:**

1. **Comments** — Lines beginning with `#` are ignored.
2. **Blank lines** — Ignored during parsing, preserved by `ConfigFile` on write-back.
3. **Key-value directives** — A non-indented line containing a colon. The key is everything before the first colon; the value is everything after the last colon, trimmed of whitespace.
4. **Key-suffix-value directives** — If there are two colons, the text between them is a suffix (also trimmed). For example, `face:top:` has key `face`, suffix `top`, and an empty value.
5. **Data lines** — Lines beginning with whitespace (space or tab). Parsed as whitespace-separated floats and attached to the preceding directive as `dataLines`. Non-numeric tokens on data lines are silently skipped.
6. **Include directives** — `include: path` causes the referenced file to be parsed and its entries merged into the current document. Entries from the included file appear before any subsequent entries in the including file, giving the includer override semantics (later entries with the same key win on lookup).

### Directive Forms

| Form | Key | Suffix | Value |
|------|-----|--------|-------|
| `texture: blocks/stone` | `texture` | *(empty)* | `blocks/stone` |
| `face:top:` | `face` | `top` | *(empty)* |
| `face:bottom: alt_tex` | `face` | `bottom` | `alt_tex` |
| `hardness: 1.5` | `hardness` | *(empty)* | `1.5` |

### Data Blocks

A directive can be followed by indented lines of numbers:

```
face:top:
    0 1 0    0 0
    1 1 0    1 0
    1 1 1    1 1
    0 1 1    0 1
```

Each indented line is parsed into a `vector<float>`. The directive's `dataLines` is a `vector<vector<float>>`. In this example there are 4 data lines, each with 5 floats (3 position + 2 UV).

The data block ends at the next non-indented line (or EOF).

### Value Types

Values are stored as strings. Accessor methods parse on demand:

| Accessor | Recognized values |
|----------|-------------------|
| `asBool()` | `true`, `yes`, `1`, `on`, `t`, `y` → true; `false`, `no`, `0`, `off`, `f`, `n` → false |
| `asFloat()` | Standard `strtof` parsing. If the value has a number list (from data lines), returns the first number. |
| `asInt()` | Standard `strtol` (base 10) parsing. |
| `asString()` | Returns the raw trimmed string. |

### Include Resolution

When the parser encounters `include: path`, it calls the user-provided `IncludeResolver` callback:

```cpp
using IncludeResolver = std::function<std::string(const std::string&)>;
```

The callback receives the raw path string from the directive (e.g., `base/solid_cube`) and must return an absolute filesystem path to the file. If no resolver is set, the path is treated as relative to the including file's directory.

**Important:** The resolver receives the path exactly as written in the file, without any file extension. If your files have extensions like `.model`, the resolver must append them.

### Override Semantics

When multiple entries share the same key, `ConfigDocument::get(key)` returns the **last** one (reverse iteration). This means:

1. A file can `include: base` to get default values.
2. Any key re-declared after the include overrides the base value.
3. `getAll(key)` returns all entries with that key, in order.

---

## C++ API

### Classes

There are two independent parsing systems:

| Class | Purpose | Use case |
|-------|---------|----------|
| `ConfigParser` → `ConfigDocument` | Read-only parsing of structured config with suffixes and data blocks | Resource files (models, geometry, biomes) |
| `ConfigFile` | Read-write config with comment/structure preservation | Settings files, save data |

### ConfigParser + ConfigDocument (Read-Only)

```cpp
#include "finevox/core/config_parser.hpp"

// Create parser and set include resolution
ConfigParser parser;
parser.setIncludeResolver([](const std::string& path) -> std::string {
    return "/absolute/path/to/" + path + ".model";
});

// Parse from file
auto doc = parser.parseFile("/path/to/block.model");
if (!doc) { /* file not found */ }

// Parse from string
auto doc2 = parser.parseString("texture: stone\nhardness: 1.5\n");
```

**ConfigDocument API:**

```cpp
// Simple key lookup (returns last entry with this key)
const ConfigEntry* entry = doc->get("texture");
const ConfigEntry* entry = doc->get("face", "top");  // key + suffix

// Convenience value access (with defaults)
std::string_view tex = doc->getString("texture", "default");
float hardness      = doc->getFloat("hardness", 1.0f);
int depth           = doc->getInt("filler_depth", 3);
bool translucent    = doc->getBool("translucent", false);

// Get all entries with a given key (e.g., all "face" entries)
for (auto* entry : doc->getAll("face")) {
    std::string faceName = entry->suffix;  // "top", "bottom", etc.

    // Access data lines (indented float arrays)
    for (const auto& line : entry->dataLines) {
        float x = line[0], y = line[1], z = line[2];
        float u = line[3], v = line[4];
    }
}

// Iterate all entries in order
for (const auto& entry : doc->entries()) {
    // entry.key, entry.suffix, entry.value, entry.dataLines
}
```

**ConfigEntry structure:**

```cpp
struct ConfigEntry {
    std::string key;                          // "face", "texture", etc.
    std::string suffix;                       // "top", "bottom", or empty
    ConfigValue value;                        // The value after the colon(s)
    std::vector<std::vector<float>> dataLines; // Indented number lines

    bool hasSuffix() const;
    bool hasData() const;
};
```

**ConfigValue:**

```cpp
class ConfigValue {
    std::string_view asString() const;
    std::string asStringOwned() const;
    bool asBool(bool defaultVal = false) const;
    float asFloat(float defaultVal = 0.0f) const;
    int asInt(int defaultVal = 0) const;
    const std::vector<float>& asNumbers() const;
    bool hasNumbers() const;
    bool empty() const;
};
```

### ConfigFile (Read-Write)

For config files that need to be modified and saved back while preserving comments and formatting:

```cpp
#include "finevox/core/config_file.hpp"

ConfigFile config;
if (config.load("/path/to/settings.conf")) {
    // Read values
    std::string name = config.getString("name", "default");
    int64_t count    = config.getInt("count", 0);
    double scale     = config.getFloat("scale", 1.0);
    bool enabled     = config.getBool("enabled", true);

    // Write values (modifies the line in-place)
    config.set("name", "new_value");
    config.set("count", int64_t(42));
    config.set("new_key", "added at end");

    // Remove (comments out the line with #)
    config.remove("obsolete_key");

    // Save back (preserves comments, blank lines, ordering)
    config.save();
}
```

`ConfigFile` does **not** support suffixes or data blocks — it is for simple key-value settings only. For structured resource files, use `ConfigParser`.

---

## File Format Examples

### Block Model (`.model`)

```
# Stone block
include: base/solid_cube
texture: blocks/stone
sounds: stone
hardness: 1.5
```

The `include: base/solid_cube` directive pulls in defaults from `base/solid_cube.model`:

```
# Base model for solid cube blocks
collision: full
rotations: none
hardness: 1.0
```

After include resolution, the document effectively contains: `collision: full`, `rotations: none`, `hardness: 1.0`, then `texture: blocks/stone`, `sounds: stone`, `hardness: 1.5`. The stone file's `hardness: 1.5` overrides the base's `hardness: 1.0`.

### Geometry (`.geom`)

```
# Slab geometry (bottom half)

face:bottom:
    0 0 1    0 0
    1 0 1    1 0
    1 0 0    1 1
    0 0 0    0 1

face:slab_top:
    0 0.5 0  0 0
    1 0.5 0  1 0
    1 0.5 1  1 1
    0 0.5 1  0 1

face:slab_west:
    0 0   1    0 0
    0 0   0    1 0
    0 0.5 0    1 0.5
    0 0.5 1    0 0.5

solid-faces: bottom
```

Each `face:<name>:` directive has 4 data lines. Each data line has 5 floats: `x y z u v`. The `solid-faces:` directive lists which face names are solid (can occlude adjacent blocks).

Standard face names (`bottom`, `top`, `west`, `east`, `north`, `south`) map to cube face indices 0-5 and participate in face culling. Custom names (like `slab_top`, `slab_west`) get higher indices and are always rendered.

### Collision Shape (`.collision`)

```
# Stairs collision shape

box:
    0 0 0.5
    1 0.5 1

box:
    0 0 0
    1 1 0.5
```

Each `box:` directive has 2 data lines of 3 floats each: min corner `(x y z)` and max corner `(x y z)`.

### Biome Definition (`.biome`)

```
# Plains biome
name: Plains
temperature_min: 0.3
temperature_max: 0.7
humidity_min: 0.2
humidity_max: 0.6
base_height: 64.0
height_variation: 8.0
surface: grass
filler: dirt
filler_depth: 3
stone: stone
tree_density: 0.005
```

Simple key-value pairs only — no suffixes or data blocks.

### Feature Definition (`.feature`)

```
# Simple oak tree
type: tree
trunk: stone
leaves: grass
min_trunk_height: 4
max_trunk_height: 7
leaf_radius: 2
requires_soil: true
```

### Ore Definition (`.ore`)

```
# Iron ore vein
block: cobble
replace: stone
vein_size: 8
min_height: 0
max_height: 48
veins_per_chunk: 8
```

---

## Porting to Another Project

The config parser is self-contained in two files:

- `include/finevox/core/config_parser.hpp` — `ConfigParser`, `ConfigDocument`, `ConfigEntry`, `ConfigValue`
- `src/core/config_parser.cpp` — Implementation

For read-write settings files, also include:

- `include/finevox/core/config_file.hpp` — `ConfigFile`
- `src/core/config_file.cpp` — Implementation (depends on `DataContainer` for typed storage)

**Dependencies:**
- `ConfigParser` / `ConfigDocument`: Only `<string>`, `<string_view>`, `<vector>`, `<unordered_map>`, `<functional>`, `<optional>`, `<variant>`, `<fstream>`, `<sstream>`, `<cctype>`, `<cstdlib>`, `<algorithm>`. No external libraries.
- `ConfigFile`: Additionally depends on `<filesystem>` and `DataContainer` (a simple key-value store using `std::variant`).
- The `parseConfig()` convenience function depends on `ResourceLocator`, but you don't need it — just use `ConfigParser` directly.

To extract the parser for another project:

1. Copy `config_parser.hpp` and `config_parser.cpp`.
2. Change the namespace from `finevox` to your own.
3. Remove the `#include "finevox/core/resource_locator.hpp"` and the `parseConfig()` convenience function from the `.cpp` file (or rewrite it for your own resource resolution).
4. Set up an `IncludeResolver` appropriate for your project's directory structure.
