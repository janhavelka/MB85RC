# Contributing

Thank you for considering contributing to this project!

## Quick Start

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run the relevant validation commands below
5. Commit with a clear message: `git commit -m "feat: add X"`
6. Push and open a Pull Request

## Guidelines

### Code Style

- Follow existing code style (see `.clang-format`)
- Use `constexpr` instead of macros for constants
- Prefer explicit over implicit
- No heap allocations in steady-state library code

### Commits

- Use [Conventional Commits](https://www.conventionalcommits.org/) format:
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation only
  - `refactor:` code change that neither fixes a bug nor adds a feature
  - `test:` adding or updating tests
  - `chore:` maintenance tasks

### Pull Requests

- Keep PRs focused (one feature/fix per PR)
- Update README, public Doxygen comments, examples, and device references when
  their contracts change
- Add changelog entry under `[Unreleased]`
- Ensure CI passes

### Validation

Run at least:

```bash
python scripts/generate_version.py check
python tools/check_metadata_consistency.py
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
doxygen Doxyfile
```

Changes affecting Arduino compilation should also build `esp32s3dev`,
`esp32s2dev`, and `esp32s3dev_legacy_54`. Changes affecting the native ESP-IDF
example should build both configured IDF targets when `idf.py` is available.
Doxygen is strict: undocumented public members, missing parameter/return
documentation, and documentation errors fail the build.

### What We Accept

- Bug fixes
- Documentation improvements
- Performance improvements (with benchmarks)
- New examples (if they demonstrate a common use case)

### What We Probably Won't Accept

- Breaking API changes without discussion
- Heavy dependencies
- Platform-specific code in the library core
- Features that add heap allocations in steady state

## Questions?

Open a GitHub Discussion or Issue for questions.
