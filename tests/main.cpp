// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#define BOOST_TEST_MODULE Nomad
#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

// Entry point to unit tests

namespace {

// Tests use SDL I/O without initializing SDL. A failed call (e.g. opening a missing file) allocates SDL's
// thread-local error buffer, which (with SDL's TLS bookkeeping) only SDL_Quit() releases.
struct SdlShutdown {
    SdlShutdown() = default;
    SdlShutdown(const SdlShutdown&) = delete;
    SdlShutdown& operator=(const SdlShutdown&) = delete;

    ~SdlShutdown() {
        SDL_Quit();
    }
};

} // namespace

BOOST_TEST_GLOBAL_FIXTURE(SdlShutdown);
