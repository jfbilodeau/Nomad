#include <DynamicVariableCopyProbe.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/DynamicVariable.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadObject.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/scope_exit.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace {

#if defined(_MSC_VER) && defined(_DEBUG)
thread_local auto hostCallAllocations = nomad::NomadIndex{0};
thread_local auto hostAllocationProbeEnabled = false;
_CRT_ALLOC_HOOK previousHostAllocationHook = nullptr;

int countHostAllocation(
    const int operation,
    void* data,
    const size_t size,
    const int blockType,
    const long request,
    const unsigned char* file,
    const int line
) {
    if (hostAllocationProbeEnabled && (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)) {
        ++hostCallAllocations;
    }

    return previousHostAllocationHook == nullptr ||
        previousHostAllocationHook(operation, data, size, blockType, request, file, line);
}
#endif

struct PropertyBenchmarkState {
    nomad::NomadInteger value = 0;
    nomad::NomadIndex reads = 0;
    nomad::NomadIndex writes = 0;
    nomad::NomadObject* receiver = nullptr;
};

struct DynamicVariableBenchmarkState {
    nomad::NomadInteger value = 0;
    nomad::NomadIndex reads = 0;
    nomad::NomadIndex writes = 0;
    nomad::VirtualMachine* machine = nullptr;
};

