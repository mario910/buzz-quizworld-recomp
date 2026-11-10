#pragma once
// Caller serializes this state. Visibility and the in-game menu are separate
// owners: neither may clear the other's reason for freezing guest time/audio.
namespace buzz_android {
struct PauseState {
    bool visible = false;
    bool menu = false;
    bool paused() const { return !visible || menu; }
};
}
