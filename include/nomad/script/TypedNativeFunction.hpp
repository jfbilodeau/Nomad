// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace nomad {

struct TypedParameterDefinition {
    NomadString name;
    NomadDocField;
};

namespace detail {

template<typename Value>
inline constexpr bool isTypedNativeValue =
    std::same_as<Value, NomadBoolean> ||
    std::same_as<Value, NomadInteger> ||
    std::same_as<Value, NomadFloat> ||
    std::same_as<Value, NomadString>;

template<typename Argument>
inline constexpr bool isTypedNativeArgument =
    isTypedNativeValue<std::remove_cvref_t<Argument>> &&
    !std::is_volatile_v<std::remove_reference_t<Argument>> &&
    (!std::is_reference_v<Argument> ||
        (std::is_lvalue_reference_v<Argument> && std::is_const_v<std::remove_reference_t<Argument>>));

template<typename Value, typename RuntimeType>
const Type* getTypedNativeType(RuntimeType& runtime) {
    using NativeValue = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<NativeValue, void>) {
        return runtime.getVoidType();
    } else if constexpr (std::same_as<NativeValue, NomadBoolean>) {
        return runtime.getBooleanType();
    } else if constexpr (std::same_as<NativeValue, NomadInteger>) {
        return runtime.getIntegerType();
    } else if constexpr (std::same_as<NativeValue, NomadFloat>) {
        return runtime.getFloatType();
    } else {
        static_assert(std::same_as<NativeValue, NomadString>);
        return runtime.getStringType();
    }
}

template<typename Argument>
decltype(auto) readTypedNativeArgument(const VirtualMachine* machine, const NomadIndex index) {
    using NativeValue = std::remove_cvref_t<Argument>;
    if constexpr (std::same_as<NativeValue, NomadBoolean>) {
        return machine->getBooleanParameter(index);
    } else if constexpr (std::same_as<NativeValue, NomadInteger>) {
        return machine->getIntegerParameter(index);
    } else if constexpr (std::same_as<NativeValue, NomadFloat>) {
        return machine->getFloatParameter(index);
    } else {
        static_assert(std::same_as<NativeValue, NomadString>);
        return machine->getStringParameter(index)->getString();
    }
}

template<typename Function, typename = void>
struct TypedNativeFunctionTraits {};

template<typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(Arguments...)> {
    using ReturnType = Result;
    static constexpr auto parameterCount = sizeof...(Arguments);
    static constexpr bool supported =
        (std::is_void_v<Result> ||
            (!std::is_reference_v<Result> && isTypedNativeValue<std::remove_cv_t<Result>>)) &&
        (isTypedNativeArgument<Arguments> && ...);

    template<typename RuntimeType, std::size_t... Indices>
    static std::vector<Parameter> makeParameters(
        RuntimeType& runtime,
        const std::vector<TypedParameterDefinition>& definitions,
        std::index_sequence<Indices...>
    ) {
        return {
            defParameter(definitions[Indices].name, getTypedNativeType<Arguments>(runtime), definitions[Indices].doc)...
        };
    }

    template<typename Function, std::size_t... Indices>
    static NativeFunctionFn bind(Function function, std::index_sequence<Indices...>) {
        return [function = std::move(function)](VirtualMachine* machine) mutable {
            if constexpr (std::is_void_v<Result>) {
                std::invoke(function, readTypedNativeArgument<Arguments>(machine, Indices)...);
            } else if constexpr (std::same_as<std::remove_cv_t<Result>, NomadBoolean>) {
                machine->setBooleanResult(std::invoke(function, readTypedNativeArgument<Arguments>(machine, Indices)...));
            } else if constexpr (std::same_as<std::remove_cv_t<Result>, NomadInteger>) {
                machine->setIntegerResult(std::invoke(function, readTypedNativeArgument<Arguments>(machine, Indices)...));
            } else if constexpr (std::same_as<std::remove_cv_t<Result>, NomadFloat>) {
                machine->setFloatResult(std::invoke(function, readTypedNativeArgument<Arguments>(machine, Indices)...));
            } else {
                static_assert(std::same_as<std::remove_cv_t<Result>, NomadString>);
                machine->setStringResult(std::invoke(function, readTypedNativeArgument<Arguments>(machine, Indices)...));
            }
        };
    }

    using Indices = std::index_sequence_for<Arguments...>;
};

template<typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(*)(Arguments...)> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(*)(Arguments...) noexcept> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Owner, typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(Owner::*)(Arguments...)> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Owner, typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(Owner::*)(Arguments...) const> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Owner, typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(Owner::*)(Arguments...) noexcept> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Owner, typename Result, typename... Arguments>
struct TypedNativeFunctionTraits<Result(Owner::*)(Arguments...) const noexcept> : TypedNativeFunctionTraits<Result(Arguments...)> {};

template<typename Function>
struct TypedNativeFunctionTraits<Function, std::void_t<decltype(&Function::operator())>> :
    TypedNativeFunctionTraits<decltype(&Function::operator())> {};

template<typename Function>
concept TypedNativeFunction =
    requires { typename TypedNativeFunctionTraits<Function>::ReturnType; } &&
    TypedNativeFunctionTraits<Function>::supported &&
    std::copy_constructible<Function> &&
    !std::is_member_function_pointer_v<Function>;

} // namespace detail

} // namespace nomad
