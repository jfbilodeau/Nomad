// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <utility>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(virtual_machine_storage)

BOOST_AUTO_TEST_CASE(contiguous_parameter_blocks_and_local_views_keep_physical_order) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(5);
    machine.pushInteger(4);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(0);
    machine.reserveLocals(4);
    const auto& constMachine = machine;
    const auto point = machine.getParameterStorage(0, 3);
    BOOST_TEST(point[0].getIntegerValue() == 1);
    BOOST_TEST(point[2].getIntegerValue() == 3);
    BOOST_TEST(constMachine.getParameterStorage(3, 1)[0].getIntegerValue() == 4);
    BOOST_TEST(constMachine.getParameterStorage(4, 1)[0].getIntegerValue() == 5);
    BOOST_TEST(constMachine.getEnclosingParameterStorage(0, 3).data() == point.data());
    machine.getEnclosingParameterStorage(0, 3)[1].setIntegerValue(17);
    BOOST_TEST(point[1].getIntegerValue() == 17);
    const auto locals = machine.getLocalStorage(1, 3);
    locals[0].setIntegerValue(23);
    BOOST_TEST(constMachine.getLocalStorage(1, 3)[0].getIntegerValue() == 23);
    const auto* original = locals.data();
    machine.pushInteger(42);
    machine.callScriptFunction(0);
    machine.reserveLocals(2);
    BOOST_TEST(machine.getParameterStorage(0, 1)[0].getIntegerValue() == 42);
    BOOST_TEST(machine.getLocalStorage(0, 2).data() != original);
    machine.returnFunction(2);
    machine.pop1();
    BOOST_TEST(machine.getLocalStorage(1, 3).data() == original);
    BOOST_TEST(machine.getParameterStorage(0, 3).data() == point.data());
    machine.returnFunction(4);
    machine.popN(5);
}

BOOST_AUTO_TEST_CASE(native_calls_and_reentry_preserve_enclosing_script_storage) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(0);
    machine.reserveLocals(1);
    machine.getLocalStorage(0, 1)[0].setIntegerValue(19);
    const auto* enclosing = machine.getEnclosingParameterStorage(0, 3).data();
    const auto id = runtime.registerNativeFunction("inspectStorage",
        [&](VirtualMachine* active) {
            BOOST_TEST(active->getParameterStorage(0, 1)[0].getIntegerValue() == 99);
            BOOST_TEST(std::as_const(*active).getEnclosingParameterStorage(0, 3).data() == enclosing);
            active->getEnclosingParameterStorage(0, 3)[0].setIntegerValue(7);
            active->pushInteger(8);
            active->callScriptFunction(0);
            active->reserveLocals(1);
            BOOST_TEST(active->getEnclosingParameterStorage(0, 1)[0].getIntegerValue() == 8);
            active->returnFunction(1);
            active->pop1();
            BOOST_TEST(active->getParameterStorage(0, 1)[0].getIntegerValue() == 99);
            BOOST_TEST(active->getEnclosingParameterStorage(0, 3).data() == enclosing);
        }, {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(id.isValid());
    machine.pushInteger(99);
    machine.callNativeFunction(id);
    machine.pop1();
    BOOST_TEST(machine.getParameterStorage(0, 3)[0].getIntegerValue() == 7);
    BOOST_TEST(machine.getLocalStorage(0, 1)[0].getIntegerValue() == 19);
    machine.returnFunction(1);
    machine.popN(3);
}

BOOST_AUTO_TEST_CASE(empty_views_and_invalid_physical_ranges_are_safe) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 16);
    const auto maximum = std::numeric_limits<NomadIndex>::max();
    const auto& constMachine = machine;
    BOOST_TEST(machine.getLocalStorage(maximum, 0).empty());
    BOOST_TEST(machine.getParameterStorage(maximum, 0).empty());
    BOOST_TEST(machine.getEnclosingParameterStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getLocalStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getParameterStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getEnclosingParameterStorage(maximum, 0).empty());
    BOOST_CHECK_THROW((void)machine.getLocalStorage(0, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getParameterStorage(0, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getEnclosingParameterStorage(0, 1), VirtualMachineException);
    machine.pushInteger(1);
    machine.callScriptFunction(0);
    BOOST_CHECK_THROW((void)machine.getLocalStorage(0, 1), VirtualMachineException);
    machine.reserveLocals(2);
    BOOST_CHECK_THROW((void)machine.getLocalStorage(maximum, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)constMachine.getLocalStorage(0, maximum), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getLocalStorage(2, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getParameterStorage(0, 2), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getParameterStorage(maximum, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getParameterStorage(0, maximum), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getEnclosingParameterStorage(0, 2), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getEnclosingParameterStorage(maximum, 1), VirtualMachineException);
    BOOST_CHECK_THROW((void)machine.getEnclosingParameterStorage(0, maximum), VirtualMachineException);
    machine.returnFunction(2);
    machine.pop1();
}

BOOST_AUTO_TEST_SUITE_END()
