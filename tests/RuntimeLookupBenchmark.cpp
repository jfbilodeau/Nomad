#include <nomad/log/Logger.hpp>

#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

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

} // namespace

int main() {
    try {
        nomad::log::setLogLevel(nomad::LogLevel::Error);
        runBenchmark();
        runOwnerResolutionBenchmark();
        runStringBenchmark();
    } catch (const std::exception& error) {
        std::cerr << "Runtime lookup benchmark failed: " << error.what() << '\n';

        return 1;
    }

    return 0;
}
