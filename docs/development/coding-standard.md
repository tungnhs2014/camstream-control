# CamStream Control Coding Standard

The project uses a project-specific C++17 coding standard informed by selected MISRA C++ safety principles, RAII,
explicit ownership, and strict C ABI design. The project does **not** claim MISRA C++ compliance.

## Userspace C and C++

### Language and formatting

- Use C++17 for project-owned userspace C++.
- Format project-owned userspace C and C++ with the root `.clang-format`.
- Use four spaces rather than tabs and a maximum line length of 120 columns.
- Use attached braces, left-aligned pointers and references, and the include ordering produced by `.clang-format`.
- Use exactly one blank line between namespace-scope function definitions, between out-of-class C++ method definitions,
  and between one function definition and the next function definition.
- Use exactly one blank line between a type or object definition and the next independent logical declaration, before a
  Doxygen or kernel-doc block for the next independent declaration, and between include groups and following macro or
  declaration sections where appropriate.
- Do not insert blank lines mechanically inside tightly related declaration groups. Keep `MaxEmptyLinesToKeep: 1` in
  `.clang-format`; formatting tools supplement rather than replace the manual readability review.
- Apply formatting only to the files in scope for the change; avoid unrelated repository-wide churn.
- Keep target-based CMake configuration and target-scoped compile features, include directories, warnings, and links.

### Line wrapping and compact formatting

- Prefer keeping declarations, statements, function calls, and simple expressions on one line when they fit within the
  configured column limit and remain readable.
- Do not introduce multiline formatting solely because a construct contains multiple arguments or parameters.
- Break lines when the configured limit would be exceeded, when an expression is genuinely complex, or when multiline
  structure materially improves readability.
- Keep logical structure visible when compacting a construct would obscure independent conditions, lifecycle steps, or
  ownership relationships. Compact formatting is not a requirement to minimize line count.
- Project-owned userspace C and C++ use the root `.clang-format` configuration and its 120-column limit. Linux kernel
  driver code follows Linux kernel coding conventions separately and is never formatted with the userspace rules.

### Naming

| Identifier | Convention | Example |
| --- | --- | --- |
| Class, C++ struct, `enum class`, public C++ type | `PascalCase` | `CameraService` |
| Function and method | `snake_case` | `release_frame()` |
| Local variable | `snake_case` | `rollback_status` |
| Function parameter | `snake_case` | `backend_module` |
| Private data member | `snake_case` without a trailing underscore | `owner_identity` |
| Internal C++ constant | `kPascalCase` | `kDiagnosticBufferSize` |
| Macro | `SCREAMING_SNAKE_CASE` | `CAMSTREAM_CAMERA_ABI_VERSION_V1` |
| C enum constant | Prefixed `SCREAMING_SNAKE_CASE` | `CAMSTREAM_CAMERA_STATUS_OK` |
| C ABI symbol or type | Prefixed `snake_case` | `camstream_camera_frame_v1` |

Private C++ members do not use a trailing underscore or prefixes such as `m_`, `m`, `this_`, or `private_`. When a
constructor or method parameter would collide with a member name, give the parameter a role-specific name:

```cpp
explicit Impl(std::string module_path)
    : backend_path(std::move(module_path)) {}
```

Rename an identifier only when its role is genuinely unclear. Standard, locally clear domain abbreviations such as
`fd`, `id`, `api`, `abi`, `ctx`, `cfg`, `src`, `dst`, `buf`, `ret`, `rc`, `argc`, `argv`, `fps`, `pts`, and `dts` may be
retained.

### Initialization and types

- Initialize every object and scalar before use.
- Prefer fixed-width integer types when size is part of a file, network, IPC, or ABI contract.
- Avoid implicit narrowing and signed/unsigned conversions. Validate range before an explicit conversion.
- Validate counts, indexes, sizes, pointers, and metadata received from external interfaces before use.
- Use `nullptr` for null pointers in C++.

