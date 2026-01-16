# 12. Scripting and Command Language

[Back to Index](INDEX.md) | [Previous: Persistence and Serialization](11-persistence.md)

---

## 12.1 Overview

The engine supports two levels of scripting, both implemented as an **external project** that integrates via well-defined hooks:

1. **Game Logic Scripting** - For block interactions, programmatic meshes, entity AI
2. **Command Language** - For in-game commands (like Minecraft's `/` commands)

Both use the same underlying interpreter, with the command language being a simplified surface syntax.

---

## 12.2 Integration Architecture

The scripting system is a separate project that we pull in as a dependency. The voxel engine provides hooks for the interpreter to call into:

```cpp
namespace finevox {

// Interface that the scripting system implements
class ScriptInterpreter {
public:
    virtual ~ScriptInterpreter() = default;

    // Execute a command string (command language syntax)
    virtual ScriptResult executeCommand(std::string_view command) = 0;

    // Execute a script (full language syntax)
    virtual ScriptResult executeScript(std::string_view script) = 0;

    // Compile a script for repeated execution
    virtual std::unique_ptr<CompiledScript> compile(std::string_view script) = 0;
};

struct ScriptResult {
    bool success;
    std::string error;
    std::variant<std::monostate, int64_t, double, std::string, BlockPos> value;
};

// Context provided to scripts for accessing game state
class ScriptContext {
public:
    explicit ScriptContext(World& world, Entity* executor = nullptr);

    // Block operations
    Block getBlock(BlockPos pos) const;
    void setBlock(BlockPos pos, uint16_t typeId, uint8_t rotation = 0);

    // Position queries (return values for script expressions)
    BlockPos getExecutorPos() const;
    BlockPos getTargetPos() const;  // Block being looked at

    // Entity access
    Entity* getExecutor() const { return executor_; }

    // Batch operations (see Batch Operations)
    BatchBuilder& batch();

private:
    World& world_;
    Entity* executor_;
    std::unique_ptr<BatchBuilder> batchBuilder_;
};

}  // namespace finevox
```

---

## 12.3 Command Language Syntax

### Design Goals

The command language should be:
- **Intuitive** - Simple commands look like `verb arg1 arg2`
- **Expressive** - Arguments can be computed expressions
- **Composable** - Complex commands built from simple parts

### Syntax Elements

| Syntax | Meaning | Example |
|--------|---------|---------|
| `word` | Literal value or context variable | `stone`, `X`, `PlayerY` |
| `(expr)` | Math/grouping expression | `(Y + 5)`, `(X * 2 - 1)` |
| `{verb args...}` | Embedded function call | `{getHeight X Z}` |
| `[index]` | Array/list indexing | `items[0]`, `neighbors[i]` |

### Delimiter Summary

- **Bare words** - Variables from context, or literal values
- **Parentheses `()`** - Math expressions and grouping
- **Braces `{}`** - Function calls (verb-first inside)
- **Brackets `[]`** - Array/list indexing (reserved for future use)

### Examples

```
# Simple command
setblock 10 64 20 stone

# With context variables (X, Y, Z preset for executor position)
setblock X Y Z stone

# With math expression (parentheses for grouping)
setblock X (Y + 5) Z stone

# With function call (braces for verb-first call)
setblock X {getHeight X Z} Z stone

# Nested function calls
setblock X {max Y {getHeight X Z}} Z stone

# Function call result in math expression
setblock X ({getHeight X Z} + 5) Z stone

# Conditional
if {isAir X (Y - 1) Z} then setblock X Y Z cobblestone

# Loop (fills a region)
for x in X..(X + 10) for z in Z..(Z + 10) setblock x Y z grass

# Array indexing (future)
give player items[0] 1
```

### Parsing Rules

1. **Top-level**: First token is verb, rest are arguments
2. **Bare word**: If known variable, use value; else treat as literal string
3. **`(expr)`**: Parse as infix math expression
4. **`{verb args...}`**: Recursive command parse, return result as value
5. **`[index]`**: Index into previous value (array/map access)

---

## 12.4 Script Hooks in Engine

The engine provides registration points for scripts:

```cpp
namespace finevox {

class ScriptRegistry {
public:
    // Register a script to run on block events
    void registerBlockScript(
        uint16_t blockTypeId,
        BlockEvent event,
        std::unique_ptr<CompiledScript> script
    );

    // Register a script to run on entity tick
    void registerEntityScript(
        std::string_view entityType,
        std::unique_ptr<CompiledScript> script
    );

    // Register a custom command
    void registerCommand(
        std::string_view name,
        std::unique_ptr<CompiledScript> handler
    );

private:
    std::unordered_map<uint16_t, std::map<BlockEvent, CompiledScriptPtr>> blockScripts_;
    std::unordered_map<std::string, CompiledScriptPtr> entityScripts_;
    std::unordered_map<std::string, CompiledScriptPtr> commands_;
};

enum class BlockEvent {
    Place,
    Break,
    Interact,
    NeighborChange,
    Tick
};

}  // namespace finevox
```

---

[Next: Batch Operations API](13-batch-operations.md)