double measureLookup(
    const nomad::Runtime& runtime,
    const nomad::NomadString& name,
    const nomad::CallableId expectedId,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        if (runtime.getFunctionId(name) != expectedId) {
            throw std::runtime_error("Unexpected function ID for benchmark lookup '" + name + "'");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        checksum += static_cast<std::uint64_t>(runtime.getFunctionId(name).id + 1);
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto expectedChecksum = static_cast<std::uint64_t>(expectedId.id + 1) * iterations;
    if (checksum != expectedChecksum) {
        throw std::runtime_error("Benchmark lookup results changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

double measureStringLookup(
    const nomad::Runtime& runtime,
    const nomad::NomadString& text,
    const nomad::StringId expectedId,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        if (runtime.getStringId(text) != expectedId) {
            throw std::runtime_error("Unexpected string ID in benchmark lookup");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        checksum += static_cast<std::uint64_t>(runtime.getStringId(text).id + 1);
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto expectedChecksum = static_cast<std::uint64_t>(expectedId.id + 1) * iterations;
    if (checksum != expectedChecksum) {
        throw std::runtime_error("Benchmark string lookup results changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

double measureStringDeduplication(
    nomad::Runtime& runtime,
    const nomad::NomadString& text,
    const nomad::StringId expectedId,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        if (runtime.registerString(text) != expectedId) {
            throw std::runtime_error("Benchmark string deduplication changed the existing ID");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        checksum += static_cast<std::uint64_t>(runtime.registerString(text).id + 1);
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto expectedChecksum = static_cast<std::uint64_t>(expectedId.id + 1) * iterations;
    if (checksum != expectedChecksum) {
        throw std::runtime_error("Benchmark string deduplication changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

template<typename Resolver>
double measureResolution(
    const char* name,
    const Resolver& resolve,
    const nomad::ResolveResult& expected,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        const auto result = resolve();
        if (
            result.status != expected.status ||
            result.id != expected.id ||
            result.candidates != expected.candidates ||
            result.argumentTypes != expected.argumentTypes
        ) {
            throw std::runtime_error(std::format("Unexpected benchmark resolution for '{}'", name));
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        const auto result = resolve();
        checksum += static_cast<std::uint64_t>(result.id.id + 1)
            + static_cast<std::uint64_t>(result.status) + result.candidates.size();
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto expectedChecksum = (
        static_cast<std::uint64_t>(expected.id.id + 1)
        + static_cast<std::uint64_t>(expected.status) + expected.candidates.size()
    ) * iterations;
    if (checksum != expectedChecksum) {
        throw std::runtime_error(std::format("Benchmark resolution changed for '{}'", name));
    }

    return elapsed / static_cast<double>(iterations);
}

double measureCurrentCallable(
    const nomad::VirtualMachine& machine,
    const nomad::ScriptCallableBase* expected,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        if (machine.getCurrentCallable() != expected) {
            throw std::runtime_error("The benchmark probe has the wrong active script callable");
        }
    }

    auto matchingQueries = nomad::NomadIndex{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        matchingQueries += machine.getCurrentCallable() == expected;
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    if (matchingQueries != iterations) {
        throw std::runtime_error("The active script callable changed during benchmark lookup");
    }

    return elapsed / static_cast<double>(iterations);
}

double measureNestedScriptCalls(
    nomad::VirtualMachine& machine,
    const nomad::ScriptFunction& caller,
    const nomad::NomadIndex iterations
) {
    const auto before = machine.captureExecutionState();
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        machine.executeFunction(caller);
        if (machine.getIntegerResult() != 42) {
            throw std::runtime_error("Unexpected nested script-call benchmark result");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        machine.executeFunction(caller);
        checksum += static_cast<std::uint64_t>(machine.getIntegerResult());
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto after = machine.captureExecutionState();
    if (
        checksum != std::uint64_t{42} * iterations ||
        after.instructionIndex != before.instructionIndex ||
        after.stackIndex != before.stackIndex ||
        after.parameterIndex != before.parameterIndex ||
        after.variableIndex != before.variableIndex ||
        after.running != before.running ||
        after.receiver.getObjectValue() != before.receiver.getObjectValue() ||
        machine.getCurrentCallable() != nullptr
    ) {
        throw std::runtime_error("Nested script calls changed their result or enclosing execution state");
    }

    return elapsed / static_cast<double>(iterations);
}

double measurePropertyDescriptor(
    const nomad::Runtime& runtime,
    const nomad::NativeProperty& expected,
    const nomad::NomadIndex iterations
) {
    const auto propertyId = expected.getId();
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        if (runtime.getNativeProperty(propertyId) != &expected) {
            throw std::runtime_error("Unexpected native-property benchmark descriptor");
        }
    }

    auto matchingDescriptors = nomad::NomadIndex{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        matchingDescriptors += runtime.getNativeProperty(propertyId) == &expected;
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    if (matchingDescriptors != iterations) {
        throw std::runtime_error("Native-property descriptor identity changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

double measurePropertyInfoCopy(
    const nomad::Runtime& runtime,
    const nomad::NativeProperty& expected,
    const nomad::NomadIndex iterations
) {
    const auto propertyId = expected.getId();
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        const auto info = runtime.getNativePropertyInfo(propertyId);
        if (
            !info ||
            info->id != propertyId ||
            info->classType != expected.getClassType() ||
            info->valueType != expected.getValueType() ||
            info->name != expected.getName() ||
            info->doc != expected.getDoc() ||
            !info->getFn ||
            !info->setFn ||
            info->getterMethodId.isValid() ||
            info->setterMethodId.isValid()
        ) {
            throw std::runtime_error("Unexpected copied native-property benchmark metadata");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        const auto info = runtime.getNativePropertyInfo(propertyId);
        if (!info) {
            throw std::runtime_error("Native-property metadata disappeared during measurement");
        }

        checksum += static_cast<std::uint64_t>(info->id.id + 1) + info->name.size() + info->doc.size()
            + static_cast<std::uint64_t>(static_cast<bool>(info->getFn))
            + static_cast<std::uint64_t>(static_cast<bool>(info->setFn));
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    const auto expectedChecksum = (
        static_cast<std::uint64_t>(propertyId.id + 1)
        + expected.getName().size() + expected.getDoc().size() + 2
    ) * iterations;
    if (checksum != expectedChecksum) {
        throw std::runtime_error("Native-property metadata changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

double measurePropertyGetter(
    nomad::VirtualMachine& machine,
    const nomad::PropertyId propertyId,
    const nomad::RuntimeValue& receiver,
    const nomad::NomadIndex iterations
) {
    nomad::RuntimeValue result;
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        machine.getNativeProperty(propertyId, receiver, result);
        if (result.getIntegerValue() != 42) {
            throw std::runtime_error("Unexpected native-property benchmark getter result");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        machine.getNativeProperty(propertyId, receiver, result);
        checksum += static_cast<std::uint64_t>(result.getIntegerValue());
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    if (checksum != std::uint64_t{42} * iterations) {
        throw std::runtime_error("Native-property getter results changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

double measurePropertySetter(
    nomad::VirtualMachine& machine,
    const nomad::PropertyId propertyId,
    const nomad::RuntimeValue& receiver,
    const PropertyBenchmarkState& state,
    const nomad::NomadInteger bias,
    const nomad::NomadIndex iterations
) {
    nomad::RuntimeValue value;
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        value.setIntegerValue(static_cast<nomad::NomadInteger>(index));
        machine.setNativeProperty(propertyId, receiver, value);
        if (state.value != value.getIntegerValue() - bias) {
            throw std::runtime_error("Unexpected native-property benchmark setter side effect");
        }
    }

    auto matchingWrites = nomad::NomadIndex{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        value.setIntegerValue(static_cast<nomad::NomadInteger>(index % 64));
        machine.setNativeProperty(propertyId, receiver, value);
        matchingWrites += state.value == value.getIntegerValue() - bias;
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    if (matchingWrites != iterations) {
        throw std::runtime_error("Native-property setter side effects changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

template<typename Invocation>
std::optional<nomad::NomadIndex> countHostCallAllocations(
    const Invocation& invoke,
    const nomad::NomadIndex iterations
) {
#if defined(_MSC_VER) && defined(_DEBUG)
    const auto previousHook = _CrtSetAllocHook(countHostAllocation);
    previousHostAllocationHook = previousHook;
    hostCallAllocations = 0;
    hostAllocationProbeEnabled = true;
    BOOST_SCOPE_EXIT_ALL(previousHook) {
        hostAllocationProbeEnabled = false;
        _CrtSetAllocHook(previousHook);
        previousHostAllocationHook = nullptr;
    };

    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        invoke();
    }

    return hostCallAllocations;
#else
    static_cast<void>(invoke);
    static_cast<void>(iterations);

    return std::nullopt;
#endif
}

template<typename Invocation>
double measureHostCall(
    nomad::VirtualMachine& machine,
    const Invocation& invoke,
    const nomad::NomadIndex iterations
) {
    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
        invoke();
        if (machine.getIntegerResult() != 42) {
            throw std::runtime_error("Unexpected host-call benchmark result");
        }
    }

    auto checksum = std::uint64_t{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
        invoke();
        checksum += static_cast<std::uint64_t>(machine.getIntegerResult());
    }

    const auto elapsed = std::chrono::duration<double, std::micro>{
        std::chrono::steady_clock::now() - start
    }.count();
    if (checksum != std::uint64_t{42} * iterations) {
        throw std::runtime_error("Host-call results changed during measurement");
    }

    return elapsed / static_cast<double>(iterations);
}

void printSamples(const char* name, std::vector<double> samples) {
    std::ranges::sort(samples);
    std::cout << ' ' << name << '=' << samples[samples.size() / 2]
        << " [" << samples.front() << ',' << samples.back() << ']';
}

void runBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto firstLookupIterations = nomad::NomadIndex{25000};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 3>{100, 1000, 5000};
    constexpr auto labels = std::array{"first_us", "middle_us", "last_us", "missing_us"};
    const auto missingName = nomad::NomadString{"benchmark.lookup.absent"};
    std::cout << "Configuration: " << NOMAD_BENCHMARK_CONFIGURATION << '\n'
        << "Samples: " << sampleCount << "; values are median [min,max]\n"
        << "Registration excludes Runtime construction and prepared names/parameters.\n"
        << "Lookups are microseconds per call; registration is total milliseconds.\n"
        << std::flush;
    std::cout << std::fixed << std::setprecision(3);

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> names;
        names.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            names.push_back(std::format("benchmark.lookup.{:06}", index));
        }

        const auto lookupIterations = nomad::NomadIndex{2000000} / addedCount;
        std::array<std::vector<double>, 4> lookupSamples;
        std::vector<double> registrationSamples;
        auto initialCount = nomad::NomadIndex{0};
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            if (sample == 0) {
                initialCount = runtime.getCallableCount();
            } else if (runtime.getCallableCount() != initialCount) {
                throw std::runtime_error("The baseline callable catalog changed between samples");
            }

            const auto* valueType = runtime.getIntegerType();
            const auto parameters = std::vector{nomad::defParameter("value", valueType, "")};
            auto callbackCalls = nomad::NomadIndex{0};
            const nomad::NativeFunctionFn callback = [&](nomad::VirtualMachine*) { ++callbackCalls; };
            std::vector<nomad::CallableId> ids(addedCount);
            const auto registrationStart = std::chrono::steady_clock::now();
            for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
                ids[index] = runtime.registerNativeFunction(names[index], callback, parameters, valueType, "");
            }

            const auto registrationElapsed = std::chrono::duration<double, std::milli>{
                std::chrono::steady_clock::now() - registrationStart
            }.count();
            if (runtime.getCallableCount() != initialCount + addedCount) {
                throw std::runtime_error("Native benchmark registration did not publish every callable");
            }

            for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
                if (ids[index] != nomad::CallableId{nomad::toNomadId(initialCount + index)}) {
                    throw std::runtime_error("Native benchmark registration changed callable identity/order");
                }
            }

            const auto queryNames = std::array<const nomad::NomadString*, 4>{
                &names.front(), &names[addedCount / 2], &names.back(), &missingName
            };
            const auto expectedIds = std::array{
                ids.front(), ids[addedCount / 2], ids.back(), nomad::CallableId::INVALID
            };
            for (auto query = nomad::NomadIndex{0}; query < queryNames.size(); ++query) {
                lookupSamples[query].push_back(measureLookup(
                    runtime,
                    *queryNames[query],
                    expectedIds[query],
                    query == 0 ? firstLookupIterations : lookupIterations
                ));
            }

            if (callbackCalls != 0) {
                throw std::runtime_error("Benchmark registration or lookup unexpectedly invoked a callable");
            }

            registrationSamples.push_back(registrationElapsed);
        }

        std::cout << "added=" << addedCount << " total=" << initialCount + addedCount
            << " first_iterations=" << firstLookupIterations << " other_iterations=" << lookupIterations;
        printSamples("registration_ms", registrationSamples);
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            printSamples(labels[query], lookupSamples[query]);
        }

        std::cout << std::endl;
    }
}

void runOwnerResolutionBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 4>{0, 100, 1000, 5000};
    constexpr auto labels = std::array{
        "method_hit_us", "method_missing_us", "constructor_hit_us", "constructor_missing_us"
    };
    const auto methodName = nomad::NomadString{"benchmarkInspect"};
    std::cout << "Owner-scoped resolution: one method and two constructors; misses use an empty owner.\n"
        << "Only unrelated free-function count varies; complete resolution is microseconds per call.\n"
        << std::flush;

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> names;
        names.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            names.push_back(std::format("benchmark.owner.noise.{:06}", index));
        }

        const auto iterations = nomad::NomadIndex{2000000} / std::max(addedCount, nomad::NomadIndex{100});
        std::array<std::vector<double>, 4> samples;
        auto initialCount = nomad::NomadIndex{0};
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            if (sample == 0) {
                initialCount = runtime.getCallableCount();
            } else if (runtime.getCallableCount() != initialCount) {
                throw std::runtime_error("The owner-resolution baseline changed between samples");
            }

            const auto ownerId = runtime.registerClass("BenchmarkOwner", nomad::ClassMode::Closed);
            const auto emptyOwnerId = runtime.registerClass("BenchmarkEmptyOwner", nomad::ClassMode::Closed);
            if (ownerId.isInvalid() || emptyOwnerId.isInvalid()) {
                throw std::runtime_error("Failed to register benchmark owners");
            }

            const auto* owner = runtime.getType(ownerId).value();
            const auto* emptyOwner = runtime.getType(emptyOwnerId).value();
            const auto* valueType = runtime.getIntegerType();
            const auto parameters = std::vector{nomad::defParameter("value", valueType, "")};
            auto callbackCalls = nomad::NomadIndex{0};
            const nomad::NativeFunctionFn callback = [&](nomad::VirtualMachine*) { ++callbackCalls; };
            const auto methodId = runtime.registerNativeClassMethod(ownerId, {
                .name = methodName,
                .parameters = {{valueType->getId(), "value", ""}},
                .returnTypeId = valueType->getId(),
                .function = callback
            });
            const auto emptyConstructorId = runtime.registerNativeClassConstructor(ownerId, {
                .parameters = {},
                .function = callback
            });
            const auto valueConstructorId = runtime.registerNativeClassConstructor(ownerId, {
                .parameters = {{valueType->getId(), "value", ""}},
                .function = callback
            });
            if (methodId.isInvalid() || emptyConstructorId.isInvalid() || valueConstructorId.isInvalid()) {
                throw std::runtime_error("Failed to register benchmark owner members");
            }

            for (const auto& name : names) {
                if (runtime.registerNativeFunction(name, callback, parameters, valueType, "").isInvalid()) {
                    throw std::runtime_error("Failed to populate the unrelated callable catalog");
                }
            }

            if (runtime.getCallableCount() != initialCount + addedCount + 3) {
                throw std::runtime_error("Unexpected owner-resolution catalog size");
            }

            const auto argumentTypes = std::vector<const nomad::Type*>{valueType};
            const auto methodResult = nomad::ResolveResult{
                nomad::ResolveStatus::Resolved, methodId, {methodId}, argumentTypes
            };
            const auto constructorResult = nomad::ResolveResult{
                nomad::ResolveStatus::Resolved,
                valueConstructorId,
                {emptyConstructorId, valueConstructorId},
                argumentTypes
            };
            const auto missingResult = nomad::ResolveResult{
                nomad::ResolveStatus::UnknownName, nomad::CallableId::INVALID, {}, argumentTypes
            };
            samples[0].push_back(measureResolution(
                labels[0],
                [&] {

                    return runtime.resolveMethod(owner, methodName, argumentTypes);
                },
                methodResult,
                iterations
            ));
            samples[1].push_back(measureResolution(
                labels[1],
                [&] {

                    return runtime.resolveMethod(emptyOwner, methodName, argumentTypes);
                },
                missingResult,
                iterations
            ));
            samples[2].push_back(measureResolution(
                labels[2],
                [&] {

                    return runtime.resolveConstructor(owner, argumentTypes);
                },
                constructorResult,
                iterations
            ));
            samples[3].push_back(measureResolution(
                labels[3],
                [&] {

                    return runtime.resolveConstructor(emptyOwner, argumentTypes);
                },
                missingResult,
                iterations
            ));
            if (callbackCalls != 0) {
                throw std::runtime_error("Owner-resolution benchmark unexpectedly invoked a callable");
            }
        }

        std::cout << "unrelated=" << addedCount << " total=" << initialCount + addedCount + 3
            << " iterations=" << iterations;
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            printSamples(labels[query], samples[query]);
        }

        std::cout << std::endl;
    }
}

void runStringBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto firstLookupIterations = nomad::NomadIndex{25000};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 3>{100, 1000, 5000};
    constexpr auto labels = std::array{"first_us", "middle_us", "last_us", "missing_us"};
    const auto missingText = nomad::NomadString{"benchmark.string.absent"};
    const auto stringsBefore = nomad::NomadStringObject::getLiveCount();
    std::cout << "String interning: unique registration excludes Runtime/text preparation and an empty-text primer.\n"
        << "Registration is total milliseconds; getStringId and repeated-last registration are microseconds per call.\n"
        << std::flush;

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> texts;
        texts.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            texts.push_back(std::format("benchmark.string.{:06}", index));
        }

        const auto iterations = nomad::NomadIndex{2000000} / addedCount;
        std::vector<double> registrationSamples;
        std::vector<double> deduplicationSamples;
        std::array<std::vector<double>, 4> lookupSamples;
        auto initialCount = nomad::NomadIndex{0};
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            const auto emptyId = runtime.registerString("");
            if (emptyId.isInvalid()) {
                throw std::runtime_error("Failed to register the empty benchmark string");
            }

            const auto sampleInitialCount = nomad::toNomadIndex(emptyId.id) + 1;
            if (sample == 0) {
                initialCount = sampleInitialCount;
            } else if (sampleInitialCount != initialCount) {
                throw std::runtime_error("The initial string catalog changed between samples");
            }

            const auto* emptyObject = runtime.getString(emptyId);
            if (
                emptyObject == nullptr ||
                !emptyObject->getView().empty() ||
                runtime.getString(nomad::StringId{nomad::toNomadId(initialCount)}) != nullptr
            ) {
                throw std::runtime_error("The empty string is not the expected final primer entry");
            }

            const auto emptyReferences = emptyObject->getReferenceCount();
            std::vector<nomad::StringId> ids(addedCount);
            const auto registrationStart = std::chrono::steady_clock::now();
            for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
                ids[index] = runtime.registerString(texts[index]);
            }

            const auto registrationElapsed = std::chrono::duration<double, std::milli>{
                std::chrono::steady_clock::now() - registrationStart
            }.count();
            for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
                const auto expectedId = nomad::StringId{nomad::toNomadId(initialCount + index)};
                const auto* object = runtime.getString(ids[index]);
                if (
                    ids[index] != expectedId ||
                    object == nullptr ||
                    object->getView() != texts[index]
                ) {
                    throw std::runtime_error("String benchmark registration changed ID order or text");
                }
            }

            const auto* firstObject = runtime.getString(ids.front());
            const auto* lastObject = runtime.getString(ids.back());
            const auto firstReferences = firstObject->getReferenceCount();
            const auto lastReferences = lastObject->getReferenceCount();
            const auto queryTexts = std::array<const nomad::NomadString*, 4>{
                &texts.front(), &texts[addedCount / 2], &texts.back(), &missingText
            };
            const auto expectedIds = std::array{
                ids.front(), ids[addedCount / 2], ids.back(), nomad::StringId::INVALID
            };
            for (auto query = nomad::NomadIndex{0}; query < queryTexts.size(); ++query) {
                lookupSamples[query].push_back(measureStringLookup(
                    runtime,
                    *queryTexts[query],
                    expectedIds[query],
                    query == 0 ? firstLookupIterations : iterations
                ));
            }

            deduplicationSamples.push_back(measureStringDeduplication(
                runtime,
                texts.back(),
                ids.back(),
                iterations
            ));
            if (
                runtime.registerString("") != emptyId ||
                runtime.getString(emptyId) != emptyObject ||
                emptyObject->getReferenceCount() != emptyReferences ||
                runtime.getStringByName(texts.front()) != firstObject ||
                runtime.getStringByName(texts.back()) != lastObject ||
                firstObject->getReferenceCount() != firstReferences ||
                lastObject->getReferenceCount() != lastReferences ||
                runtime.getStringByName(missingText) != nullptr ||
                runtime.getString(nomad::StringId{nomad::toNomadId(initialCount + addedCount)}) != nullptr
            ) {
                throw std::runtime_error("String benchmark changed interning identity, references, or catalog bounds");
            }

            registrationSamples.push_back(registrationElapsed);
        }

        std::cout << "strings=" << addedCount << " total=" << initialCount + addedCount
            << " first_iterations=" << firstLookupIterations << " other_iterations=" << iterations;
        printSamples("registration_ms", registrationSamples);
        printSamples("duplicate_last_us", deduplicationSamples);
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            printSamples(labels[query], lookupSamples[query]);
        }

        std::cout << std::endl;
    }

    if (nomad::NomadStringObject::getLiveCount() != stringsBefore) {
        throw std::runtime_error("String benchmark leaked interned string objects");
    }
}

void runCurrentCallableBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 4>{0, 100, 1000, 5000};
    constexpr auto labels = std::array{"caller_us", "nested_us", "restored_us"};
    const auto source = nomad::NomadString{
        "fun benchmarkActiveCallee\nbenchmarkCurrentProbe\nend\n"
        "fun benchmarkActiveCaller\nbenchmarkCurrentProbe\n"
        "benchmarkActiveCallee\nbenchmarkCurrentProbe\nend\n"
    };
    auto fixedCallableCount = nomad::NomadIndex{0};
    std::cout << "Active-callable lookup: native probes inside a compiled caller, nested callee, and restored caller.\n"
        << "Only unrelated native-function count varies; getCurrentCallable is microseconds per checked query.\n"
        << "Compilation, initialization, frame transitions, and probe entry are outside timing.\n"
        << std::flush;

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> names;
        names.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            names.push_back(std::format("benchmark.active.noise.{:06}", index));
        }

        const auto iterations = nomad::NomadIndex{2000000} / std::max(addedCount, nomad::NomadIndex{100});
        std::array<std::vector<double>, 3> samples;
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            std::array<const nomad::ScriptCallableBase*, 3> expectedCallables{};
            auto probeVisits = nomad::NomadIndex{0};
            nomad::Runtime runtime;
            const auto probeId = runtime.registerNativeFunction(
                "benchmarkCurrentProbe",
                [&](nomad::VirtualMachine* machine) {
                    if (
                        probeVisits >= expectedCallables.size() ||
                        expectedCallables[probeVisits] == nullptr
                    ) {
                        throw std::runtime_error("Unexpected active-callable benchmark probe");
                    }

                    samples[probeVisits].push_back(measureCurrentCallable(
                        *machine,
                        expectedCallables[probeVisits],
                        iterations
                    ));
                    ++probeVisits;
                },
                {},
                runtime.getVoidType(),
                ""
            );
            if (probeId.isInvalid()) {
                throw std::runtime_error("Failed to register the active-callable benchmark probe");
            }

            for (const auto& name : names) {
                if (
                    runtime.registerNativeFunction(
                        name,
                        [](nomad::VirtualMachine*) {},
                        {},
                        runtime.getVoidType(),
                        ""
                    ).isInvalid()
                ) {
                    throw std::runtime_error("Failed to populate the active-callable benchmark catalog");
                }
            }

            auto compiler = runtime.createCompiler();
            const auto compilation = nomad::compileSources(
                {{{"benchmarkActiveMain", {"benchmark-active.nomad", source}}}, {}},
                *compiler
            );
            if (!compilation.succeeded()) {
                auto message = nomad::NomadString{"Active-callable benchmark compilation failed:\n"};
                for (const auto& diagnostic : compilation.diagnostics) {
                    message += diagnostic.message + "\n";
                }

                throw std::runtime_error(message);
            }

            const auto callerId = runtime.getScriptFunctionId("benchmarkActiveCaller");
            const auto* caller = runtime.getScriptFunction(callerId);
            const auto* callee = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkActiveCallee"));
            if (
                caller == nullptr ||
                callee == nullptr ||
                !caller->isCompiled() ||
                !callee->isCompiled()
            ) {
                throw std::runtime_error("The active-callable benchmark functions are unavailable");
            }

            const auto currentFixedCount = runtime.getCallableCount() - addedCount;
            if (fixedCallableCount == 0) {
                fixedCallableCount = currentFixedCount;
            } else if (currentFixedCount != fixedCallableCount) {
                throw std::runtime_error("The compiled benchmark callable count changed between samples");
            }

            expectedCallables = {caller, callee, caller};
            runtime.initializeScripts();
            auto machine = runtime.createVirtualMachine();
            machine->executeFunction(callerId);
            if (probeVisits != expectedCallables.size()) {
                throw std::runtime_error("The benchmark did not visit every expected compiled frame");
            }
        }

        std::cout << "active_unrelated=" << addedCount << " total=" << fixedCallableCount + addedCount
            << " iterations=" << iterations;
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            printSamples(labels[query], samples[query]);
        }

        std::cout << std::endl;
    }
}

void runNestedScriptCallBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{50000};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 4>{0, 100, 1000, 5000};
    const auto source = nomad::NomadString{
        "fun benchmarkCallLeaf\nreturn 41\nend\n"
        "fun benchmarkCallCaller\nreturn benchmarkCallLeaf + 1\nend\n"
    };
    auto fixedCallableCount = nomad::NomadIndex{0};
    std::cout << "Script-call overhead: a prepared scalar caller invokes one compiled leaf; no callable queries are timed.\n"
        << "Microseconds per complete invocation include two script frames, execution, return, and result checksum.\n"
        << "Runtime setup, compilation, initialization, and state checks are outside timing.\n"
        << std::flush;

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> names;
        names.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            names.push_back(std::format("benchmark.calls.noise.{:06}", index));
        }

        std::vector<double> samples;
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            for (const auto& name : names) {
                if (
                    runtime.registerNativeFunction(
                        name,
                        [](nomad::VirtualMachine*) {},
                        {},
                        runtime.getVoidType(),
                        ""
                    ).isInvalid()
                ) {
                    throw std::runtime_error("Failed to populate the script-call benchmark catalog");
                }
            }

            auto compiler = runtime.createCompiler();
            const auto compilation = nomad::compileSources(
                {{{"benchmarkCallMain", {"benchmark-calls.nomad", source}}}, {}},
                *compiler
            );
            if (!compilation.succeeded()) {
                auto message = nomad::NomadString{"Script-call benchmark compilation failed:\n"};
                for (const auto& diagnostic : compilation.diagnostics) {
                    message += diagnostic.message + "\n";
                }

                throw std::runtime_error(message);
            }

            const auto* caller = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkCallCaller"));
            const auto* leaf = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkCallLeaf"));
            if (
                caller == nullptr ||
                leaf == nullptr ||
                !caller->isCompiled() ||
                !leaf->isCompiled() ||
                caller->getReturnType() != runtime.getIntegerType() ||
                leaf->getReturnType() != runtime.getIntegerType()
            ) {
                throw std::runtime_error("The nested script-call benchmark functions are unavailable");
            }

            const auto currentFixedCount = runtime.getCallableCount() - addedCount;
            if (fixedCallableCount == 0) {
                fixedCallableCount = currentFixedCount;
            } else if (currentFixedCount != fixedCallableCount) {
                throw std::runtime_error("The script-call benchmark catalog changed between samples");
            }

            runtime.initializeScripts();
            auto machine = runtime.createVirtualMachine();
            samples.push_back(measureNestedScriptCalls(*machine, *caller, iterations));
        }

        std::cout << "call_unrelated=" << addedCount << " total=" << fixedCallableCount + addedCount
            << " iterations=" << iterations;
        printSamples("nested_call_us", samples);
        std::cout << std::endl;
    }
}

void runNativePropertyBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{50000};
    constexpr auto cases = std::array{"small_capture", "large_capture", "long_metadata"};
    constexpr auto labels = std::array{"descriptor_us", "info_copy_us", "getter_us", "setter_us"};
    const auto objectsBefore = nomad::NomadObject::getLiveObjectCount();
    std::cout << "Native properties: borrowed descriptor lookup, copied metadata, and complete VM getter/setter access.\n"
        << "Cases vary callback capture size or metadata text length; scalar callback behavior is otherwise fixed.\n"
        << "Microseconds per checked operation include result checks; setup and ownership/state checks are outside timing.\n"
        << std::flush;

    for (const auto* caseName : cases) {
        const auto largeCapture = nomad::NomadStringView{caseName} == "large_capture";
        const auto longMetadata = nomad::NomadStringView{caseName} == "long_metadata";
        const auto name = longMetadata ? nomad::NomadString(128, 'n') : nomad::NomadString{"value"};
        const auto documentation = longMetadata ? nomad::NomadString(256, 'd') : nomad::NomadString{"value"};
        auto getterCaptureSize = nomad::NomadIndex{0};
        auto setterCaptureSize = nomad::NomadIndex{0};
        std::array<std::vector<double>, 4> samples;

        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            PropertyBenchmarkState state;
            nomad::Runtime runtime;
            const auto ownerId = runtime.registerClass("BenchmarkProperty", nomad::ClassMode::Closed);
            if (ownerId.isInvalid()) {
                throw std::runtime_error("Failed to register the native-property benchmark owner");
            }

            const auto* ownerType = runtime.getType(ownerId).value()->asClassType();
            const auto bias = static_cast<nomad::NomadInteger>(sample) + 3;
            state.value = 42 - bias;
            nomad::NativePropertyDefinition definition{
                .valueType = runtime.getIntegerType(),
                .name = name,
                .documentation = documentation
            };
            if (largeCapture) {
                std::array<nomad::NomadInteger, 32> payload;
                std::ranges::fill(payload, bias);
                auto getter = [&state, payload](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    state.receiver = machine->getThis();
                    ++state.reads;
                    result.setIntegerValue(state.value + payload.back());
                };
                auto setter = [&state, payload](nomad::VirtualMachine* machine, const nomad::RuntimeValue& value) {
                    state.receiver = machine->getThis();
                    ++state.writes;
                    state.value = value.getIntegerValue() - payload.back();
                };
                getterCaptureSize = sizeof(getter);
                setterCaptureSize = sizeof(setter);
                definition.getter = std::move(getter);
                definition.setter = std::move(setter);
            } else {
                auto getter = [&state, bias](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    state.receiver = machine->getThis();
                    ++state.reads;
                    result.setIntegerValue(state.value + bias);
                };
                auto setter = [&state, bias](nomad::VirtualMachine* machine, const nomad::RuntimeValue& value) {
                    state.receiver = machine->getThis();
                    ++state.writes;
                    state.value = value.getIntegerValue() - bias;
                };
                getterCaptureSize = sizeof(getter);
                setterCaptureSize = sizeof(setter);
                definition.getter = std::move(getter);
                definition.setter = std::move(setter);
            }

            const auto propertyId = runtime.registerNativeClassProperty(ownerId, std::move(definition));
            const auto* property = runtime.getNativeProperty(propertyId);
            if (
                propertyId.isInvalid() ||
                property == nullptr ||
                !property->isTypeResolved() ||
                property->getClassType() != ownerType ||
                property->getValueType() != runtime.getIntegerType() ||
                runtime.getNativeProperty(nomad::PropertyId::INVALID) != nullptr ||
                runtime.getNativePropertyInfo(nomad::PropertyId::INVALID).has_value()
            ) {
                throw std::runtime_error("Unexpected native-property benchmark registration or lookup bounds");
            }

            nomad::RuntimeValue receiver;
            const nomad::RuntimeValueGuard receiverOwner(*ownerType, &receiver);
            receiver.setObjectValue(new nomad::NomadObject);
            auto machine = runtime.createVirtualMachine();
            machine->setIntegerResult(73);
            machine->setIntegerIntermediate(79);
            const auto before = machine->captureExecutionState();
            samples[0].push_back(measurePropertyDescriptor(runtime, *property, iterations));
            samples[1].push_back(measurePropertyInfoCopy(runtime, *property, iterations));
            if (state.reads != 0 || state.writes != 0) {
                throw std::runtime_error("Native-property metadata lookup unexpectedly invoked an accessor");
            }

            samples[2].push_back(measurePropertyGetter(*machine, propertyId, receiver, iterations));
            samples[3].push_back(measurePropertySetter(*machine, propertyId, receiver, state, bias, iterations));
            const auto after = machine->captureExecutionState();
            if (
                state.reads != iterations + 32 ||
                state.writes != iterations + 32 ||
                state.receiver != receiver.getObjectValue() ||
                receiver.getObjectValue()->getReferenceCount() != 1 ||
                after.instructionIndex != before.instructionIndex ||
                after.stackIndex != before.stackIndex ||
                after.parameterIndex != before.parameterIndex ||
                after.variableIndex != before.variableIndex ||
                after.running != before.running ||
                after.receiver.getObjectValue() != before.receiver.getObjectValue() ||
                machine->getIntegerResult() != 73 ||
                machine->getIntegerIntermediate() != 79 ||
                machine->getCurrentCallable() != nullptr
            ) {
                throw std::runtime_error("Native-property access changed receiver ownership or enclosing VM state");
            }
        }

        std::cout << "property_case=" << caseName << " getter_capture_bytes=" << getterCaptureSize
            << " setter_capture_bytes=" << setterCaptureSize << " name_bytes=" << name.size()
            << " documentation_bytes=" << documentation.size() << " iterations=" << iterations;
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            printSamples(labels[query], samples[query]);
        }

        std::cout << std::endl;
    }

    if (nomad::NomadObject::getLiveObjectCount() != objectsBefore) {
        throw std::runtime_error("Native-property benchmark leaked receiver objects");
    }
}

void runHostCallBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{50000};
    constexpr auto allocationIterations = nomad::NomadIndex{1000};
    constexpr auto labels = std::array{
        "prepared_empty", "raw_empty", "raw_integer", "raw_four_integers",
        "raw_two_strings", "raw_capture_vector", "closure_empty", "closure_integer", "closure_captured",
        "raw_eight_integers", "closure_two_integers", "closure_eight_integers"
    };
    constexpr auto probeLabels = std::array{
        "integer_parameter_shape", "integer_call_layout", "integer_closure_frame", "raw_empty_explicit_captures"
    };
    const auto source = nomad::NomadString{
        "fun benchmarkHostEmpty\nreturn 42\nend\n"
        "fun benchmarkHostInteger value:int\nreturn value\nend\n"
        "fun benchmarkHostTwo first:int second:int\nreturn first + second\nend\n"
        "fun benchmarkHostFour first:int second:int third:int fourth:int\n"
        "return first + second + third + fourth\nend\n"
        "fun benchmarkHostEight first:int second:int third:int fourth:int fifth:int sixth:int seventh:int eighth:int\n"
        "return first + second + third + fourth + fifth + sixth + seventh + eighth\nend\n"
        "fun benchmarkHostStrings first:string second:string\nreturn first.length + second.length\nend\n"
        "fun benchmarkPrepareHostCapture\nprefix = \"captured\"\nbase = 13\n"
        "benchmarkKeepHostCapture fun value\nreturn value + base + prefix.length\nend\nend\n"
    };
    std::array<std::vector<double>, labels.size()> latencySamples;
    std::array<std::vector<double>, labels.size()> allocationSamples;
    std::array<std::vector<double>, probeLabels.size()> probeSamples;
    const auto objectsBefore = nomad::NomadObject::getLiveObjectCount();
    std::cout << "Host calls: prepared execution, raw arguments, and Closure-owned captures.\n"
        << "Latency includes execution and result checksum; compilation and argument/capture preparation are excluded.\n"
        << "Allocation counts use a separate untimed MSVC Debug CRT pass; other builds report unavailable.\n"
        << std::flush;
    std::cout << "host_bookkeeping vector_bytes=" << sizeof(std::vector<const nomad::Type*>)
        << " inline_four_bytes=" << sizeof(boost::container::small_vector<const nomad::Type*, 4>)
        << " capture_span_bytes=" << sizeof(std::span<const nomad::RuntimeValue>) << std::endl;
    const auto controlAllocations = countHostCallAllocations([] {
#if defined(_MSC_VER) && defined(_DEBUG)
        auto* control = _malloc_dbg(16, _NORMAL_BLOCK, __FILE__, __LINE__);
        if (control == nullptr) {
            throw std::runtime_error("Cannot allocate the host-call probe control");
        }

        _free_dbg(control, _NORMAL_BLOCK);
#endif
    }, 1);
    if (controlAllocations.has_value() && *controlAllocations != 1) {
        throw std::runtime_error("The host-call allocation probe did not observe its single-allocation control");
    }

    for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
        nomad::Runtime runtime;
        std::unique_ptr<nomad::Closure> captured;
        const auto storeId = runtime.registerNativeFunction(
            "benchmarkKeepHostCapture",
            [&](nomad::VirtualMachine* machine) {
                captured = machine->createClosure(&runtime, machine->getCallableIdParameter(0));
            },
            {nomad::defParameter(
                "callback",
                runtime.getTypeManager().getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType()),
                ""
            )},
            runtime.getVoidType(),
            ""
        );
        if (storeId.isInvalid()) {
            throw std::runtime_error("Failed to register the host-call benchmark capture store");
        }

        auto compiler = runtime.createCompiler();
        const auto compilation = nomad::compileSources(
            {{{"benchmarkHostMain", {"benchmark-host.nomad", source}}}, {}},
            *compiler
        );
        if (!compilation.succeeded()) {
            auto message = nomad::NomadString{"Host-call benchmark compilation failed:\n"};
            for (const auto& diagnostic : compilation.diagnostics) {
                message += diagnostic.message + "\n";
            }

            throw std::runtime_error(message);
        }

        const auto* empty = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostEmpty"));
        const auto* integer = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostInteger"));
        const auto* two = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostTwo"));
        const auto* four = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostFour"));
        const auto* eight = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostEight"));
        const auto* strings = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostStrings"));
        for (const auto* function : {empty, integer, two, four, eight, strings}) {
            if (
                function == nullptr ||
                !function->isCompiled() ||
                function->getReturnType() != runtime.getIntegerType()
            ) {
                throw std::runtime_error("A host-call benchmark function is unavailable");
            }
        }

        runtime.initializeScripts();
        auto machine = runtime.createVirtualMachine();
        machine->executeFunction(runtime.getScriptFunctionId("benchmarkPrepareHostCapture"));
        if (captured == nullptr || captured->getCaptureCount() != 2) {
            throw std::runtime_error("The host-call benchmark did not capture its scalar and string values");
        }

        std::vector<nomad::RuntimeValue> rawCaptures;
        const nomad::NomadStringObject* capturedString = nullptr;
        for (auto index = nomad::NomadIndex{0}; index < captured->getCaptureCount(); ++index) {
            const auto* type = captured->getCaptureType(index);
            if (type != runtime.getIntegerType() && type != runtime.getStringType()) {
                throw std::runtime_error("Unexpected host-call benchmark capture type");
            }

            rawCaptures.push_back(captured->getCaptureValue(index));
            if (type == runtime.getStringType()) {
                capturedString = rawCaptures.back().getStringObject();
            }
        }

        if (capturedString == nullptr) {
            throw std::runtime_error("The host-call benchmark reference capture is missing");
        }

        const auto captureReferences = capturedString->getReferenceCount();
        nomad::RuntimeValue firstText{nomad::NomadString(19, 'a')};
        const nomad::RuntimeValueGuard firstOwner(*runtime.getStringType(), &firstText);
        nomad::RuntimeValue secondText{nomad::NomadString(23, 'b')};
        const nomad::RuntimeValueGuard secondOwner(*runtime.getStringType(), &secondText);
        nomad::RuntimeValue receiver{nomad::NomadString{"borrowed host-call receiver"}};
        const nomad::RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
        const std::vector<nomad::RuntimeValue> noArguments;
        const std::vector integerArguments{nomad::RuntimeValue{nomad::NomadInteger{42}}};
        const std::vector twoArguments{
            nomad::RuntimeValue{nomad::NomadInteger{17}},
            nomad::RuntimeValue{nomad::NomadInteger{25}}
        };
        const std::vector fourArguments{
            nomad::RuntimeValue{nomad::NomadInteger{3}},
            nomad::RuntimeValue{nomad::NomadInteger{7}},
            nomad::RuntimeValue{nomad::NomadInteger{13}},
            nomad::RuntimeValue{nomad::NomadInteger{19}}
        };
        auto eightArguments = fourArguments;
        eightArguments.resize(8);
        const std::vector textArguments{firstText, secondText};
        const std::vector captureArguments{nomad::RuntimeValue{nomad::NomadInteger{21}}};
        const nomad::Closure emptyClosure(empty);
        const nomad::Closure integerClosure(integer);
        const nomad::Closure twoClosure(two);
        const nomad::Closure eightClosure(eight);
        machine->pushInteger(91);
        machine->enterNativeFrame(receiver);
        const auto before = machine->captureExecutionState();
        const auto recordProbe = [&](const nomad::NomadIndex query, const auto& invoke) {
            const auto allocations = countHostCallAllocations(invoke, allocationIterations);
            if (allocations.has_value()) {
                probeSamples[query].push_back(
                    static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                );
            }
        };
        recordProbe(0, [&] {
            if (runtime.getParameterShape(integer->getParameters()[0].type) != nomad::ParameterShape::Value) {
                throw std::runtime_error("Unexpected parameter shape in the host-call allocation probe");
            }
        });
        recordProbe(1, [&] {
            const auto layout = integer->getCallStorageLayout();
            if (layout.argumentSlotCount != 1 || layout.stackSlotCount != 1) {
                throw std::runtime_error("Unexpected call layout in the host-call allocation probe");
            }
        });
        recordProbe(2, [&] {
            const nomad::InterpreterCallFrame frame(*machine, integerClosure, integerArguments);
        });
        recordProbe(3, [&] { machine->run(empty, noArguments, noArguments); });
        const auto record = [&](const nomad::NomadIndex query, const auto& invoke) {
            latencySamples[query].push_back(measureHostCall(*machine, invoke, iterations));
            auto allocationChecksum = std::uint64_t{0};
            const auto allocations = countHostCallAllocations([&] {
                invoke();
                allocationChecksum += static_cast<std::uint64_t>(machine->getIntegerResult());
            }, allocationIterations);
            if (allocations.has_value()) {
                if (allocationChecksum != std::uint64_t{42} * allocationIterations) {
                    throw std::runtime_error("The allocation-probe host calls returned unexpected results");
                }

                allocationSamples[query].push_back(
                    static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                );
            }

            const auto after = machine->captureExecutionState();
            if (
                after.instructionIndex != before.instructionIndex ||
                after.stackIndex != before.stackIndex ||
                after.parameterIndex != before.parameterIndex ||
                after.variableIndex != before.variableIndex ||
                after.running != before.running ||
                after.receiver.getObjectValue() != before.receiver.getObjectValue() ||
                machine->getCurrentCallable() != nullptr ||
                machine->peekStack(0).getIntegerValue() != 91 ||
                firstText.getObjectValue()->getReferenceCount() != 1 ||
                secondText.getObjectValue()->getReferenceCount() != 1 ||
                receiver.getObjectValue()->getReferenceCount() != 1 ||
                capturedString->getReferenceCount() != captureReferences
            ) {
                throw std::runtime_error("Host calls changed enclosing state, borrowed references, or capture ownership");
            }
        };
        record(0, [&] { machine->executeFunction(*empty); });
        record(1, [&] { machine->run(empty, noArguments); });
        record(2, [&] { machine->run(integer, integerArguments); });
        record(3, [&] { machine->run(four, fourArguments); });
        record(4, [&] { machine->run(strings, textArguments); });
        record(5, [&] { machine->run(captured->getFunction(), captureArguments, rawCaptures); });
        record(6, [&] { machine->run(emptyClosure, noArguments); });
        record(7, [&] { machine->run(integerClosure, integerArguments); });
        record(8, [&] { machine->run(*captured, captureArguments); });
        record(9, [&] { machine->run(eight, eightArguments); });
        record(10, [&] { machine->run(twoClosure, twoArguments); });
        record(11, [&] { machine->run(eightClosure, eightArguments); });
    }

    for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
        std::cout << "host_call=" << labels[query] << " iterations=" << iterations
            << " allocation_iterations=" << (controlAllocations.has_value() ? allocationIterations : 0);
        printSamples("latency_us", latencySamples[query]);
        if (!allocationSamples[query].empty()) {
            printSamples("allocations_per_call", allocationSamples[query]);
        } else {
            std::cout << " allocations_per_call=unavailable";
        }

        std::cout << std::endl;
    }

    for (auto query = nomad::NomadIndex{0}; query < probeLabels.size(); ++query) {
        std::cout << "host_probe=" << probeLabels[query]
            << " iterations=" << (controlAllocations.has_value() ? allocationIterations : 0);
        if (!probeSamples[query].empty()) {
            printSamples("allocations_per_operation", probeSamples[query]);
        } else {
            std::cout << " allocations_per_operation=unavailable";
        }

        std::cout << std::endl;
    }

    if (nomad::NomadObject::getLiveObjectCount() != objectsBefore) {
        throw std::runtime_error("Host-call benchmark leaked argument, capture, or receiver objects");
    }
}

void runFrameResultBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{50000};
    constexpr auto allocationIterations = nomad::NomadIndex{1000};
    constexpr auto labels = std::array{
        "void", "integer", "string", "empty_struct", "two_slot_struct", "eight_slot_struct"
    };
    std::array<std::vector<double>, labels.size()> latencySamples;
    std::array<std::vector<double>, labels.size()> allocationSamples;
    const auto objectsBefore = nomad::NomadObject::getLiveObjectCount();
    std::cout << "Frame results: preparation, cleared storage checks, writes, and destruction; no body or snapshot copies.\n"
        << "Setup is excluded; allocation requests use the separate untimed MSVC Debug pass.\n"
        << "frame_result_storage frame_bytes=" << sizeof(nomad::InterpreterCallFrame)
        << " vector_bytes=" << sizeof(std::vector<nomad::RuntimeValue>)
        << " array_owner_bytes=" << sizeof(std::unique_ptr<nomad::RuntimeValue[]>) << std::endl;

    for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
        nomad::Runtime runtime;
        std::array<const nomad::Type*, labels.size()> types{
            runtime.getVoidType(), runtime.getIntegerType(), runtime.getStringType()
        };
        for (auto query = nomad::NomadIndex{3}; query < labels.size(); ++query) {
            const auto typeId = runtime.registerStruct(std::format("BenchmarkResult{}", query));
            if (typeId.isInvalid()) {
                throw std::runtime_error("Failed to register a frame-result benchmark type");
            }

            const auto fieldCount = query == 3 ? nomad::NomadIndex{0} :
                query == 4 ? nomad::NomadIndex{2} : nomad::NomadIndex{8};
            for (auto field = nomad::NomadIndex{0}; field < fieldCount; ++field) {
                if (
                    runtime.getTypeManager().registerScriptStructProperty(
                        typeId,
                        std::format("field{}", field),
                        runtime.getIntegerType()
                    ).isInvalid()
                ) {
                    throw std::runtime_error("Failed to register a frame-result benchmark field");
                }
            }

            runtime.getTypeManager().finalizeCompositeFieldLayout(typeId);
            types[query] = runtime.getType(typeId).value();
        }

        runtime.initializeScripts();
        auto machine = runtime.createVirtualMachine();
        machine->pushInteger(91);
        machine->setIntegerResult(42);
        machine->setIntegerIntermediate(79);
        const auto before = machine->captureExecutionState();
        for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
            const auto* type = types[query];
            const auto callableId = runtime.registerNativeFunction(
                std::format("benchmarkFrameResult{}", query),
                [](nomad::VirtualMachine*) {},
                {},
                type,
                ""
            );
            const auto* callable = runtime.getCallable(callableId);
            if (callable == nullptr) {
                throw std::runtime_error("Failed to register a frame-result benchmark callable");
            }

            const auto size = type->getSize();
            const auto structResult = type->isStruct();
            const auto referenceResult = type == runtime.getStringType();
            const auto invoke = [&] {
                const nomad::InterpreterCallFrame frame(*machine, *callable, {}, type);
                auto* storage = frame.getResult();
                if (
                    frame.getResultSize() != size ||
                    (storage == nullptr) != (size == 0) ||
                    (size != 0 && !structResult && storage != &machine->getResult()) ||
                    (size != 0 && structResult &&
                        machine->getStackAddress(0)->getStorageAddress() != storage)
                ) {
                    throw std::runtime_error("Unexpected frame-result storage shape or hidden pointer");
                }

                if (referenceResult) {
                    if (storage->getObjectValue() != nullptr) {
                        throw std::runtime_error("Frame-result reference storage is not cleared");
                    }
                } else {
                    for (auto index = nomad::NomadIndex{0}; index < size; ++index) {
                        if (storage[index].getIntegerValue() != 0) {
                            throw std::runtime_error("Frame-result value storage is not cleared");
                        }

                        storage[index].setIntegerValue(static_cast<nomad::NomadInteger>(index) + 1);
                    }
                }

                if (frame.getResult() != storage) {
                    throw std::runtime_error("Frame-result storage moved after publication");
                }
            };
            latencySamples[query].push_back(measureHostCall(*machine, invoke, iterations));
            const auto allocations = countHostCallAllocations(invoke, allocationIterations);
            if (allocations.has_value()) {
                allocationSamples[query].push_back(
                    static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                );
            }

            const auto after = machine->captureExecutionState();
            if (
                after.instructionIndex != before.instructionIndex ||
                after.stackIndex != before.stackIndex ||
                after.parameterIndex != before.parameterIndex ||
                after.variableIndex != before.variableIndex ||
                after.running != before.running ||
                after.receiver.getObjectValue() != before.receiver.getObjectValue() ||
                machine->getIntegerResult() != 42 ||
                machine->getIntegerIntermediate() != 79 ||
                machine->peekStack(0).getIntegerValue() != 91
            ) {
                throw std::runtime_error("Frame-result preparation changed enclosing VM state or registers");
            }
        }
    }

    for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
        std::cout << "frame_result=" << labels[query] << " iterations=" << iterations;
        printSamples("latency_us", latencySamples[query]);
        if (!allocationSamples[query].empty()) {
            printSamples("allocations_per_frame", allocationSamples[query]);
        } else {
            std::cout << " allocations_per_frame=unavailable";
        }

        std::cout << std::endl;
    }

    if (nomad::NomadObject::getLiveObjectCount() != objectsBefore) {
        throw std::runtime_error("Frame-result benchmark leaked objects");
    }
}

void runAssignmentScratchBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{50000};
    constexpr auto allocationIterations = nomad::NomadIndex{1000};
    constexpr auto cases = std::array{"empty", "integer", "string", "pair", "wide"};
    constexpr auto labels = std::array{"construction", "prepared", "setter_id", "setter_path"};
    const auto objectsBefore = nomad::NomadObject::getLiveObjectCount();
    std::cout << "Assignment scratch: construction, prepared Type assignment, and public ID/nested-path setters.\n"
        << "Values alternate; timing includes operation, value/bounds checks, and saved-result checksum.\n"
        << "Receiver/value/scratch preparation and reference/state checks are outside timing.\n"
        << "Allocation requests use the separate untimed MSVC Debug pass.\n"
        << std::flush;

    for (auto query = nomad::NomadIndex{0}; query < cases.size(); ++query) {
        std::array<std::vector<double>, labels.size()> latencySamples;
        std::array<std::vector<double>, labels.size()> allocationSamples;
        auto valueSize = nomad::NomadIndex{0};
        auto scratchSize = nomad::NomadIndex{0};
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            auto& manager = runtime.getTypeManager();
            const nomad::Type* type = nullptr;
            if (query == 1) {
                type = runtime.getIntegerType();
            } else if (query == 2) {
                type = runtime.getStringType();
            } else {
                const auto id = runtime.registerStruct("BenchmarkAssignmentValue");
                if (id.isInvalid()) {
                    throw std::runtime_error("Failed to register an assignment benchmark value type");
                }

                const auto count = query == 0 ? nomad::NomadIndex{0} :
                    query == 3 ? nomad::NomadIndex{2} : nomad::NomadIndex{8};
                for (auto index = nomad::NomadIndex{0}; index < count; ++index) {
                    const auto* fieldType = index + 1 == count ?
                        runtime.getStringType() : runtime.getIntegerType();
                    if (
                        manager.registerScriptStructProperty(
                            id,
                            std::format("field{}", index),
                            fieldType
                        ).isInvalid()
                    ) {
                        throw std::runtime_error("Failed to register an assignment benchmark value field");
                    }
                }

                manager.finalizeCompositeFieldLayout(id);
                type = runtime.getType(id).value();
            }

            valueSize = type->getSize();
            scratchSize = type->getAssignmentScratchSize();
            const auto referenceValue = query >= 2;
            const auto referenceOffset = valueSize == 0 ? nomad::NomadIndex{0} : valueSize - 1;
            std::vector<nomad::RuntimeValue> firstSource(valueSize);
            std::vector<nomad::RuntimeValue> secondSource(valueSize);
            const nomad::RuntimeValueGuard firstOwner(
                *type,
                firstSource.empty() ? nullptr : firstSource.data()
            );
            const nomad::RuntimeValueGuard secondOwner(
                *type,
                secondSource.empty() ? nullptr : secondSource.data()
            );
            for (auto index = nomad::NomadIndex{0}; index < valueSize; ++index) {
                if (referenceValue && index == referenceOffset) {
                    firstSource[index].setStringValue("assignment first");
                    secondSource[index].setStringValue("assignment second");
                } else {
                    firstSource[index].setIntegerValue(static_cast<nomad::NomadInteger>(index) + 17);
                    secondSource[index].setIntegerValue(static_cast<nomad::NomadInteger>(index) + 29);
                }
            }

            const nomad::InterpreterValue first(type, firstSource);
            const nomad::InterpreterValue second(type, secondSource);
            const auto firstReferences = referenceValue ?
                firstSource[referenceOffset].getObjectValue()->getReferenceCount() : nomad::NomadIndex{0};
            const auto secondReferences = referenceValue ?
                secondSource[referenceOffset].getObjectValue()->getReferenceCount() : nomad::NomadIndex{0};
            const auto ownerId = runtime.registerStruct("BenchmarkAssignmentOwner");
            const auto outerId = runtime.registerStruct("BenchmarkAssignmentOuter");
            if (ownerId.isInvalid() || outerId.isInvalid()) {
                throw std::runtime_error("Failed to register assignment benchmark receivers");
            }

            const auto beforeId = manager.registerScriptStructProperty(
                ownerId,
                "before",
                runtime.getIntegerType()
            );
            const auto valueId = manager.registerScriptStructProperty(ownerId, "value", type);
            const auto afterId = manager.registerScriptStructProperty(
                ownerId,
                "after",
                runtime.getIntegerType()
            );
            manager.finalizeCompositeFieldLayout(ownerId);
            const auto* ownerType = runtime.getType(ownerId).value()->asStructType();
            const auto outerBeforeId = manager.registerScriptStructProperty(
                outerId,
                "before",
                runtime.getIntegerType()
            );
            const auto innerId = manager.registerScriptStructProperty(outerId, "inner", ownerType);
            const auto outerAfterId = manager.registerScriptStructProperty(
                outerId,
                "after",
                runtime.getIntegerType()
            );
            if (
                beforeId.isInvalid() || valueId.isInvalid() || afterId.isInvalid() ||
                outerBeforeId.isInvalid() || innerId.isInvalid() || outerAfterId.isInvalid()
            ) {
                throw std::runtime_error("Failed to register assignment benchmark receiver fields");
            }

            manager.finalizeCompositeFieldLayout(outerId);
            const auto* outerType = runtime.getType(outerId).value()->asStructType();
            std::vector<nomad::RuntimeValue> outerStorage(outerType->getSize());
            const nomad::RuntimeValueGuard outerOwner(*outerType, outerStorage.data());
            outerType->initValue(outerStorage.data());
            const auto innerOffset = outerType->getFieldOffset(innerId);
            const auto ownerStorage = std::span{outerStorage}.subspan(innerOffset, ownerType->getSize());
            const auto valueOffset = ownerType->getFieldOffset(valueId);
            auto* destination = valueSize == 0 ? nullptr : ownerStorage.data() + valueOffset;
            const auto beforeOffset = ownerType->getFieldOffset(beforeId);
            const auto afterOffset = ownerType->getFieldOffset(afterId);
            const auto outerBeforeOffset = outerType->getFieldOffset(outerBeforeId);
            const auto outerAfterOffset = outerType->getFieldOffset(outerAfterId);
            ownerStorage[beforeOffset].setIntegerValue(107);
            ownerStorage[afterOffset].setIntegerValue(109);
            outerStorage[outerBeforeOffset].setIntegerValue(101);
            outerStorage[outerAfterOffset].setIntegerValue(103);
            std::vector<nomad::RuntimeValue> preparedScratch(scratchSize);
            std::optional<nomad::RuntimeValueGuard> scratchOwner;
            if (scratchSize != 0) {
                scratchOwner.emplace(*type, preparedScratch.data());
            }

            nomad::RuntimeValue receiver{nomad::NomadString{"borrowed assignment receiver"}};
            const nomad::RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
            runtime.initializeScripts();
            auto machine = runtime.createVirtualMachine();
            machine->pushInteger(91);
            machine->enterNativeFrame(receiver);
            machine->setIntegerResult(42);
            machine->setIntegerIntermediate(79);
            const nomad::Interpreter interpreter(*machine);
            const auto before = machine->captureExecutionState();
            const auto guardsMatch = [&] {

                return ownerStorage[beforeOffset].getIntegerValue() == 107 &&
                    ownerStorage[afterOffset].getIntegerValue() == 109 &&
                    outerStorage[outerBeforeOffset].getIntegerValue() == 101 &&
                    outerStorage[outerAfterOffset].getIntegerValue() == 103;
            };
            const auto matches = [&](const nomad::InterpreterValue& expected) {
                const auto storage = expected.getStorage();
                for (auto index = nomad::NomadIndex{0}; index < valueSize; ++index) {
                    if (referenceValue && index == referenceOffset) {
                        if (destination[index].getObjectValue() != storage[index].getObjectValue()) {

                            return false;
                        }
                    } else if (destination[index].getIntegerValue() != storage[index].getIntegerValue()) {

                        return false;
                    }
                }

                return guardsMatch();
            };
            const auto constructScratch = [&] {
                const std::vector<nomad::RuntimeValue> scratch(scratchSize);
                if (scratch.size() != scratchSize || !guardsMatch()) {
                    throw std::runtime_error("Scratch construction changed receiver bounds or storage size");
                }
            };
            latencySamples[0].push_back(measureHostCall(*machine, constructScratch, iterations));
            if (const auto allocations = countHostCallAllocations(constructScratch, allocationIterations)) {
                allocationSamples[0].push_back(
                    static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                );
            }

            const auto recordAssignment = [&](const nomad::NomadIndex operation, const auto& assign) {
                auto visits = nomad::NomadIndex{0};
                const auto invoke = [&] {
                    const auto& value = visits++ % 2 == 0 ? first : second;
                    assign(value);
                    if (!matches(value)) {
                        throw std::runtime_error("Field assignment changed values or receiver bounds");
                    }
                };
                latencySamples[operation].push_back(measureHostCall(*machine, invoke, iterations));
                const auto allocations = countHostCallAllocations(invoke, allocationIterations);
                if (allocations.has_value()) {
                    allocationSamples[operation].push_back(
                        static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                    );
                }

                const auto expectedVisits = iterations + 32 +
                    (allocations.has_value() ? allocationIterations : nomad::NomadIndex{0});
                if (visits != expectedVisits || !matches(second)) {
                    throw std::runtime_error("Assignment benchmark did not complete every expected write");
                }
            };
            recordAssignment(1, [&](const nomad::InterpreterValue& value) {
                type->assignValue(
                    value.getStorage().data(),
                    destination,
                    preparedScratch.empty() ? nullptr : preparedScratch.data()
                );
            });
            recordAssignment(2, [&](const nomad::InterpreterValue& value) {
                interpreter.setProperty(*ownerType, ownerStorage, valueId, value);
            });
            recordAssignment(3, [&](const nomad::InterpreterValue& value) {
                interpreter.setProperty(*outerType, outerStorage, "inner.value", value);
            });

            type->assignValue(
                destination,
                destination,
                preparedScratch.empty() ? nullptr : preparedScratch.data()
            );
            if (!matches(second)) {
                throw std::runtime_error("Aliased Type assignment changed the stored value");
            }

            {
                const nomad::InterpreterValue snapshot(
                    type,
                    std::span<const nomad::RuntimeValue>{destination, valueSize}
                );
                interpreter.setProperty(*ownerType, ownerStorage, valueId, snapshot);
                if (!matches(snapshot)) {
                    throw std::runtime_error("Public self-assignment changed stored values or bounds");
                }
            }

            for (const auto& slot : preparedScratch) {
                if (slot.getIntegerValue() != 0) {
                    throw std::runtime_error("Prepared assignment scratch retained an owned value");
                }
            }

            const auto after = machine->captureExecutionState();
            if (
                after.instructionIndex != before.instructionIndex ||
                after.stackIndex != before.stackIndex ||
                after.parameterIndex != before.parameterIndex ||
                after.variableIndex != before.variableIndex ||
                after.running != before.running ||
                after.receiver.getObjectValue() != before.receiver.getObjectValue() ||
                machine->getIntegerResult() != 42 || machine->getIntegerIntermediate() != 79 ||
                machine->peekStack(0).getIntegerValue() != 91 ||
                receiver.getObjectValue()->getReferenceCount() != 1 ||
                (referenceValue &&
                    (firstSource[referenceOffset].getObjectValue()->getReferenceCount() != firstReferences ||
                     secondSource[referenceOffset].getObjectValue()->getReferenceCount() != secondReferences + 1))
            ) {
                throw std::runtime_error("Assignment benchmark changed reference ownership or enclosing VM state");
            }
        }

        std::cout << "assignment_case=" << cases[query] << " value_slots=" << valueSize
            << " scratch_slots=" << scratchSize << " iterations=" << iterations;
        for (auto operation = nomad::NomadIndex{0}; operation < labels.size(); ++operation) {
            const auto timeLabel = std::format("{}_us", labels[operation]);
            printSamples(timeLabel.c_str(), latencySamples[operation]);
            const auto allocationLabel = std::format("{}_allocations", labels[operation]);
            if (!allocationSamples[operation].empty()) {
                printSamples(allocationLabel.c_str(), allocationSamples[operation]);
            } else {
                std::cout << ' ' << allocationLabel << "=unavailable";
            }
        }

        std::cout << std::endl;
    }

    if (nomad::NomadObject::getLiveObjectCount() != objectsBefore) {
        throw std::runtime_error("Assignment scratch benchmark leaked owned values or receivers");
    }
}

