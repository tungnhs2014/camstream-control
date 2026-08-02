# CamStream Control Coding Standard

The project uses a project-specific C++17 coding standard informed by selected MISRA C++ safety principles, RAII,
explicit ownership, and strict C ABI design. The project does **not** claim MISRA C++ compliance.

## Userspace C and C++

### Language and formatting

- Use C++17 for project-owned userspace C++.
- Format project-owned userspace C and C++ with the root `.clang-format`.
- Use spaces rather than tabs and a maximum line length of 120 columns.
- Keep short declarations on one line when readable. Once a declaration becomes multiline, put one parameter on each
  line and do not bin-pack remaining parameters.
- Apply formatting only to the files in scope for the change; avoid unrelated repository-wide churn.
- Keep target-based CMake configuration and target-scoped compile features, include directories, warnings, and links.

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
- Keep exported symbols intentional and avoid hidden constructor-based registration.

### Names, comments, and public documentation

- Use names that describe responsibility and layer. A Camera backend implements the Camera PPI; it is not a separate PPI
  contract.
- Add concise Doxygen to public APIs and non-obvious ownership or lifecycle contracts.
- Comments explain contracts, ownership, rationale, constraints, or non-obvious failure behavior.
- Do not add comments that merely restate an assignment, branch, or function name.
- Distinguish implemented behavior, source-reviewed behavior, runtime-tested behavior, and planned work.

## Kernel code

Code under `drivers/` follows Linux kernel coding style and kernel ownership, locking, and error-handling conventions. It
is not reformatted with the userspace `.clang-format` rules.

`checkpatch.pl` is the relevant style-check keyword for project kernel changes. Run it with the selected kernel source
and project policy when reviewing a kernel patch; it was not run as part of this Stage 8.3 userspace cleanup.
