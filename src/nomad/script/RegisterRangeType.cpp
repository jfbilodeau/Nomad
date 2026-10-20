// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RegisterRangeType.hpp>

#include <nomad/script/RangeIterator.hpp>
#include <nomad/script/RangeObject.hpp>
#include <nomad/script/RangeType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerRangeType(Runtime* runtime) {
    auto type = std::make_unique<RangeType>();
    const auto* rangeType = type.get();
    runtime->registerType(std::move(type));
    const auto* integerType = runtime->getIntegerType();
    const auto iteratorId = runtime->registerNativeMethod(
        rangeType, "iterator",
        [integerType](VirtualMachine* machine) {
            machine->getResult().setObjectValue(
                new RangeIterator(integerType, *machine->getThisAs<RangeObject>())
            );
        },
        {}, runtime->getIteratorType(integerType),
        NomadDoc("Creates an independent iterator over the range.")
    );
    runtime->registerIterableType(rangeType, integerType, runtime->getNativeMemberMethod(iteratorId));

    const auto stop = defParameter("stop", integerType, NomadParamDoc("Exclusive end of the range."));
    const auto start = defParameter("start", integerType, NomadParamDoc("Inclusive start of the range."));
    const auto step = defParameter("step", integerType, NomadParamDoc("Non-zero increment."));
    const auto stopId = runtime->registerNativeFunction(
        "range",
        [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new RangeObject(0, machine->getIntegerParameter(0), 1));
        },
        {stop}, rangeType,
        NomadDoc("Creates a range from zero up to, but not including, stop, with step one.")
    );
    const auto boundsId = runtime->registerNativeFunction(
        "range",
        [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(
                new RangeObject(machine->getIntegerParameter(0), machine->getIntegerParameter(1), 1)
            );
        },
        {start, stop}, rangeType,
        NomadDoc("Creates a range from start up to, but not including, stop, with step one.")
    );
    const auto stepId = runtime->registerNativeFunction(
        "range",
        [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new RangeObject(
                machine->getIntegerParameter(0), machine->getIntegerParameter(1), machine->getIntegerParameter(2)
            ));
        },
        {start, stop, step}, rangeType,
        NomadDoc("Creates a range with an exclusive stop. A zero step raises a runtime error.")
    );
    if (stopId == NOMAD_INVALID_ID || boundsId == NOMAD_INVALID_ID || stepId == NOMAD_INVALID_ID) {
        throw NomadException("Failed to register range functions");
    }
}

} // namespace nomad