bool eventDefinitionsMatch(
    const nomad::EventDefinition& actual,
    const nomad::EventDefinition& expected
) {

    return actual.id == expected.id && actual.name == expected.name && actual.doc == expected.doc &&
        std::ranges::equal(actual.parameters, expected.parameters, [](const auto& first, const auto& second) {

            return first.name == second.name && first.typeId == second.typeId && first.typeName == second.typeName;
        });
}

std::uint64_t eventDefinitionChecksum(const nomad::EventDefinition& definition) {
    auto checksum = static_cast<std::uint64_t>(definition.id.id + 1)
        + definition.name.size() + definition.doc.size() + definition.parameters.size();
    for (const auto& parameter : definition.parameters) {
        checksum += static_cast<std::uint64_t>(parameter.typeId.id + 1)
            + parameter.name.size() + parameter.typeName.size();
    }

    return checksum;
}

void runEventSnapshotBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto iterations = nomad::NomadIndex{20000};
    constexpr auto allocationIterations = nomad::NomadIndex{1000};
    constexpr auto cases = std::array{"empty", "one_short", "eight_short", "eight_long"};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 3>{0, 100, 1000};
    constexpr auto labels = std::array{"snapshot", "prepared_copy", "presence", "missing", "borrowed"};
    std::cout << "Event definitions: late-ID explicit snapshot, prepared copy, presence, missing ID, and borrowed metadata.\n"
        << "Name/type text and parameter count vary independently of unrelated event registrations.\n"
        << "Timing includes the query/copy and checksum; setup, full metadata checks, and growth checks are excluded.\n"
        << "Allocation requests use the separate untimed MSVC Debug pass.\n"
        << std::flush;

    for (auto shape = nomad::NomadIndex{0}; shape < cases.size(); ++shape) {
        const auto longText = shape == 3;
        const auto parameterCount = shape == 0 ? nomad::NomadIndex{0} :
            shape == 1 ? nomad::NomadIndex{1} : nomad::NomadIndex{8};
        const auto name = longText ? nomad::NomadString(128, 'e') : nomad::NomadString{"snapshotEvent"};
        const auto parameterPrefix = longText ? nomad::NomadString(63, 'p') : nomad::NomadString{"value"};
        for (const auto unrelatedCount : catalogSizes) {
            std::vector<nomad::NomadString> unrelatedNames;
            unrelatedNames.reserve(unrelatedCount);
            for (auto index = nomad::NomadIndex{0}; index < unrelatedCount; ++index) {
                unrelatedNames.push_back(std::format("benchmarkEventNoise{:04}", index));
            }

            std::array<std::vector<double>, labels.size()> latencySamples;
            std::array<std::vector<double>, labels.size()> allocationSamples;
            auto parameterNameBytes = nomad::NomadIndex{0};
            auto typeNameBytes = nomad::NomadIndex{0};
            for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
                nomad::Runtime runtime;
                if (runtime.getFirstEventId().isValid()) {
                    throw std::runtime_error("The fresh-runtime event catalog changed before benchmarking");
                }

                const nomad::Type* parameterType = runtime.getIntegerType();
                if (longText) {
                    const auto typeId = runtime.registerClass(nomad::NomadString(96, 'T'), nomad::ClassMode::Closed);
                    if (typeId.isInvalid()) {
                        throw std::runtime_error("Failed to register the event benchmark parameter type");
                    }

                    parameterType = runtime.getType(typeId).value();
                }

                std::vector<nomad::EventParameter> parameters;
                for (auto index = nomad::NomadIndex{0}; index < parameterCount; ++index) {
                    parameters.push_back({
                        parameterPrefix + std::to_string(index),
                        parameterType->getId(),
                        parameterType->getTypeName()
                    });
                }

                parameterNameBytes = parameters.empty() ? 0 : parameters.front().name.size();
                typeNameBytes = parameters.empty() ? 0 : parameters.front().typeName.size();
                for (const auto& unrelatedName : unrelatedNames) {
                    if (runtime.registerEvent(unrelatedName, {}).isInvalid()) {
                        throw std::runtime_error("Failed to populate the event snapshot benchmark catalog");
                    }
                }

                const auto eventId = runtime.registerEvent(name, parameters);
                if (eventId.isInvalid() || eventId.id != nomad::toNomadId(unrelatedCount)) {
                    throw std::runtime_error("Unexpected event snapshot benchmark registration order");
                }

                const nomad::EventDefinition expected{eventId, name, parameters, {}};
                const auto* definitionBeforeGrowth = runtime.getEventDefinition(eventId);
                const auto missingId = nomad::EventId{nomad::toNomadId(unrelatedCount + 1)};
                if (
                    !definitionBeforeGrowth ||
                    !eventDefinitionsMatch(*definitionBeforeGrowth, expected) ||
                    runtime.getEventId(name) != eventId ||
                    runtime.getEventDefinition(nomad::EventId::INVALID) != nullptr ||
                    runtime.getEventDefinition(nomad::EventId{-2}) != nullptr ||
                    runtime.getEventDefinition(missingId) != nullptr ||
                    runtime.getNextEventId(eventId).isValid()
                ) {
                    throw std::runtime_error("Unexpected event snapshot metadata, iteration, or lookup bounds");
                }

                const auto snapshotBeforeGrowth = *definitionBeforeGrowth;
                auto detached = *definitionBeforeGrowth;
                detached.id = nomad::EventId::INVALID;
                detached.name = "detachedSnapshot";
                detached.doc = "detached documentation";
                if (!detached.parameters.empty()) {
                    detached.parameters.front().name = "detachedParameter";
                    detached.parameters.front().typeId = nomad::TypeId::INVALID;
                    detached.parameters.front().typeName = "detachedType";
                }

                if (!eventDefinitionsMatch(*definitionBeforeGrowth, expected)) {
                    throw std::runtime_error("Changing a detached event snapshot modified the registered event");
                }

                const auto record = [&](
                    const nomad::NomadIndex query,
                    const std::uint64_t expectedValue,
                    const auto& read
                ) {
                    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
                        if (read() != expectedValue) {
                            throw std::runtime_error("Unexpected event snapshot benchmark warmup result");
                        }
                    }

                    auto checksum = std::uint64_t{0};
                    const auto start = std::chrono::steady_clock::now();
                    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
                        checksum += read();
                    }

                    latencySamples[query].push_back(std::chrono::duration<double, std::micro>{
                        std::chrono::steady_clock::now() - start
                    }.count() / static_cast<double>(iterations));
                    if (checksum != expectedValue * iterations) {
                        throw std::runtime_error("Event snapshot benchmark results changed during timing");
                    }

                    auto allocationChecksum = std::uint64_t{0};
                    const auto allocations = countHostCallAllocations([&] {
                        allocationChecksum += read();
                    }, allocationIterations);
                    if (allocations.has_value()) {
                        if (allocationChecksum != expectedValue * allocationIterations) {
                            throw std::runtime_error("Event snapshot allocation probe returned unexpected metadata");
                        }

                        allocationSamples[query].push_back(
                            static_cast<double>(*allocations) / static_cast<double>(allocationIterations)
                        );
                    }
                };
                const auto expectedChecksum = eventDefinitionChecksum(expected);
                record(0, expectedChecksum, [&] {
                    const auto* definition = runtime.getEventDefinition(eventId);
                    if (!definition) {
                        throw std::runtime_error("The benchmark event snapshot disappeared");
                    }

                    const auto snapshot = *definition;

                    return eventDefinitionChecksum(snapshot);
                });
                record(1, expectedChecksum, [&] {
                    const auto copy = expected;

                    return eventDefinitionChecksum(copy);
                });
                record(2, 1, [&] {

                    return static_cast<std::uint64_t>(runtime.getEventDefinition(eventId) != nullptr);
                });
                record(3, 0, [&] {

                    return static_cast<std::uint64_t>(runtime.getEventDefinition(missingId) != nullptr);
                });
                record(4, expectedChecksum, [&] {
                    const auto* definition = runtime.getEventDefinition(eventId);
                    if (!definition) {
                        throw std::runtime_error("The borrowed benchmark event disappeared");
                    }

                    return eventDefinitionChecksum(*definition);
                });
                for (auto index = nomad::NomadIndex{0}; index < 64; ++index) {
                    if (runtime.registerEvent(std::format("snapshotGrowth{}", index), {}).isInvalid()) {
                        throw std::runtime_error("Failed to grow the event catalog after snapshot measurements");
                    }
                }

                if (
                    runtime.getEventDefinition(eventId) != definitionBeforeGrowth ||
                    !eventDefinitionsMatch(snapshotBeforeGrowth, expected) ||
                    !eventDefinitionsMatch(*definitionBeforeGrowth, expected) ||
                    runtime.getEventId(name) != eventId
                ) {
                    throw std::runtime_error("Event metadata or detached snapshot changed after catalog growth");
                }
            }

            std::cout << "event_case=" << cases[shape] << " unrelated=" << unrelatedCount
                << " total=" << unrelatedCount + 1 << " parameters=" << parameterCount
                << " name_bytes=" << name.size() << " parameter_name_bytes=" << parameterNameBytes
                << " type_name_bytes=" << typeNameBytes << " documentation_bytes=0"
                << " iterations=" << iterations;
            for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
                const auto timeLabel = std::format("{}_us", labels[query]);
                printSamples(timeLabel.c_str(), latencySamples[query]);
                const auto allocationLabel = std::format("{}_allocations", labels[query]);
                if (!allocationSamples[query].empty()) {
                    printSamples(allocationLabel.c_str(), allocationSamples[query]);
                } else {
                    std::cout << ' ' << allocationLabel << "=unavailable";
                }
            }

            std::cout << std::endl;
        }
    }
}

