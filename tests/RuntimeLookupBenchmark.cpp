#include <nomad/log/Logger.hpp>

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

} // namespace

int main() {
    try {
        nomad::log::setLogLevel(nomad::LogLevel::Error);
        runBenchmark();
    } catch (const std::exception& error) {
        std::cerr << "Runtime lookup benchmark failed: " << error.what() << '\n';

        return 1;
    }

    return 0;
}
