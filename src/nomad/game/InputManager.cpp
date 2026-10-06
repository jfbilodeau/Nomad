// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/InputManager.hpp>

#include <nomad/game/Game.hpp>

#include <boost/json.hpp>

namespace nomad {

static std::pair<InputCode, const char*> test = { static_cast<short>(1), "" };

static const std::array<std::pair<InputCode, const char*>, 26> INPUT_NAMES = {{
    // Mouse input names
    { INPUT_MOUSE_BUTTON_LEFT, "Mouse Left" },
    { INPUT_MOUSE_BUTTON_MIDDLE, "Mouse Middle" },
    { INPUT_MOUSE_BUTTON_RIGHT, "Mouse Right" },
    { INPUT_MOUSE_BUTTON_X1, "Mouse X1" },
    { INPUT_MOUSE_BUTTON_X2, "Mouse X2" },

    // Controller input names
    { CONTROLLER1_BUTTON_A, "Controller 1 A" },
    { CONTROLLER1_BUTTON_B, "Controller 1 B" },
    { CONTROLLER1_BUTTON_X, "Controller 1 X" },
    { CONTROLLER1_BUTTON_Y, "Controller 1 Y" },
    { CONTROLLER1_BUTTON_BACK, "Controller 1 Back" },
    { CONTROLLER1_BUTTON_GUIDE, "Controller 1 Guide" },
    { CONTROLLER1_BUTTON_START, "Controller 1 Start" },
    { CONTROLLER1_BUTTON_LEFT_STICK, "Controller 1 Left Stick" },
    { CONTROLLER1_BUTTON_RIGHT_STICK, "Controller 1 Right Stick" },
    { CONTROLLER1_BUTTON_LEFT_SHOULDER, "Controller 1 Left Shoulder" },
    { CONTROLLER1_BUTTON_RIGHT_SHOULDER, "Controller 1 Right Shoulder" },
    { CONTROLLER1_BUTTON_DPAD_UP, "Controller 1 D-Pad Up" },
    { CONTROLLER1_BUTTON_DPAD_DOWN, "Controller 1 D-Pad Down" },
    { CONTROLLER1_BUTTON_DPAD_LEFT, "Controller 1 D-Pad Left" },
    { CONTROLLER1_BUTTON_DPAD_RIGHT, "Controller 1 D-Pad Right" },
    { CONTROLLER1_BUTTON_MISC1, "Controller 1 Misc 1" },
    { CONTROLLER1_BUTTON_PADDLE1, "Controller 1 Paddle 1" },
    { CONTROLLER1_BUTTON_PADDLE2, "Controller 1 Paddle 2" },
    { CONTROLLER1_BUTTON_PADDLE3, "Controller 1 Paddle 3" },
    { CONTROLLER1_BUTTON_PADDLE4, "Controller 1 Paddle 4" },
    { CONTROLLER1_BUTTON_TOUCHPAD, "Controller 1 Touchpad" },
}};

NomadString getInputCodeName(InputCode code) {
    // Look for input name
    for (const auto& inputName: INPUT_NAMES) {
        if (inputName.first == code) {
            return inputName.second;
        }
    }

    // Let's ask SDL for the name
    auto sdlName = SDL_GetScancodeName(SDL_Scancode(code));

    return sdlName;
}

InputCode getInputCode(const NomadString& name) {
    // Look for input name
    for (const auto& inputName: INPUT_NAMES) {
        if (inputName.second == name) {
            return inputName.first;
        }
    }

    // Let's ask SDL for the code
    auto sdlCode = SDL_GetScancodeFromName(name.c_str());

    return InputCode(sdlCode);
}

void mapSdlKeyboardEvent(SDL_KeyboardEvent& event, InputEvent& inputEvent) {
    inputEvent.source = InputSource::Keyboard;
    inputEvent.action = event.type == SDL_EVENT_KEY_DOWN ? InputAction::Pressed : InputAction::Released;
    inputEvent.code = InputCode(event.scancode + INPUT_KEYBOARD_OFFSET);
}

void mapSdlMouseEvent(SDL_MouseButtonEvent& event, InputEvent& inputEvent) {
    inputEvent.source = InputSource::Mouse;
    inputEvent.action = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputAction::Pressed : InputAction::Released;
    inputEvent.code = InputCode(event.button + INPUT_MOUSE_OFFSET);
}

void mapSdlGamepadButtonEvent(SDL_GamepadButtonEvent& event, InputEvent& inputEvent) {
    inputEvent.source = InputSource::Controller;
    inputEvent.action = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? InputAction::Pressed : InputAction::Released;
    inputEvent.code = InputCode(event.button + INPUT_CONTROLLER1_OFFSET);
}

} // nomad