void runDynamicVariableRegistrationBenchmark() {
    using Callbacks = std::pair<nomad::DynamicVariableSetFn, nomad::DynamicVariableGetFn>;
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto batchSizes = std::array<nomad::NomadIndex, 2>{100, 1000};
    constexpr auto cases = std::array{"small_capture", "large_capture", "long_metadata"};
    constexpr auto labels = std::array{"registration_lvalue", "registration_rvalue", "prepared_callback_copy"};
    std::cout << "Dynamic-variable registration: prebuilt lvalue/rvalue callbacks and a prepared callback-pair copy control.\n"
        << "Five fresh samples per batch; 32 separate warmup registrations; Runtime, inputs, and buffers excluded.\n"
        << "Registration includes catalog growth and hub publication; copied callback buffers are retained until validation.\n"
        << "Debug allocation requests use separate fresh-runtime passes; controls are not a subtractive cost breakdown.\n"
        << std::flush;

    for (const auto* caseName : cases) {
        const auto largeCapture = nomad::NomadStringView{caseName} == "large_capture";
        const auto longMetadata = nomad::NomadStringView{caseName} == "long_metadata";
        const auto prefix = longMetadata ? nomad::NomadString(118, 'v') : nomad::NomadString{"v"};
        const auto documentation = longMetadata ? nomad::NomadString(512, 'd') : nomad::NomadString{"benchmark"};
        std::array<nomad::NomadInteger, 128> payload;
        std::iota(payload.begin(), payload.end(), nomad::NomadInteger{1});
        const auto expectedBias = largeCapture ?
            std::accumulate(payload.begin(), payload.end(), nomad::NomadInteger{0}) : nomad::NomadInteger{0};
        auto setterCaptureBytes = nomad::NomadIndex{0};
        auto getterCaptureBytes = nomad::NomadIndex{0};
        const auto createCallbacks = [&](DynamicVariableBenchmarkState& state) -> Callbacks {
            if (largeCapture) {
                auto setter = [&state, payload](nomad::VirtualMachine* machine, const nomad::RuntimeValue& value) {
                    ++state.writes;
                    state.machine = machine;
                    state.value = value.getIntegerValue() -
                        std::accumulate(payload.begin(), payload.end(), nomad::NomadInteger{0});
                };
                auto getter = [&state, payload](nomad::VirtualMachine* machine, nomad::RuntimeValue& value) {
                    ++state.reads;
                    state.machine = machine;
                    value.setIntegerValue(state.value +
                        std::accumulate(payload.begin(), payload.end(), nomad::NomadInteger{0}));
                };
                setterCaptureBytes = sizeof(setter);
                getterCaptureBytes = sizeof(getter);

                return {std::move(setter), std::move(getter)};
            }

            auto setter = [&state](nomad::VirtualMachine* machine, const nomad::RuntimeValue& value) {
                ++state.writes;
                state.machine = machine;
                state.value = value.getIntegerValue();
            };
            auto getter = [&state](nomad::VirtualMachine* machine, nomad::RuntimeValue& value) {
                ++state.reads;
                state.machine = machine;
                value.setIntegerValue(state.value);
            };
            setterCaptureBytes = sizeof(setter);
            getterCaptureBytes = sizeof(getter);

            return {std::move(setter), std::move(getter)};
        };
        for (const auto batchSize : batchSizes) {
            std::vector<nomad::NomadString> names;
            names.reserve(batchSize);
            for (auto index = nomad::NomadIndex{0}; index < batchSize; ++index) {
                names.push_back(std::format("bench.{}{:04}", prefix, index));
            }

            std::array<std::vector<double>, labels.size()> latencySamples;
            std::array<std::vector<double>, labels.size()> allocationSamples;
            for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
                {
                    DynamicVariableBenchmarkState state;
                    nomad::Runtime warmup;
                    if (warmup.registerHub("bench", nomad::HubMode::Opened).isInvalid()) {
                        throw std::runtime_error("Failed to prepare dynamic-variable registration warmups");
                    }

                    const auto callbacks = createCallbacks(state);
                    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
                        const auto id = warmup.registerDynamicVariable(
                            names[index],
                            callbacks.first,
                            callbacks.second,
                            warmup.getIntegerType(),
                            documentation
                        );
                        if (id != nomad::DynamicVariableId{nomad::toNomadId(index)}) {
                            throw std::runtime_error("Unexpected dynamic-variable warmup registration");
                        }
                    }
                }

                for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
                    for (auto pass = 0; pass < 2; ++pass) {
                        const auto allocationPass = pass == 1;
                        DynamicVariableBenchmarkState state;
                        nomad::Runtime runtime;
                        const auto hubId = runtime.registerHub("bench", nomad::HubMode::Opened);
                        if (hubId.isInvalid() || runtime.getFirstDynamicVariableId().isValid()) {
                            throw std::runtime_error("Unexpected initial dynamic-variable benchmark catalog");
                        }

                        const auto* type = runtime.getIntegerType();
                        auto machine = runtime.createVirtualMachine();
                        const auto callbacks = createCallbacks(state);
                        std::vector<Callbacks> inputs(batchSize, callbacks);
                        std::vector<Callbacks> copied(query == 2 ? batchSize : 0);
                        std::vector<nomad::DynamicVariableId> ids(batchSize);
                        auto index = nomad::NomadIndex{0};
                        auto checksum = std::uint64_t{0};
                        std::optional<nomad::NomadIndex> allocationRequests;
                        const auto measure = [&](const auto& invoke) {
                            if (allocationPass) {
                                allocationRequests = countHostCallAllocations(invoke, batchSize);
                            } else {
                                const auto start = std::chrono::steady_clock::now();
                                for (auto iteration = nomad::NomadIndex{0}; iteration < batchSize; ++iteration) {
                                    invoke();
                                }

                                latencySamples[query].push_back(std::chrono::duration<double, std::micro>{
                                    std::chrono::steady_clock::now() - start
                                }.count() / static_cast<double>(batchSize));
                            }
                        };
                        if (query == 0) {
                            measure([&] {
                                ids[index] = runtime.registerDynamicVariable(
                                    names[index],
                                    inputs[index].first,
                                    inputs[index].second,
                                    type,
                                    documentation
                                );
                                checksum += static_cast<std::uint64_t>(ids[index].id + 1);
                                ++index;
                            });
                        } else if (query == 1) {
                            measure([&] {
                                ids[index] = runtime.registerDynamicVariable(
                                    names[index],
                                    std::move(inputs[index].first),
                                    std::move(inputs[index].second),
                                    type,
                                    documentation
                                );
                                checksum += static_cast<std::uint64_t>(ids[index].id + 1);
                                ++index;
                            });
                        } else {
                            measure([&] {
                                copied[index] = inputs[index];
                                checksum += static_cast<std::uint64_t>(static_cast<bool>(copied[index].first)) +
                                    static_cast<std::uint64_t>(static_cast<bool>(copied[index].second));
                                ++index;
                            });
                        }

                        if (allocationPass) {
                            if (!allocationRequests.has_value()) {
                                continue;
                            }

                            allocationSamples[query].push_back(
                                static_cast<double>(*allocationRequests) / static_cast<double>(batchSize)
                            );
                        }

                        const auto expectedChecksum = query == 2 ? std::uint64_t{2} * batchSize :
                            static_cast<std::uint64_t>(batchSize) * (batchSize + 1) / 2;
                        if (
                            index != batchSize ||
                            checksum != expectedChecksum ||
                            state.reads != 0 ||
                            state.writes != 0
                        ) {
                            throw std::runtime_error("Dynamic-variable registration/copy invoked callbacks or changed results");
                        }

                        for (auto entry = nomad::NomadIndex{0}; entry < batchSize; ++entry) {
                            if (
                                query != 2 &&
                                (
                                    ids[entry] != nomad::DynamicVariableId{nomad::toNomadId(entry)} ||
                                    runtime.getDynamicVariableName(ids[entry]) != names[entry] ||
                                    runtime.getDynamicVariableType(ids[entry]) != type ||
                                    !runtime.canGetDynamicVariable(ids[entry]) ||
                                    !runtime.canSetDynamicVariable(ids[entry])
                                )
                            ) {
                                throw std::runtime_error("Dynamic-variable registration lost identity, metadata, or capabilities");
                            }

                            state.value = 0;
                            nomad::RuntimeValue result;
                            const auto value = nomad::RuntimeValue{static_cast<nomad::NomadInteger>(entry) + 37};
                            if (query == 2) {
                                copied[entry].second(machine.get(), result);
                            } else {
                                runtime.getDynamicVariableValue(machine.get(), ids[entry], result);
                            }

                            if (result.getIntegerValue() != expectedBias) {
                                throw std::runtime_error("A dynamic-variable callback copy lost captured values");
                            }

                            if (query == 2) {
                                copied[entry].first(machine.get(), value);
                                copied[entry].second(machine.get(), result);
                            } else {
                                runtime.setDynamicVariable(machine.get(), ids[entry], value);
                                runtime.getDynamicVariableValue(machine.get(), ids[entry], result);
                            }

                            if (result.getIntegerValue() != value.getIntegerValue()) {
                                throw std::runtime_error("Registered/copied dynamic-variable callbacks changed behavior");
                            }
                        }

                        if (
                            state.reads != 2 * batchSize ||
                            state.writes != batchSize ||
                            state.machine != machine.get()
                        ) {
                            throw std::runtime_error("Dynamic-variable callback invocation counts or VM identity changed");
                        }

                        if (query == 2) {
                            if (runtime.getFirstDynamicVariableId().isValid()) {
                                throw std::runtime_error("The callback-copy control unexpectedly registered a variable");
                            }
                        } else {
                            const auto* hub = runtime.getHub(hubId);
                            for (const auto entry : {nomad::NomadIndex{0}, batchSize / 2, batchSize - 1}) {
                                const auto* member = hub->getMember(names[entry].substr(hub->getName().size() + 1));
                                const auto* publishedId = member == nullptr ? nullptr :
                                    std::get_if<nomad::DynamicVariableId>(member);
                                if (
                                    runtime.getDynamicVariableId(names[entry]) != ids[entry] ||
                                    publishedId == nullptr ||
                                    *publishedId != ids[entry]
                                ) {
                                    throw std::runtime_error("Dynamic-variable name lookup or hub publication changed");
                                }
                            }

                            if (
                                runtime.getFirstDynamicVariableId() != ids.front() ||
                                runtime.getNextDynamicVariableId(ids.back()).isValid()
                            ) {
                                throw std::runtime_error("Dynamic-variable registration order or terminal iteration changed");
                            }
                        }
                    }
                }
            }

            std::cout << "dynamic_case=" << caseName << " registrations=" << batchSize
                << " name_bytes=" << names.front().size() << " documentation_bytes=" << documentation.size()
                << " setter_capture_bytes=" << setterCaptureBytes << " getter_capture_bytes=" << getterCaptureBytes;
            for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
                const auto timeLabel = std::format("{}_us", labels[query]);
                printSamples(timeLabel.c_str(), latencySamples[query]);
                const auto allocationLabel = std::format("{}_allocations", labels[query]);
                if (!allocationSamples[query].empty()) {
                    printSamples(allocationLabel.c_str(), allocationSamples[query]);
                } else {
                    std::cout << ' ' << allocationLabel << "=unavailable";
                }
            }

            std::cout << std::endl;
        }
    }
}

void runDynamicVariableCopyProbe() {
    using Callbacks = std::pair<nomad::DynamicVariableSetFn, nomad::DynamicVariableGetFn>;
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto batchSizes = std::array<nomad::NomadIndex, 2>{100, 1000};
    constexpr auto inputs = std::array{"lvalue", "rvalue"};
    constexpr auto labels = std::array{"setter_copies", "getter_copies", "setter_moves", "getter_moves"};
    std::cout << "Callback constructor counts use a separate instrumented target, not a latency/allocation control.\n"
        << "Input preparation is excluded; std::function target moves/storage depend on the standard library.\n"
        << std::flush;
    for (const auto batchSize : batchSizes) {
        std::vector<nomad::NomadString> names;
        for (auto index = nomad::NomadIndex{0}; index < batchSize; ++index) {
            names.push_back(std::format("bench.probe{:04}", index));
        }

        for (auto input = nomad::NomadIndex{0}; input < inputs.size(); ++input) {
            std::array<std::vector<double>, labels.size()> samples;
            for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
                nomad::test::DynamicVariableCopyCounts setterCounts;
                nomad::test::DynamicVariableCopyCounts getterCounts;
                nomad::Runtime runtime;
                if (runtime.registerHub("bench", nomad::HubMode::Opened).isInvalid()) {
                    throw std::runtime_error("Failed to register the dynamic-variable copy-probe hub");
                }

                const auto* type = runtime.getIntegerType();
                const Callbacks callbacks{
                    nomad::test::DynamicVariableCopyProbe{setterCounts},
                    nomad::test::DynamicVariableCopyProbe{getterCounts}
                };
                std::vector<Callbacks> prepared(batchSize, callbacks);
                setterCounts = {};
                getterCounts = {};
                for (auto index = nomad::NomadIndex{0}; index < batchSize; ++index) {
                    const auto id = input == 0 ?
                        runtime.registerDynamicVariable(
                            names[index],
                            prepared[index].first,
                            prepared[index].second,
                            type,
                            ""
                        ) :
                        runtime.registerDynamicVariable(
                            names[index],
                            std::move(prepared[index].first),
                            std::move(prepared[index].second),
                            type,
                            ""
                        );
                    if (id != nomad::DynamicVariableId{nomad::toNomadId(index)}) {
                        throw std::runtime_error("The dynamic-variable copy probe changed registration identity");
                    }
                }

                if (setterCounts.invocations != 0 || getterCounts.invocations != 0) {
                    throw std::runtime_error("Dynamic-variable registration invoked an instrumented callback");
                }

                samples[0].push_back(static_cast<double>(setterCounts.copies) / static_cast<double>(batchSize));
                samples[1].push_back(static_cast<double>(getterCounts.copies) / static_cast<double>(batchSize));
                samples[2].push_back(static_cast<double>(setterCounts.moves) / static_cast<double>(batchSize));
                samples[3].push_back(static_cast<double>(getterCounts.moves) / static_cast<double>(batchSize));
                auto machine = runtime.createVirtualMachine();
                for (const auto index : {nomad::NomadIndex{0}, batchSize - 1}) {
                    const auto id = nomad::DynamicVariableId{nomad::toNomadId(index)};
                    nomad::RuntimeValue value;
                    runtime.setDynamicVariable(machine.get(), id, value);
                    runtime.getDynamicVariableValue(machine.get(), id, value);
                    if (value.getIntegerValue() != 0) {
                        throw std::runtime_error("The registered instrumented getter returned an unexpected value");
                    }
                }

                if (setterCounts.invocations != 2 || getterCounts.invocations != 2) {
                    throw std::runtime_error("Registered instrumented callbacks did not retain their target state");
                }
            }

            std::cout << "dynamic_copy_probe input=" << inputs[input] << " registrations=" << batchSize
                << " instrumented_target_bytes=" << sizeof(nomad::test::DynamicVariableCopyProbe);
            for (auto query = nomad::NomadIndex{0}; query < labels.size(); ++query) {
                printSamples(labels[query], samples[query]);
            }

            std::cout << std::endl;
        }
    }
}

