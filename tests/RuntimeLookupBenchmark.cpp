#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>

#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadObject.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/VirtualMachine.hpp>

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
#include <optional>
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
        "raw_two_strings", "raw_capture_vector", "closure_empty", "closure_integer", "closure_captured"
    };
    constexpr auto probeLabels = std::array{
        "integer_parameter_shape", "integer_call_layout", "integer_closure_frame", "raw_empty_explicit_captures"
    };
    const auto source = nomad::NomadString{
        "fun benchmarkHostEmpty\nreturn 42\nend\n"
        "fun benchmarkHostInteger value:int\nreturn value\nend\n"
        "fun benchmarkHostFour first:int second:int third:int fourth:int\n"
        "return first + second + third + fourth\nend\n"
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
        const auto* four = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostFour"));
        const auto* strings = runtime.getScriptFunction(runtime.getScriptFunctionId("benchmarkHostStrings"));
        for (const auto* function : {empty, integer, four, strings}) {
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
        const std::vector fourArguments{
            nomad::RuntimeValue{nomad::NomadInteger{3}},
            nomad::RuntimeValue{nomad::NomadInteger{7}},
            nomad::RuntimeValue{nomad::NomadInteger{13}},
            nomad::RuntimeValue{nomad::NomadInteger{19}}
        };
        const std::vector textArguments{firstText, secondText};
        const std::vector captureArguments{nomad::RuntimeValue{nomad::NomadInteger{21}}};
        const nomad::Closure emptyClosure(empty);
        const nomad::Closure integerClosure(integer);
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
    } catch (const std::exception& error) {
        std::cerr << "Runtime lookup benchmark failed: " << error.what() << '\n';

        return 1;
    }

    return 0;
}
