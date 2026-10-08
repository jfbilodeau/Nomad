This is the repository's Copilot instructions file. It provides guidance to GitHub Copilot on how to generate code that is consistent with the project's coding standards and practices.

## Project Description

The Nomad game engine is a 2D game engine written in C++ using modern C++20 features. It utilizes SDL3 for windowing and input, Box2D for physics simulation, and Boost libraries for various utilities. The engine is designed to be cross-platform, supporting Windows, Linux, macOS, iOS, and Android.

## C++ Coding Guidelines

- `enum` and `enum class` start at `1` instead of `0` to make it easier to detect uninitialized values.
- Keep `#include` organized as follows:
  - In a `.cpp`, always start with the corresponding header file
	- Project headers are next (or first in a header file), followed by third-party headers (i.e. SDL3), and then system headers.
  - Group includes logically, with a blank line between each group.
	- Project files are grouped by 'module.' For example, group `nomad/compiler/*`, `nomad/game/*`, and `nomad/geometry/*` together.
	- Third party headers are grouped the same.
	- System headers are grouped together as well.
	- Separate groups by blank line.
	- Keep headers sorted alphabetically within each group.
- Prefer C++20 standard library types and features over Boost equivalents when one exists (for example, `std::optional` instead of `boost::optional`). Use Boost only for utilities that have no C++20 standard equivalent. Do not replace existing Boost usage unless the task asks for it.
- Use K&R style for formatting and indentation. Enforce via `.clang-format` or equivalent.
- Use PascalCase for class names and camelCase for functions and variables. Acronyms should not be fully capitalized (e.g., `SdlManager`).
- Prefix member variables with `m_` and static member variables with `s_`.
- Name accessors and mutators `getXXX` and `setXXX`; mark getters with `[[nodiscard]]`.
- Use full words in identifier. For example, use `calculateVelocity` instead of `calcVel`.
- Use meaningful names for identifier names, variables, functions, and classes. Avoid single-letter names except for loop indices.
- Avoid names like `temp`, `util`, or `helper` for identifiers. Use descriptive names that convey purpose.
- Use smart pointers (`std::shared_ptr`, `std::unique_ptr`) for resource management; avoid raw pointers unless necessary for performance or API compatibility.
- Use `[[nodiscard]]` for functions returning important values to prevent accidental discards.
- Avoid copyable classes for core engine systems unless explicitly required; use `delete` for copy constructors/assignment where appropriate.
- Use `#pragma once` for header guards.
- Use angle brackets (`#include <...>`) instead of quotation marks (`#include "..."`) for all include directives, including project headers.
- Prefer modern C++ idioms and avoid legacy patterns.
- Use multi-platform code practices to ensure compatibility across Windows, Linux, macOS, iOS and Android.
- Prefer `auto` over explicit type declaration.

## Dependency Management

- Use CMake's `FetchContent` for dependency management; avoid manual submodule management.
- Link against libraries using CMake's `find_package` and `target_link_libraries`.
- Use SDL3 and Boost libraries as specified in CMake; Do not use SDL2
- Pin dependency versions in CMake to ensure reproducible builds.

## Project Structure

- Organize source and include files by module (e.g., `game/`, `geometry/`, `log/`).
- Keep header files in `include/` and source files in `src/`.

## Agent Workflow

- **Purpose:** Provide concise, actionable guidance for AI assistants (Copilot, chat agents) to be productive in this repository.
- **Discover:** Search for build scripts, `CMakeLists.txt`, top-level `README.md`, and platform-specific run tasks before making changes.
- **Build & Run:** Use the workspace CMake tasks. Common tasks available in this workspace:
	- `CMake: build` — builds `ALL_BUILD` targets via CMake.
	- `Run Wishlair1` — launches the game binary at `build/bin/Debug/nomad.exe` (Windows), passing `--resource-path res`. Look at `./vscode/tasks.json` and ./vscode/launch.json for the exact command line.
	
## Conventions & Anti-Patterns for AI

- **Follow existing style:** Respect the C++ guidelines above (K&R formatting, PascalCase for classes, camelCase for variables and parameters, `m_` prefix for members).
- **Small, focused changes:** Prefer minimal edits that keep diffs readable; don't reformat large vendor or third-party directories.
- **Rebuild after changes:** Always run the build task after making changes to ensure no compilation errors.
- **If the build task fails:** Read the compiler errors from the output pane, fix them, and run the build task again. If the build still fails after two attempts, stop and report the errors to the user without claiming the change is complete.
- **Don't modify generated or vendored files** inside `build/` or `_deps/` unless explicitly asked.
- **Tests & verification:** When changing code, add or update small unit tests in `tests/` when feasible. Run the build task before proposing larger changes.