### Ownership and lifetime

- Use RAII for resource ownership and deterministic cleanup.
- Do not use owning raw pointers in C++ code. Raw pointers may express non-owning access or interoperate with a C API;
  their lifetime contract must be explicit.
- Make owned and borrowed handles, memory, file descriptors, library references, and frame buffers distinguishable at
  interfaces.
- Make resource-owning types non-copyable unless copying has a defined ownership meaning.
- Keep destructors `noexcept` and cleanup idempotent where repeated stop or release is expected.
- Review normal, partial-initialization, error, and repeated-cleanup paths for leaks, double release, and use after free.
- Keep acquire/release pairs explicit. Do not access borrowed storage after successful release.

### Errors and status handling

- Check every relevant return or status value. Do not discard a result unless the reason is explicit and safe.
- Report errors with the operation and enough bounded context to diagnose the failed layer.
- Use exceptions only within C++ boundaries where they improve error propagation.
- Never allow a C++ exception to cross a C callback or C ABI boundary.
- Convert backend exceptions into documented C-compatible status values.
- Project-owned code must build with zero compiler warnings under its configured warning policy.

### Public C ABI

- Do not expose C++ types, templates, STL containers, exceptions, or platform-specific private objects through a public C
  ABI.
- Use opaque handles for implementation-owned objects.
- Use fixed-width fields and fixed-capacity arrays where ABI layout matters.
- Include explicit ABI version and structure-size fields in extensible ABI structures.
- Validate ABI version, minimum structure size, mandatory callbacks, and bounded strings before use.
- Document descriptor, instance, frame, and module lifetime with Doxygen.
- Keep exported symbols and runtime registration mechanisms intentional and documented. Constructor-based registration
  is permitted only for explicitly designed plugin or HAL registration paths. Such constructors perform registration
  only, avoid runtime resource acquisition and device I/O, and rely on the receiving layer to validate the registered
  ABI and mandatory callbacks before use.

### Names, comments, and public documentation

- Use names that describe responsibility and layer. A Camera backend implements the Camera HAL backend SPI; it is not a
  separate HAL contract.
- Add concise Doxygen to public APIs and non-obvious ownership or lifecycle contracts.
- Comments explain contracts, ownership, rationale, constraints, or non-obvious failure behavior.
- Do not add comments that merely restate an assignment, branch, or function name.
- Distinguish implemented behavior, source-reviewed behavior, runtime-tested behavior, and planned work.

## Kernel code

Code under `drivers/` follows Linux kernel coding style and kernel ownership, locking, and error-handling conventions. It
is not reformatted with the userspace `.clang-format` rules. Kernel code uses tabs for indentation, K&R braces,
`snake_case`, established kernel abbreviations, normal `goto` cleanup paths, and the appropriate kernel logging helper.
Project-owned global symbols, structures, and major helpers use a `camstream_` prefix where needed to avoid namespace
collisions. Comments and kernel-doc explain ownership, locking, lifetime, framework constraints, or non-obvious error
handling rather than restating straightforward code.

`checkpatch.pl` is the relevant style checker for project kernel changes. Run it with the pinned kernel source and
project policy when the validation scope permits it.

## CMake, Buildroot, shell, and Markdown

- **CMake:** keep target-based declarations grouped consistently, use four-space indentation, and format multiline
  commands without changing option defaults, dependencies, visibility, link order, or install behavior.
- **Buildroot:** follow upstream `Config.in`, package `.mk`, help-text, continuation, and package-prefix conventions.
  Do not apply C/C++ formatting tools to Buildroot files.
- **Shell:** use consistent four-space indentation, quote expansions according to their intended word-splitting
  behavior, and keep conditions and functions readable. Do not add shell strict-mode options when they could change
  existing exit behavior.
- **Markdown:** edit structure and wrapping manually. Do not apply blind repository-wide formatting.
