#include <nomad/compiler/DeferredType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/StructType.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>

int main(int argumentCount, char* arguments[]) {
    if (argumentCount != 2) {
        std::fputs("Expected one cleanup contract case\n", stderr);
        return 2;
    }
    const auto testCase = std::string_view{arguments[1]};
    const auto terminate = [] { std::_Exit(86); };
    try {
        nomad::Runtime runtime;
        if (testCase == "deferred-free") {
            const nomad::DeferredType type(&runtime, {"Unresolved"});
            std::set_terminate(terminate);
            type.freeValue(nullptr);
        } else {
            const auto id = runtime.registerStruct("Unfinalized");
            const auto* type = runtime.getTypeByName("Unfinalized")->asStructType();
            if (testCase == "failed-layout-fields") {
                if (runtime.registerProperty(id, "prefix", runtime.getIntegerType()).isInvalid() ||
                    runtime.registerProperty(id, "cycle", type).isInvalid()) {
                    std::fputs("Failed to prepare cyclic layout\n", stderr);
                    return 2;
                }
                auto failed = false;
                try {
                    runtime.finalizeCompositeFieldLayout(id);
                } catch (const nomad::NomadException&) {
                    failed = true;
                }
                if (!failed || type->isFieldLayoutFinalized()) {
                    std::fputs("Cyclic layout unexpectedly finalized\n", stderr);
                    return 2;
                }
            }
            std::set_terminate(terminate);
            if (testCase == "unfinalized-free") {
                type->freeValue(nullptr);
            } else if (testCase == "unfinalized-size") {
                (void)type->getFinalizedFieldStorageSize();
            } else if (testCase == "unfinalized-fields" || testCase == "failed-layout-fields") {
                (void)type->getFinalizedFields();
            } else {
                std::fputs("Unknown cleanup contract case\n", stderr);
                return 2;
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Unexpected exception: %s\n", error.what());
        return 3;
    } catch (...) {
        std::fputs("Unexpected nonstandard exception\n", stderr);
        return 3;
    }
    std::fputs("Expected std::terminate, but the operation returned\n", stderr);
    return 1;
}
