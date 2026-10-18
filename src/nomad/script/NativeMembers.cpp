// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeMembers.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <boost/locale/encoding_utf.hpp>

#include <vector>

namespace nomad {

namespace {

void faultOnInvalidUtf8(VirtualMachine* virtualMachine, const NomadStringView text, const NomadString& memberName) {
    if (!isValidUtf8(text)) {
        virtualMachine->fault("Native string member '" + memberName + "' requires valid UTF-8");
    }
}

void validateSearchOperands(VirtualMachine* virtualMachine, const NomadString& receiver, const NomadString& search,
                            const NomadString& memberName) {
    faultOnInvalidUtf8(virtualMachine, receiver, memberName);
    faultOnInvalidUtf8(virtualMachine, search, memberName);
}

} // namespace

void registerBuiltInNativeMembers(Runtime* runtime) {
    const auto* stringType = runtime->getStringType();
    runtime->registerNativeIndexer1(
        stringType,
        runtime->getIntegerType(),
        stringType,
        [](VirtualMachine* machine, const RuntimeValue& indexValue, RuntimeValue& result) {
            const auto& text = machine->getThisAs<NomadStringObject>()->getString();
            faultOnInvalidUtf8(machine, text, "indexer");
            const auto codePoints = boost::locale::conv::utf_to_utf<char32_t>(text, boost::locale::conv::stop);
            const auto index = indexValue.getIntegerValue();
            if (index < 0 || static_cast<NomadIndex>(index) >= codePoints.size()) {
                machine->fault("String index " + toString(index) + " is out of bounds for size " + toString(codePoints.size()));
            }
            result.setStringValue(boost::locale::conv::utf_to_utf<char>(
                codePoints.substr(static_cast<NomadIndex>(index), 1), boost::locale::conv::stop
            ));
        },
        {},
        NomadDoc("Returns one Unicode code point as a string at a zero-based index. Read-only; out-of-range indices fault.")
    );
    runtime->registerNativeProperty(
        stringType,
        "length",
        runtime->getIntegerType(),
        [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            const auto& text = virtualMachine->getThisAs<NomadStringObject>()->getString();

            try {
                const auto codePoints =
                    boost::locale::conv::utf_to_utf<char32_t>(text, boost::locale::conv::stop);
                result.setIntegerValue(static_cast<NomadInteger>(codePoints.size()));
            } catch (const boost::locale::conv::conversion_error&) {
                virtualMachine->fault("Native string member 'length' requires valid UTF-8");
            }
        },
        {},
        NomadDoc("The number of characters (Unicode code points) in the string.")
    );

    runtime->registerNativeProperty(
        stringType,
        "byteLength",
        runtime->getIntegerType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(static_cast<NomadInteger>(machine->getThisAs<NomadStringObject>()->getLength()));
        },
        {},
        NomadDoc("The number of bytes in the UTF-8 encoding of the string.")
    );

    runtime->registerNativeProperty(
        stringType,
        "isEmpty",
        runtime->getBooleanType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setBooleanValue(machine->getThisAs<NomadStringObject>()->getString().empty());
        },
        {},
        NomadDoc("True when the string is empty.")
    );

    runtime->registerNativeMethod(
        stringType,
        "toUpper",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "toUpper");
            virtualMachine->setStringResult(stringToUpper(text));
        },
        {},
        runtime->getStringType(),
        NomadDoc("Return an uppercase copy of the string.")
    );

    runtime->registerNativeMethod(
        stringType,
        "toLower",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "toLower");
            virtualMachine->setStringResult(stringToLower(text));
        },
        {},
        runtime->getStringType(),
        NomadDoc("Return a lowercase copy of the string.")
    );

    runtime->registerNativeMethod(
        stringType,
        "trim",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trim");
            virtualMachine->setStringResult(stringTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        NomadDoc("Return a copy with whitespace removed from both ends.")
    );

    runtime->registerNativeMethod(
        stringType,
        "trimStart",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trimStart");
            virtualMachine->setStringResult(stringLeftTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        NomadDoc("Return a copy with whitespace removed from the beginning.")
    );

    runtime->registerNativeMethod(
        stringType,
        "trimEnd",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trimEnd");
            virtualMachine->setStringResult(stringRightTrimCopy(text));
        },
        {},
        runtime->getStringType(),
        NomadDoc("Return a copy with whitespace removed from the end.")
    );

    const auto stringSearchParameters = std::vector{
        defParameter("text", runtime->getStringType(), NomadParamDoc("The text to find")),
    };

    runtime->registerNativeMethod(
        stringType,
        "startsWith",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "startsWith");
            virtualMachine->setBooleanResult(text.starts_with(search));
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        NomadDoc("Return true when the string starts with the given text.")
    );

    runtime->registerNativeMethod(
        stringType,
        "endsWith",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "endsWith");
            virtualMachine->setBooleanResult(text.ends_with(search));
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        NomadDoc("Return true when the string ends with the given text.")
    );

    runtime->registerNativeMethod(
        stringType,
        "contains",
        [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "contains");
            virtualMachine->setBooleanResult(text.find(search) != NomadString::npos);
        },
        stringSearchParameters,
        runtime->getBooleanType(),
        NomadDoc("Return true when the string contains the given text.")
    );
}

} // namespace nomad
