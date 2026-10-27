// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ScriptCallableBase.hpp>

#include <concepts>
#include <utility>
#include <vector>

namespace nomad {

template<class Base>
    requires (std::same_as<Base, Function> || std::same_as<Base, Method> || std::same_as<Base, Constructor>)
class ScriptCallable : public Base, public ScriptCallableBase {
public:
    [[nodiscard]] CallableId getId() const override { return Base::getId(); }
    [[nodiscard]] const NomadString& getName() const override { return Base::getName(); }
    [[nodiscard]] const std::vector<Parameter>& getParameters() const override { return Base::getParameters(); }
    [[nodiscard]] NomadIndex getParameterCount() const override { return Base::getParameterCount(); }
    [[nodiscard]] const Type* getReturnType() const override { return Base::getReturnType(); }
    [[nodiscard]] const Type* getObjectType() const override { return Base::getObjectType(); }
    void setReturnType(const Type* returnType) override { Base::setReturnType(returnType); }
    [[nodiscard]] ScriptCallableBase* asScriptCallable() override { return this; }
    [[nodiscard]] const ScriptCallableBase* asScriptCallable() const override { return this; }

protected:
    template<class... Arguments>
    ScriptCallable(
        const CallableId id,
        NomadString path,
        NomadString source,
        Arguments&&... arguments
    ):
        Base(CallableSource::Script, std::forward<Arguments>(arguments)...),
        ScriptCallableBase(std::move(path), std::move(source))
    {
        this->setId(id);
    }

    [[nodiscard]] std::vector<Parameter>& getEditableParameters() override {
        return Base::getEditableParameters();
    }
};

class ScriptFunction : public ScriptCallable<Function> {
public:
    ScriptFunction(
        const CallableId id,
        NomadString name,
        NomadString path,
        NomadString source
    ):
        ScriptCallable(
            id,
            std::move(path),
            std::move(source),
            std::move(name),
            std::vector<Parameter>{},
            nullptr,
            NomadString{}
        )
    {
    }

    [[nodiscard]] ScriptFunction* asScriptFunction() override { return this; }
    [[nodiscard]] const ScriptFunction* asScriptFunction() const override { return this; }
};

class ScriptMethod final : public ScriptCallable<Method> {
public:
    ScriptMethod(
        NomadString name,
        const Type* objectType,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString path,
        NomadString source
    );

    [[nodiscard]] const ScriptMethod* asScriptMethod() const override;
};

class ScriptConstructor final : public ScriptCallable<Constructor> {
public:
    ScriptConstructor(
        const Type* objectType,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );

    [[nodiscard]] const ScriptConstructor* asScriptConstructor() const override;
    [[nodiscard]] const Type* getConstructedType() const override;
};

} // namespace nomad
