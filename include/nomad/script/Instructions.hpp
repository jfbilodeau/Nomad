// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

class VirtualMachine;

void instructionNop(VirtualMachine * interpreter);
void instructionReturn(VirtualMachine * interpreter);
void instructionReturnZero(VirtualMachine * interpreter);
void instructionYield(VirtualMachine * interpreter);
void instructionCallNativeFunction(VirtualMachine* interpreter);
void instructionCallFunction(VirtualMachine * interpreter);
void instructionJump(VirtualMachine * interpreter);
void instructionIf(VirtualMachine * interpreter);
void instructionSetConstant(VirtualMachine * interpreter);
void instructionSetDynamicVariable(VirtualMachine * interpreter);
void instructionSetContextVariable(VirtualMachine * interpreter);
void instructionSetFunctionVariable(VirtualMachine * interpreter);
void instructionAssert(VirtualMachine * interpreter);
void instructionStringAssert(VirtualMachine * interpreter);

} // nomad
