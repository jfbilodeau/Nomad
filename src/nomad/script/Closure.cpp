// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Closure.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/system/String.hpp>

namespace nomad {

Closure::Closure(const Function* function, std::vector<RuntimeValue> captures):
	m_function(function),
	m_capturedParameters(std::move(captures))
{
	if (m_function == nullptr) {
		log::warning("Closure constructor: null function");
		return;
	}

	// New design: captured parameters are passed as-is and stored.
	// When the closure is executed, the interpreter consolidates them with
	// explicit parameters into a unified parameter frame.
}

Closure::~Closure() {
	if (m_function == nullptr) {
		return;
	}

	// Clean up captured parameter values
	for (NomadIndex i = 0; i < m_capturedParameters.size(); ++i) {
		const auto parameterIndex = toNomadId(m_function->getParameterCount() - m_capturedParameters.size() + i);

		if (const auto type = m_function->getCaptureType(parameterIndex)) {
			type->freeValue(m_capturedParameters[i]);
		}
	}
}

NomadIndex Closure::getCaptureCount() const {
	return m_capturedParameters.size();
}

const RuntimeValue& Closure::getCaptureValue(const NomadIndex captureIndex) const {
	if (captureIndex >= m_capturedParameters.size()) {
		static const RuntimeValue invalidValue{};
		return invalidValue;
	}
	return m_capturedParameters[captureIndex];
}

const Type* Closure::getCaptureType(const NomadIndex captureIndex) const {
	if (m_function == nullptr || captureIndex >= m_capturedParameters.size()) {
		return nullptr;
	}

	// Captured parameters are at the end of the parameter list
	const auto parameterIndex = toNomadId(m_function->getParameterCount() - m_capturedParameters.size() + captureIndex);
	return m_function->getParameterType(parameterIndex);
}

}
