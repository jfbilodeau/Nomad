// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/EngineApi.hpp>

#include <nomad/game/Alignment.hpp>
#include <nomad/game/BodyType.hpp>
#include <nomad/game/Cardinal.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void registerEngineApi(Runtime* runtime) {
    const auto* integerType = runtime->getIntegerType();
    const auto registerConstant = [runtime, integerType](const NomadString& name, const NomadInteger value) {
        if (runtime->registerConstant(name, RuntimeValue(value), integerType) == NOMAD_INVALID_ID) {
            throw NomadBug("Failed to register engine API constant '" + name + "'");
        }
    };

    const auto registerEvent = [runtime](const NomadString& name) {
        if (runtime->registerEvent(name, {}) == NOMAD_INVALID_ID) {
            throw NomadBug("Failed to register engine API event '" + name + "'");
        }
    };

    registerEvent("update");
    registerEvent("beforeUpdate");
    registerEvent("afterUpdate");

    registerConstant("alignment.topLeft", static_cast<NomadInteger>(Alignment::TopLeft));
    registerConstant("alignment.topMiddle", static_cast<NomadInteger>(Alignment::TopMiddle));
    registerConstant("alignment.topRight", static_cast<NomadInteger>(Alignment::TopRight));
    registerConstant("alignment.centerLeft", static_cast<NomadInteger>(Alignment::CenterLeft));
    registerConstant("alignment.centerMiddle", static_cast<NomadInteger>(Alignment::CenterMiddle));
    registerConstant("alignment.centerRight", static_cast<NomadInteger>(Alignment::CenterRight));
    registerConstant("alignment.bottomLeft", static_cast<NomadInteger>(Alignment::BottomLeft));
    registerConstant("alignment.bottomMiddle", static_cast<NomadInteger>(Alignment::BottomMiddle));
    registerConstant("alignment.bottomRight", static_cast<NomadInteger>(Alignment::BottomRight));
    registerConstant("alignment.left", static_cast<NomadInteger>(HorizontalAlignment::Left));
    registerConstant("alignment.middle", static_cast<NomadInteger>(HorizontalAlignment::Middle));
    registerConstant("alignment.right", static_cast<NomadInteger>(HorizontalAlignment::Right));
    registerConstant("alignment.top", static_cast<NomadInteger>(VerticalAlignment::Top));
    registerConstant("alignment.center", static_cast<NomadInteger>(VerticalAlignment::Center));
    registerConstant("alignment.bottom", static_cast<NomadInteger>(VerticalAlignment::Bottom));

    registerConstant("body.static", static_cast<NomadInteger>(BodyType::Static));
    registerConstant("body.dynamic", static_cast<NomadInteger>(BodyType::Dynamic));
    registerConstant("body.kinematic", static_cast<NomadInteger>(BodyType::Kinematic));

    registerConstant("cardinal.north", static_cast<NomadInteger>(Cardinal::North));
    registerConstant("cardinal.east", static_cast<NomadInteger>(Cardinal::East));
    registerConstant("cardinal.south", static_cast<NomadInteger>(Cardinal::South));
    registerConstant("cardinal.west", static_cast<NomadInteger>(Cardinal::West));

    const auto registerFunction = [runtime](
        const NomadString& name,
        const std::vector<NativeFunctionParameterDefinition>& parameters,
        const Type* returnType,
        const NomadString& doc
    ) {
        if (runtime->registerNativeFunction(name, parameters, returnType, doc) == NOMAD_INVALID_ID) {
            throw NomadBug("Failed to register engine API function '" + name + "'");
        }
    };

    registerFunction(
        "rgb",
        {
            defParameter("r", integerType, NomadParamDoc("Red component (0-255).")),
            defParameter("g", integerType, NomadParamDoc("Green component (0-255).")),
            defParameter("b", integerType, NomadParamDoc("Blue component (0-255)."))
        },
        integerType,
        NomadDoc("Creates an RGB color from red, green and blue components (0-255).")
    );

    registerFunction(
        "rgba",
        {
            defParameter("r", integerType, NomadParamDoc("Red component (0-255).")),
            defParameter("g", integerType, NomadParamDoc("Green component (0-255).")),
            defParameter("b", integerType, NomadParamDoc("Blue component (0-255).")),
            defParameter("a", integerType, NomadParamDoc("Alpha component (0-255)."))
        },
        integerType,
        NomadDoc("Creates an RGBA color from red, green, blue and alpha components (0-255).")
    );

    registerFunction(
        "system.exit",
        {},
        runtime->getVoidType(),
        NomadDoc("Exits the game.")
    );

    const auto* voidCallbackType = runtime->getCallbackType({}, runtime->getVoidType());
    const auto* stringRefType = runtime->getStringRefType();
    const auto* voidType = runtime->getVoidType();

    registerFunction(
        "game.createScene",
        {
            defParameter(
                "function",
                voidCallbackType,
                NomadParamDoc("The function to execute to initialize the scene. Return the scene id.")
            )
        },
        integerType,
        NomadDoc("Creates a new scene.")
    );

    registerFunction(
        "game.createSceneByName",
        {
            defParameter(
                "functionName",
                stringRefType,
                NomadParamDoc("Name of the function to execute to initialize the scene. Return the scene id.")
            )
        },
        integerType,
        NomadDoc("Creates a new scene.")
    );

    registerFunction(
        "game.createSceneByNameThen",
        {
            defParameter(
                "functionName",
                stringRefType,
                NomadParamDoc("Name of the function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction",
                voidCallbackType,
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        integerType,
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    registerFunction(
        "game.createSceneThen",
        {
            defParameter(
                "function",
                voidCallbackType,
                NomadParamDoc("The function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction",
                voidCallbackType,
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        integerType,
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    registerFunction(
        "game.isInScene",
        {},
        runtime->getBooleanType(),
        NomadDoc("Returns true if this function is executing in a scene.")
    );

    registerFunction(
        "game.loadFont",
        {
            defParameter("fontName", stringRefType, NomadParamDoc("Name of the font to load.")),
            defParameter("fontSize", integerType, NomadParamDoc("Size of the font in point to load."))
        },
        integerType,
        NomadDoc("Loads a font from a file. Returns the ID of the font.")
    );

    registerFunction(
        "game.loadImage",
        {
            defParameter("imageName", stringRefType, NomadParamDoc("Name of the image to load."))
        },
        integerType,
        NomadDoc("Loads a font from a file.")
    );

    registerFunction(
        "game.loadSpriteAtlas",
        {
            defParameter("atlasName", stringRefType, NomadParamDoc("Name of the sprite atlas to load."))
        },
        voidType,
        NomadDoc("Loads a sprite atlas from a file.")
    );

    registerFunction(
        "game.inventory.load",
        {
            defParameter("saveName", stringRefType, NomadParamDoc("Name of the save to load."))
        },
        voidType,
        NomadDoc(
            "Loads `inventory.*` variables from `<pref>/save/<saveName>.json`. Variables absent from the save keep "
            "their current value."
        )
    );

    registerFunction(
        "game.inventory.save",
        {
            defParameter("saveName", stringRefType, NomadParamDoc("Name of the save."))
        },
        voidType,
        NomadDoc("Saves all `inventory.*` variables to `<pref>/save/<saveName>.json`.")
    );

    registerFunction(
        "game.inventory.saveExists",
        {
            defParameter("saveName", stringRefType, NomadParamDoc("Name of the save."))
        },
        runtime->getBooleanType(),
        NomadDoc("Returns true if the save `<pref>/save/<saveName>.json` exists.")
    );

    registerFunction(
        "game.settings.load",
        {},
        voidType,
        NomadDoc(
            "Loads `settings.*` variables from `<pref>/settings.json`. Variables absent from the file keep their "
            "current value."
        )
    );

    registerFunction(
        "game.settings.save",
        {},
        voidType,
        NomadDoc("Saves all `settings.*` variables to `<pref>/settings.json`.")
    );

    registerFunction(
        "game.trigger",
        {
            defParameter(
                "event",
                runtime->getEventDispatchType(),
                NomadParamDoc("The event to dispatch (trigger).")
            )
        },
        voidType,
        NomadDoc("Trigger a global event.")
    );

    registerFunction(
        "window.maximize",
        {},
        voidType,
        NomadDoc("Maximizes the window.")
    );

    registerFunction(
        "window.minimize",
        {},
        voidType,
        NomadDoc("Minimizes the window.")
    );

    registerFunction(
        "window.setFps",
        {
            defParameter("framesPerSecond", integerType, NomadParamDoc("Frames per second."))
        },
        voidType,
        NomadDoc("Sets the frames per seconds (FPS) of the game.")
    );

    registerFunction(
        "window.setResolution",
        {
            defParameter("resolutionWidth", integerType, NomadParamDoc("Resolution width.")),
            defParameter("resolutionHeight", integerType, NomadParamDoc("Resolution height."))
        },
        voidType,
        NomadDoc("Sets the resolution of the game window.")
    );

    registerFunction(
        "window.setSize",
        {
            defParameter("windowWidth", integerType, NomadParamDoc("Window width.")),
            defParameter("windowHeight", integerType, NomadParamDoc("Window height."))
        },
        voidType,
        NomadDoc("Sets the size of the game window.")
    );

    registerFunction(
        "window.setSizeAndCenter",
        {
            defParameter("windowWidth", integerType, NomadParamDoc("Window width.")),
            defParameter("windowHeight", integerType, NomadParamDoc("Window height."))
        },
        voidType,
        NomadDoc("Sets the size of the game window.")
    );

    registerFunction(
        "window.setTitle",
        {
            defParameter(
                "windowTitle",
                runtime->getStringRefType(),
                NomadParamDoc("Title of the game window.")
            )
        },
        voidType,
        NomadDoc("Set the title of the game window.")
    );

    registerFunction(
        "window.toggleFullScreen",
        {},
        voidType,
        NomadDoc("Toggles the window between windowed and full-screen modes.")
    );

    const auto registerWindowCallback = [runtime, &registerFunction](
        const NomadString& name,
        const NomadString& clearName,
        const std::vector<const Type*>& parameterTypes,
        const NomadString& documentation
    ) {
        registerFunction(
            name,
            {
                defParameter(
                    "callback",
                    runtime->getCallbackType(parameterTypes, runtime->getVoidType()),
                    NomadParamDoc("Callback to invoke when the window event occurs.")
                )
            },
            runtime->getVoidType(),
            documentation
        );

        registerFunction(
            clearName,
            {},
            runtime->getVoidType(),
            NomadDoc("Clears the callback registered by " + name + ".")
        );
    };

    registerWindowCallback(
        "window.onClose",
        "window.clearOnClose",
        {},
        "Sets the callback invoked when the user requests that the window close. The window is not closed automatically."
    );

    registerWindowCallback(
        "window.onGainFocus",
        "window.clearOnGainFocus",
        {},
        "Sets the callback invoked when the window gains focus."
    );

    registerWindowCallback(
        "window.onLoseFocus",
        "window.clearOnLoseFocus",
        {},
        "Sets the callback invoked when the window loses focus."
    );

    registerWindowCallback(
        "window.onMaximize",
        "window.clearOnMaximize",
        {},
        "Sets the callback invoked when the window is maximized."
    );

    registerWindowCallback(
        "window.onMinimize",
        "window.clearOnMinimize",
        {},
        "Sets the callback invoked when the window is minimized."
    );

    registerWindowCallback(
        "window.onMove",
        "window.clearOnMove",
        {integerType, integerType},
        "Sets the callback invoked when the window is moved. Receives the x and y position."
    );

    registerWindowCallback(
        "window.onResize",
        "window.clearOnResize",
        {integerType, integerType},
        "Sets the callback invoked when the window is resized. Receives the width and height."
    );

    registerWindowCallback(
        "window.onRestore",
        "window.clearOnRestore",
        {},
        "Sets the callback invoked when the window is restored."
    );
}

} // namespace nomad
