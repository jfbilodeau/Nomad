// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeFunction.hpp>

namespace nomad {

NativeFunctionParameterDefinition defParameter(const NomadString& name, const Type* type, const NomadString& doc) {
    return {
        name,
        type,
        doc
    };
}

//NativeFunctionParameter def_parameter(NativeFunctionParameterType type, NomadDocArg) {
//    return {
//        type,
//        0
//#if defined(NOMAD_FUNCTION_DOC)
//        , doc
//#endif
//    };
//}
//
//NativeFunctionParameter def_callback(int parameter_count, NomadDocArg) {
//    return {
//        NativeFunctionParameterType::Callback,
//        parameter_count
//#if defined(NOMAD_FUNCTION_DOC)
//        , doc
//#endif
//    };
//}

} // namespace nomad
