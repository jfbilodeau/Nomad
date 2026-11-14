// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ExecutionStateGuard.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>

#include <boost/test/unit_test.hpp>

#include <type_traits>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<ExecutionStateGuard>);
static_assert(!std::is_copy_assignable_v<ExecutionStateGuard>);
static_assert(!std::is_move_constructible_v<ExecutionStateGuard>);
static_assert(!std::is_move_assignable_v<ExecutionStateGuard>);
static_assert(std::is_nothrow_constructible_v<ExecutionStateGuard, VirtualMachine&>);
static_assert(std::is_nothrow_destructible_v<ExecutionStateGuard>);

BOOST_AUTO_TEST_SUITE(execution_state_guard)

BOOST_AUTO_TEST_CASE(restores_complete_state_on_normal_exit_and_exception_without_owning_values) {
    Runtime runtime;
    RuntimeValue receiver{NomadString{"borrowed receiver"}};
    const RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
    for (const auto running : {false, true}) {
        for (const auto throws : {false, true}) {
            VirtualMachine machine(&runtime, 64);
            auto before = machine.captureExecutionState();
            before.instructionIndex = 7;
            before.stackIndex = 9;
            before.parameterIndex = 3;
            before.variableIndex = 6;
            before.running = running;
            before.receiver = receiver;
            machine.restoreExecutionState(before);
            auto caught = false;
            try {
                const ExecutionStateGuard state(machine);
                BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
                auto invocation = before;
                invocation.instructionIndex = 17;
                invocation.stackIndex = 19;
                invocation.parameterIndex = 13;
                invocation.variableIndex = 16;
                invocation.running = !running;
                invocation.receiver = RuntimeValue{};
                machine.restoreExecutionState(invocation);
                machine.setIntegerResult(42);
                machine.setIntegerIntermediate(55);
                if (throws) {
                    throw NomadException("Expected execution-state unwinding");
                }
            } catch (const NomadException&) {
                caught = true;
            }

            const auto after = machine.captureExecutionState();
            BOOST_TEST(caught == throws);
            BOOST_TEST(after.instructionIndex == before.instructionIndex);
            BOOST_TEST(after.stackIndex == before.stackIndex);
            BOOST_TEST(after.parameterIndex == before.parameterIndex);
            BOOST_TEST(after.variableIndex == before.variableIndex);
            BOOST_TEST(after.running == before.running);
            BOOST_TEST(after.receiver.getObjectValue() == receiver.getObjectValue());
            BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(machine.getIntegerResult() == 42);
            BOOST_TEST(machine.getIntegerIntermediate() == 55);
        }
    }
}

BOOST_AUTO_TEST_CASE(nested_guards_restore_in_reverse_order) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 64);
    machine.pushInteger(11);
    machine.jump(7);
    {
        const ExecutionStateGuard outerState(machine);
        machine.pushInteger(22);
        machine.jump(17);
        {
            const ExecutionStateGuard innerState(machine);
            machine.pushInteger(33);
            machine.jump(27);
        }

        BOOST_TEST(machine.captureExecutionState().stackIndex == 2U);
        BOOST_TEST(machine.getInstructionIndex() == 17U);
        BOOST_TEST(machine.getStackAddress(0)->getIntegerValue() == 22);
    }

    BOOST_TEST(machine.captureExecutionState().stackIndex == 1U);
    BOOST_TEST(machine.getInstructionIndex() == 7U);
    BOOST_TEST(machine.getStackAddress(0)->getIntegerValue() == 11);
}

BOOST_AUTO_TEST_CASE(release_leaves_current_state_active) {
    Runtime runtime;
    RuntimeValue receiver{NomadString{"retained receiver"}};
    const RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
    VirtualMachine machine(&runtime, 64);
    auto expected = machine.captureExecutionState();
    expected.instructionIndex = 17;
    expected.stackIndex = 19;
    expected.parameterIndex = 13;
    expected.variableIndex = 16;
    expected.running = true;
    expected.receiver = receiver;
    {
        ExecutionStateGuard state(machine);
        machine.restoreExecutionState(expected);
        state.release();
        state.release();
    }

    const auto after = machine.captureExecutionState();
    BOOST_TEST(after.instructionIndex == expected.instructionIndex);
    BOOST_TEST(after.stackIndex == expected.stackIndex);
    BOOST_TEST(after.parameterIndex == expected.parameterIndex);
    BOOST_TEST(after.variableIndex == expected.variableIndex);
    BOOST_TEST(after.running == expected.running);
    BOOST_TEST(after.receiver.getObjectValue() == receiver.getObjectValue());
    BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
}

BOOST_AUTO_TEST_SUITE_END()
