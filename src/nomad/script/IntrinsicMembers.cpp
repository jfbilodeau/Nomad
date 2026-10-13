// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/IntrinsicMembers.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <boost/locale/encoding_utf.hpp>

#include <vector>

namespace nomad {

namespace {

void faultOnInvalidUtf8(VirtualMachine* virtualMachine, const NomadStringView text, const NomadString& memberName) {
    if (!isValidUtf8(text)) {
        virtualMachine->fault("Intrinsic string member '" + memberName + "' requires valid UTF-8");
    }
}

NomadString getReceiverText(const VirtualMachine* virtualMachine) {
    const auto* receiver = virtualMachine->getIntrinsicReceiver().getStringValue();
    return receiver != nullptr ? receiver : "";
}

NomadString getStringParameter(const VirtualMachine* virtualMachine, const NomadIndex index) {
    const auto* parameter = virtualMachine->getStringParameter(index);
    return parameter != nullptr ? parameter : "";
}

void validateSearchOperands(VirtualMachine* virtualMachine, const NomadString& receiver, const NomadString& search,
                            const NomadString& memberName) {
    faultOnInvalidUtf8(virtualMachine, receiver, memberName);
    faultOnInvalidUtf8(virtualMachine, search, memberName);
}

} // namespace

void registerBuiltInIntrinsicMembers(Runtime* runtime) {
    runtime->registerIntrinsicProperty(
        runtime->getStringType(),
        "length",
        runtime->getIntegerType(),
        [](VirtualMachine* virtualMachine, const RuntimeValue& receiver, RuntimeValue& result) {
            const NomadString text = receiver.getStringValue() != nullptr ? receiver.getStringValue() : "";

            try {
                const auto codePoints =
                    boost::locale::conv::utf_to_utf<char32_t>(text, boost::locale::conv::stop);
                result.setIntegerValue(static_cast<NomadInteger>(codePoints.size()));
            } catch (const boost::locale::conv::conversion_error&) {
                virtualMachine->fault("Intrinsic string member 'length' requires valid UTF-8");
            }
        },
        {},
        NomadDoc("The number of characters (Unicode code points) in the string.")
    );

    runtime->registerIntrinsicProperty(
        runtime->getStringType(),
        "byteLength",
        runtime->getIntegerType(),
        [](VirtualMachine*, const RuntimeValue& receiver, RuntimeValue& result) {
            const auto* text = receiver.getStringValue();
            result.setIntegerValue(static_cast<NomadInteger>(text != nullptr ? std::char_traits<NomadChar>::length(text) : 0));
        },
        {},
        NomadDoc("The number of bytes in the UTF-8 encoding of the string.")
    );

    runtime->registerIntrinsicProperty(
        runtime->getStringType(),
        "isEmpty",
        runtime->getBooleanType(),
        [](VirtualMachine*, const RuntimeValue& receiver, RuntimeValue& result) {
            const auto* text = receiver.getStringValue();
            result.setBooleanValue(text == nullptr || *text == '\0');
        },
        {},
        NomadDoc("True when the string is empty.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "toUpper",
        [](VirtualMachine* virtualMachine) {
            const auto* receiver = virtualMachine->getIntrinsicReceiver().getStringValue();
            const NomadString text = receiver != nullptr ? receiver : "";
            faultOnInvalidUtf8(virtualMachine, text, "toUpper");
            virtualMachine->setStringResult(stringToUpper(text));
        },
        {},
        runtime->getStringType(),
        false,
        NomadDoc("Return an uppercase copy of the string.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "toLower",
        [](VirtualMachine* virtualMachine) {
            const auto text = getReceiverText(virtualMachine);
            faultOnInvalidUtf8(virtualMachine, text, "toLower");
            virtualMachine->setStringResult(stringToLower(text));
        },
        {},
        runtime->getStringType(),
        false,
        NomadDoc("Return a lowercase copy of the string.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "trim",
        [](VirtualMachine* virtualMachine) {
            const auto text = getReceiverText(virtualMachine);
            faultOnInvalidUtf8(virtualMachine, text, "trim");
            virtualMachine->setStringResult(stringTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        false,
        NomadDoc("Return a copy with whitespace removed from both ends.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "trimStart",
        [](VirtualMachine* virtualMachine) {
            const auto text = getReceiverText(virtualMachine);
            faultOnInvalidUtf8(virtualMachine, text, "trimStart");
            virtualMachine->setStringResult(stringLeftTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        false,
        NomadDoc("Return a copy with whitespace removed from the beginning.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "trimEnd",
        [](VirtualMachine* virtualMachine) {
            const auto text = getReceiverText(virtualMachine);
            faultOnInvalidUtf8(virtualMachine, text, "trimEnd");
            virtualMachine->setStringResult(stringRightTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        false,
        NomadDoc("Return a copy with whitespace removed from the end.")
    );

    const auto stringSearchParameters = std::vector{
        defParameter("text", runtime->getStringRefType(), NomadParamDoc("The text to find")),
    };

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "startsWith",
        [](VirtualMachine* virtualMachine) {
            const auto receiver = getReceiverText(virtualMachine);
            const auto search = getStringParameter(virtualMachine, 0);
            validateSearchOperands(virtualMachine, receiver, search, "startsWith");
            virtualMachine->setBooleanResult(receiver.starts_with(search));
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        false,
        NomadDoc("Return true when the string starts with the given text.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "endsWith",
        [](VirtualMachine* virtualMachine) {
            const auto receiver = getReceiverText(virtualMachine);
            const auto search = getStringParameter(virtualMachine, 0);
            validateSearchOperands(virtualMachine, receiver, search, "endsWith");
            virtualMachine->setBooleanResult(receiver.ends_with(search));
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        false,
        NomadDoc("Return true when the string ends with the given text.")
    );

    runtime->registerIntrinsicMethod(
        runtime->getStringType(),
        "contains",
        [](VirtualMachine* virtualMachine) {
            const auto receiver = getReceiverText(virtualMachine);
            const auto search = getStringParameter(virtualMachine, 0);
            validateSearchOperands(virtualMachine, receiver, search, "contains");
            virtualMachine->setBooleanResult(receiver.find(search) != NomadString::npos);
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        false,
        NomadDoc("Return true when the string contains the given text.")
    );
}

} // namespace nomad
