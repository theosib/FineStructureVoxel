# 10. Input and Player Control

[Back to Index](INDEX.md) | [Previous: Lighting System](09-lighting.md)

---

## 10.1 Input System

Uses FineStructureVK's input callbacks with a higher-level abstraction:

```cpp
namespace finevox {

class InputManager {
public:
    InputManager();

    // Update (call once per frame, before processing)
    void update();

    // Key state
    bool isKeyDown(int key) const;
    bool wasKeyPressed(int key) const;   // True only on first frame
    bool wasKeyReleased(int key) const;

    // Mouse state
    bool isMouseButtonDown(int button) const;
    bool wasMouseButtonPressed(int button) const;
    glm::vec2 mousePosition() const;
    glm::vec2 mouseDelta() const;
    float scrollDelta() const;

    // Mouse capture
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const;

    // Register with FineStructureVK window
    void bindToWindow(finevk::Window& window);

    // Action mapping (optional higher-level abstraction)
    void mapAction(const std::string& action, int key);
    bool isActionActive(const std::string& action) const;

private:
    std::unordered_set<int> keysDown_;
    std::unordered_set<int> keysPressed_;
    std::unordered_set<int> keysReleased_;

    glm::vec2 mousePos_{0};
    glm::vec2 lastMousePos_{0};
    float scrollAccum_ = 0;

    bool mouseCaptured_ = false;

    std::unordered_map<std::string, int> actionMap_;
};

}  // namespace finevox
```

---

## 10.2 Player Controller

```cpp
namespace finevox {

class PlayerController {
public:
    PlayerController(Entity& player, Camera& camera, InputManager& input, PhysicsSystem& physics);

    void update(float deltaTime);

    // Configuration
    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    void setJumpForce(float force) { jumpForce_ = force; }
    void setMouseSensitivity(float sens) { mouseSensitivity_ = sens; }
    void setFlying(bool flying) { flying_ = flying; }

private:
    Entity& player_;
    Camera& camera_;
    InputManager& input_;
    PhysicsSystem& physics_;

    float moveSpeed_ = 5.0f;      // Blocks per second
    float jumpForce_ = 8.0f;      // Initial Y velocity
    float mouseSensitivity_ = 0.1f;
    bool flying_ = false;

    void handleMovement(float dt);
    void handleLook(float dt);
    void handleActions();
};

}  // namespace finevox
```

---

[Next: Persistence and Serialization](11-persistence.md)