void runWellKnownTypeLookupBenchmark() {
    constexpr auto sampleCount = nomad::NomadIndex{5};
    constexpr auto catalogSizes = std::array<nomad::NomadIndex, 3>{0, 100, 1000};
    constexpr auto cases = std::array{
        "void", "boolean", "integer", "float", "string", "file_name", "function_name", "line_number",
        "event_callback", "event_dispatch", "function_missing", "function_late"
    };
    const auto names = std::array{
        nomad::VOID_TYPE_NAME, nomad::BOOLEAN_TYPE_NAME, nomad::INTEGER_TYPE_NAME, nomad::FLOAT_TYPE_NAME,
        nomad::STRING_TYPE_NAME, nomad::FILE_NAME_TYPE_NAME, nomad::FUNCTION_NAME_TYPE_NAME,
        nomad::FUNCTION_LINE_NUMBER_TYPE_NAME, nomad::EVENT_CALLBACK_TYPE_NAME, nomad::EVENT_DISPATCH_TYPE_NAME,
        nomad::FUNCTION_TYPE_NAME, nomad::FUNCTION_TYPE_NAME
    };
    constexpr auto labels = std::array{"convenience", "name", "id"};
    std::cout << "Well-known types: convenience getters, prepared-name lookups, and prepared-ID queries.\n"
        << "Ten constructor-registered types and function-type absence/late registration are measured separately.\n"
        << "Five fresh Runtime samples; 32 warmups; 25000 operations for built-ins, scan-budgeted function queries.\n"
        << "Debug allocation probes are separate; setup and post-registration identity checks are excluded.\n"
        << std::flush;

    for (const auto addedCount : catalogSizes) {
        std::vector<nomad::NomadString> addedNames;
        addedNames.reserve(addedCount);
        for (auto index = nomad::NomadIndex{0}; index < addedCount; ++index) {
            addedNames.push_back(std::format("R18LookupNoise{:04}", index));
        }

        std::array<std::array<std::vector<double>, labels.size()>, cases.size()> latencySamples;
        std::array<std::array<std::vector<double>, labels.size()>, cases.size()> allocationSamples;
        std::array<nomad::NomadIndex, cases.size()> typeCounts{};
        std::array<nomad::NomadIndex, cases.size()> iterationCounts{};
        std::array<nomad::NomadIndex, cases.size()> allocationIterationCounts{};
        std::array<nomad::TypeId, cases.size()> typeIds{};
        auto initialCount = nomad::NomadIndex{0};
        for (auto sample = nomad::NomadIndex{0}; sample < sampleCount; ++sample) {
            nomad::Runtime runtime;
            auto& manager = runtime.getTypeManager();
            if (sample == 0) {
                initialCount = manager.getTypeCount();
            } else if (manager.getTypeCount() != initialCount) {
                throw std::runtime_error("The initial type catalog changed between benchmark samples");
            }

            const auto builtIns = std::array{
                runtime.getVoidType(), runtime.getBooleanType(), runtime.getIntegerType(), runtime.getFloatType(),
                runtime.getStringType(), runtime.getFileNameType(), runtime.getFunctionNameType(),
                runtime.getLineNumberType(), runtime.getEventCallbackType(), runtime.getEventDispatchType()
            };
            if (runtime.getFunctionType() != nullptr) {
                throw std::runtime_error("The fresh type fixture unexpectedly contains a callback type");
            }

            for (const auto& addedName : addedNames) {
                if (runtime.registerClass(addedName, nomad::ClassMode::Closed).isInvalid()) {
                    throw std::runtime_error("Failed to populate the well-known-type benchmark catalog");
                }
            }

            if (manager.getTypeCount() != initialCount + addedCount) {
                throw std::runtime_error("Unexpected type catalog growth before lookup measurements");
            }

            const auto record = [&](
                const nomad::NomadIndex query,
                const nomad::Type* expected,
                const auto& getConvenience
            ) {
                const auto& name = names[query];
                const auto id = expected == nullptr ? nomad::TypeId::INVALID : expected->getId();
                const auto typeCount = manager.getTypeCount();
                const auto iterations = query < builtIns.size() ? nomad::NomadIndex{25000} :
                    std::clamp(
                        nomad::NomadIndex{500000} / typeCount,
                        nomad::NomadIndex{500},
                        nomad::NomadIndex{25000}
                    );
                const auto allocationIterations = std::min(nomad::NomadIndex{1000}, iterations);
                if (
                    (expected != nullptr &&
                        (expected->getRuntime() != &runtime || expected->getTypeName() != name || id.isInvalid())) ||
                    (query < builtIns.size() && expected == nullptr) ||
                    runtime.getTypeId(name) != id ||
                    runtime.getTypeByName(name) != expected
                ) {
                    throw std::runtime_error("Unexpected well-known-type identity, ownership, or name resolution");
                }

                if (sample == 0) {
                    typeCounts[query] = typeCount;
                    iterationCounts[query] = iterations;
                    allocationIterationCounts[query] = allocationIterations;
                    typeIds[query] = id;
                } else if (
                    typeCounts[query] != typeCount ||
                    iterationCounts[query] != iterations ||
                    allocationIterationCounts[query] != allocationIterations ||
                    typeIds[query] != id
                ) {
                    throw std::runtime_error("Well-known-type fixtures changed between fresh samples");
                }

                const auto measure = [&](const nomad::NomadIndex operation, const auto& lookup) {
                    for (auto index = nomad::NomadIndex{0}; index < 32; ++index) {
                        if (lookup() != expected) {
                            throw std::runtime_error("A well-known-type warmup returned the wrong descriptor");
                        }
                    }

                    auto matchingQueries = nomad::NomadIndex{0};
                    const auto start = std::chrono::steady_clock::now();
                    for (auto index = nomad::NomadIndex{0}; index < iterations; ++index) {
                        matchingQueries += lookup() == expected;
                    }

                    latencySamples[query][operation].push_back(std::chrono::duration<double, std::micro>{
                        std::chrono::steady_clock::now() - start
                    }.count() / static_cast<double>(iterations));
                    if (matchingQueries != iterations) {
                        throw std::runtime_error("Well-known-type identity changed during timing");
                    }

                    auto allocationMatches = nomad::NomadIndex{0};
                    const auto requests = countHostCallAllocations([&] {
                        allocationMatches += lookup() == expected;
                    }, allocationIterations);
                    if (requests.has_value()) {
                        if (allocationMatches != allocationIterations) {
                            throw std::runtime_error("A well-known-type allocation probe changed its result");
                        }

                        allocationSamples[query][operation].push_back(
                            static_cast<double>(*requests) / static_cast<double>(allocationIterations)
                        );
                    }
                };
                measure(0, getConvenience);
                measure(1, [&] {

                    return runtime.getTypeByName(name);
                });
                measure(2, [&]() -> const nomad::Type* {
                    const auto type = runtime.getType(id);

                    return type.has_value() ? *type : nullptr;
                });
                if (manager.getTypeCount() != typeCount) {
                    throw std::runtime_error("A well-known-type query unexpectedly changed the catalog");
                }
            };
            record(0, builtIns[0], [&] {

                return runtime.getVoidType();
            });
            record(1, builtIns[1], [&] {

                return runtime.getBooleanType();
            });
            record(2, builtIns[2], [&] {

                return runtime.getIntegerType();
            });
            record(3, builtIns[3], [&] {

                return runtime.getFloatType();
            });
            record(4, builtIns[4], [&] {

                return runtime.getStringType();
            });
            record(5, builtIns[5], [&] {

                return runtime.getFileNameType();
            });
            record(6, builtIns[6], [&] {

                return runtime.getFunctionNameType();
            });
            record(7, builtIns[7], [&] {

                return runtime.getLineNumberType();
            });
            record(8, builtIns[8], [&] {

                return runtime.getEventCallbackType();
            });
            record(9, builtIns[9], [&] {

                return runtime.getEventDispatchType();
            });
            record(10, nullptr, [&] {

                return runtime.getFunctionType();
            });
            const auto* firstCallback = manager.getCallbackType({builtIns[2]}, builtIns[0]);
            if (firstCallback == nullptr || firstCallback->asCallback() == nullptr) {
                throw std::runtime_error("Failed to register the late callback lookup fixture");
            }

            record(11, firstCallback, [&] {

                return runtime.getFunctionType();
            });
            const auto* secondCallback = manager.getCallbackType({builtIns[3]}, builtIns[2]);
            if (
                secondCallback == nullptr ||
                secondCallback == firstCallback ||
                runtime.registerClass("R18PostMeasurement", nomad::ClassMode::Closed).isInvalid() ||
                runtime.getFunctionType() != firstCallback ||
                runtime.getTypeByName(nomad::FUNCTION_TYPE_NAME) != firstCallback ||
                runtime.getType(firstCallback->getId()).value() != firstCallback
            ) {
                throw std::runtime_error("Late registration changed the first matching callback descriptor");
            }

            const auto afterGrowth = std::array{
                runtime.getVoidType(), runtime.getBooleanType(), runtime.getIntegerType(), runtime.getFloatType(),
                runtime.getStringType(), runtime.getFileNameType(), runtime.getFunctionNameType(),
                runtime.getLineNumberType(), runtime.getEventCallbackType(), runtime.getEventDispatchType()
            };
            if (afterGrowth != builtIns) {
                throw std::runtime_error("Well-known-type descriptors changed after catalog growth");
            }
        }

        for (auto query = nomad::NomadIndex{0}; query < cases.size(); ++query) {
            std::cout << "well_known_case=" << cases[query] << " added_types=" << addedCount
                << " initial_types=" << initialCount << " measured_types=" << typeCounts[query]
                << " type_id=" << typeIds[query].id << " name_bytes=" << names[query].size()
                << " added_name_bytes=" << (addedNames.empty() ? 0 : addedNames.front().size())
                << " iterations=" << iterationCounts[query]
                << " allocation_iterations=" << allocationIterationCounts[query];
            for (auto operation = nomad::NomadIndex{0}; operation < labels.size(); ++operation) {
                const auto timeLabel = std::format("{}_us", labels[operation]);
                printSamples(timeLabel.c_str(), latencySamples[query][operation]);
                const auto allocationLabel = std::format("{}_allocations", labels[operation]);
                if (!allocationSamples[query][operation].empty()) {
                    printSamples(allocationLabel.c_str(), allocationSamples[query][operation]);
                } else {
                    std::cout << ' ' << allocationLabel << "=unavailable";
                }
            }

            std::cout << std::endl;
        }
    }
}

} // namespace

int main() {
    try {
        nomad::log::setLogLevel(nomad::LogLevel::Error);
        runBenchmark();
        runOwnerResolutionBenchmark();
        runStringBenchmark();
        runCurrentCallableBenchmark();
        runNestedScriptCallBenchmark();
        runNativePropertyBenchmark();
        runHostCallBenchmark();
        runFrameResultBenchmark();
        runAssignmentScratchBenchmark();
        runEventSnapshotBenchmark();
        runDynamicVariableRegistrationBenchmark();
        runDynamicVariableCopyProbe();
        runWellKnownTypeLookupBenchmark();
    } catch (const std::exception& error) {
        std::cerr << "Runtime lookup benchmark failed: " << error.what() << '\n';

        return 1;
    }

    return 0;
}
