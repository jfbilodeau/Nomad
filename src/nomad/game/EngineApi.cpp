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
}

} // namespace nomad
