// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

namespace nomad {

// Forward declarations
class VirtualMachine;
class Type;

using UnaryFoldingFn = void (*)(const RuntimeValue&, RuntimeValue&);
using BinaryFoldingFn = void (*)(const RuntimeValue&, const RuntimeValue&, RuntimeValue&);

enum class UnaryOperator {
    Unknown = 1,
    Bang,
    Plus,
    Minus,
    Sin,
    Cos,
    Tan,
};

UnaryOperator getUnaryOperator(const NomadString& symbol);
NomadString toString(UnaryOperator op);

enum class BinaryOperator {
    Unknown = 1,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,
    And,
    Pipe,
    EqualEqual,
    BangEqual,
    LessThan,
    LessThanEqual,
    GreaterThan,
    GreaterThanEqual,
    AndAnd,
    PipePipe,
};

BinaryOperator getBinaryOperator(const NomadString& symbol);
NomadString toString(BinaryOperator op);

// Folding functions

// Unary operator implementations

// Boolean unary operators
void foldBooleanUnaryBang(const RuntimeValue& value, RuntimeValue& result);

// Integer unary operators
void foldIntegerUnaryPlus(const RuntimeValue& value, RuntimeValue& result);
void foldIntegerUnaryMinus(const RuntimeValue& value, RuntimeValue& result);

// Float unary operators
void foldFloatUnaryPlus(const RuntimeValue& value, RuntimeValue& result);
void foldFloatUnaryMinus(const RuntimeValue& value, RuntimeValue& result);
void foldFloatUnarySin(const RuntimeValue& value, RuntimeValue& result);
void foldFloatUnaryCos(const RuntimeValue& value, RuntimeValue& result);
void foldFloatUnaryTan(const RuntimeValue& value, RuntimeValue& result);

// Binary operator implementations

// Boolean binary operators
void foldBooleanBinaryAndAnd(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldBooleanBinaryPipePipe(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldBooleanBinaryEqualEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldBooleanBinaryBangEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);

// Integer binary operators
void foldIntegerBinaryPlus(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryMinus(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryStar(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinarySlash(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryPercent(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryCaret(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerAnd(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerPipe(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryEqualEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryBangEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryLessThan(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryLessThanEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryGreaterThan(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldIntegerBinaryGreaterThanEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);

// Float binary operators
void foldFloatBinaryPlus(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryMinus(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryStar(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinarySlash(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryPercent(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryCaret(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryEqualEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryBangEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryLessThan(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryLessThanEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryGreaterThan(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldFloatBinaryGreaterThanEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);

void foldStringBinaryEqualEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);
void foldStringBinaryBangEqual(const RuntimeValue& lhs, const RuntimeValue& rhs, RuntimeValue& result);

} // nomad
