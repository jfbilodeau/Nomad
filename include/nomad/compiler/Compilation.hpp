// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

enum class CompilationPhase {
    Configure = 1,
    PreParse,
    Parse,
    PrepareFormatStrings,
    Resolve,
    Emit,
    Link,
    Verify,
    Completed,
};

enum class CompilationTarget {
    Analysis = 1,
    Executable,
};

} // namespace nomad
