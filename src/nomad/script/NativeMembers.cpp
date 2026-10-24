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
    const auto stringTypeId = runtime->getTypeId("string");
    const auto indexerId = runtime->addIndexer(stringTypeId, {
        .valueTypeId = stringTypeId,
        .indexTypes = {runtime->getIntegerType()->getId()},
        .getter = [](VirtualMachine* machine) {
            const auto& text = machine->getThisAs<NomadStringObject>()->getString();
            faultOnInvalidUtf8(machine, text, "indexer");
            const auto codePoints = boost::locale::conv::utf_to_utf<char32_t>(text, boost::locale::conv::stop);
            const auto index = machine->getIntegerParameter(0);
            if (index < 0 || static_cast<NomadIndex>(index) >= codePoints.size()) {
                machine->fault("String index " + toString(index) + " is out of bounds for size " + toString(codePoints.size()));
            }
            machine->setStringResult(boost::locale::conv::utf_to_utf<char>(
                codePoints.substr(static_cast<NomadIndex>(index), 1), boost::locale::conv::stop
            ));
        },
        .setter = {},
        .documentation = NomadDoc("Returns one Unicode code point as a string at a zero-based index. Read-only; out-of-range indices fault.")
    });
    if (indexerId.isInvalid()) {
        throw NomadException("Failed to register string indexer");
    }
    const auto lengthId = runtime->addProperty(stringTypeId, {
        .valueTypeId = runtime->getTypeId("int"),
        .name = "length",
        .getter = [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            const auto& text = virtualMachine->getThisAs<NomadStringObject>()->getString();

            try {
                const auto codePoints =
                    boost::locale::conv::utf_to_utf<char32_t>(text, boost::locale::conv::stop);
                result.setIntegerValue(static_cast<NomadInteger>(codePoints.size()));
            } catch (const boost::locale::conv::conversion_error&) {
                virtualMachine->fault("Native string member 'length' requires valid UTF-8");
            }
        },
        .setter = {},
        .documentation = NomadDoc("The number of characters (Unicode code points) in the string.")
    });

    const auto byteLengthId = runtime->addProperty(stringTypeId, {
        .valueTypeId = runtime->getTypeId("int"),
        .name = "byteLength",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(static_cast<NomadInteger>(machine->getThisAs<NomadStringObject>()->getLength()));
        },
        .setter = {},
        .documentation = NomadDoc("The number of bytes in the UTF-8 encoding of the string.")
    });

    const auto isEmptyId = runtime->addProperty(stringTypeId, {
        .valueTypeId = runtime->getTypeId("bool"),
        .name = "isEmpty",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            result.setBooleanValue(machine->getThisAs<NomadStringObject>()->getString().empty());
        },
        .setter = {},
        .documentation = NomadDoc("True when the string is empty.")
    });
    if (lengthId.isInvalid() || byteLengthId.isInvalid() || isEmptyId.isInvalid()) {
        throw NomadException("Failed to register string properties");
    }

    const auto toUpperId = runtime->addMethod(stringTypeId, {
        .name = "toUpper",
        .parameters = {},
        .returnTypeId = stringTypeId,
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "toUpper");
            virtualMachine->setStringResult(stringToUpper(text));
        },
        .documentation = NomadDoc("Return an uppercase copy of the string.")
    });

    const auto toLowerId = runtime->addMethod(stringTypeId, {
        .name = "toLower",
        .parameters = {},
        .returnTypeId = stringTypeId,
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "toLower");
            virtualMachine->setStringResult(stringToLower(text));
        },
        .documentation = NomadDoc("Return a lowercase copy of the string.")
    });

    const auto trimId = runtime->addMethod(stringTypeId, {
        .name = "trim",
        .parameters = {},
        .returnTypeId = stringTypeId,
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trim");
            virtualMachine->setStringResult(stringTrimCopy(text));
        },
        .documentation = NomadDoc("Return a copy with whitespace removed from both ends.")
    });

    const auto trimStartId = runtime->addMethod(stringTypeId, {
        .name = "trimStart",
        .parameters = {},
        .returnTypeId = stringTypeId,
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trimStart");
            virtualMachine->setStringResult(stringLeftTrimCopy(text));
        },
        .documentation = NomadDoc("Return a copy with whitespace removed from the beginning.")
    });

    const auto trimEndId = runtime->addMethod(stringTypeId, {
        .name = "trimEnd",
        .parameters = {},
        .returnTypeId = stringTypeId,
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            faultOnInvalidUtf8(virtualMachine, text, "trimEnd");
            virtualMachine->setStringResult(stringRightTrimCopy(text));
        },
        .documentation = NomadDoc("Return a copy with whitespace removed from the end.")
    });

    const auto stringSearchParameters = std::vector<ParameterDefinition>{
        {stringTypeId, "text", NomadParamDoc("The text to find")},
    };

    const auto startsWithId = runtime->addMethod(stringTypeId, {
        .name = "startsWith",
        .parameters = stringSearchParameters,
        .returnTypeId = runtime->getTypeId("bool"),
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "startsWith");
            virtualMachine->setBooleanResult(text.starts_with(search));
        },
        .documentation = NomadDoc("Return true when the string starts with the given text.")
    });

    const auto endsWithId = runtime->addMethod(stringTypeId, {
        .name = "endsWith",
        .parameters = stringSearchParameters,
        .returnTypeId = runtime->getTypeId("bool"),
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "endsWith");
            virtualMachine->setBooleanResult(text.ends_with(search));
        },
        .documentation = NomadDoc("Return true when the string ends with the given text.")
    });

    const auto containsId = runtime->addMethod(stringTypeId, {
        .name = "contains",
        .parameters = stringSearchParameters,
        .returnTypeId = runtime->getTypeId("bool"),
        .function = [](VirtualMachine* virtualMachine) {
            const auto* instance = virtualMachine->getThisAs<NomadStringObject>();
            const auto& text = instance->getString();
            const auto& search = virtualMachine->getStringParameter(0)->getString();
            validateSearchOperands(virtualMachine, text, search, "contains");
            virtualMachine->setBooleanResult(text.find(search) != NomadString::npos);
        },
        .documentation = NomadDoc("Return true when the string contains the given text.")
    });
    if (toUpperId.isInvalid() || toLowerId.isInvalid() || trimId.isInvalid() ||
        trimStartId.isInvalid() || trimEndId.isInvalid() || startsWithId.isInvalid() ||
        endsWithId.isInvalid() || containsId.isInvalid()) {
        throw NomadException("Failed to register string methods");
    }
}

} // namespace nomad
