# Nomad

Nomad is a compiled, strongly typed scripting language
designed for embedding in C++ applications, and a 2D game engine built around it.

The current baseline builds the engine library, generic runtime, and tests.

## Build

Nomad requires CMake 3.30 or newer and a C++20 compiler.

```console
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

See [the language documentation](docs/language.md) for the current Nomad
language reference.
