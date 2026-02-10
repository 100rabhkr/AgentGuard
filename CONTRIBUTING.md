# Contributing to AgentGuard

Thank you for your interest in contributing to AgentGuard! This document provides guidelines for contributing to the project.

## Getting Started

### Development Setup

1. Clone the repository:
```bash
git clone https://github.com/100rabhkr/AgentGuard.git
cd AgentGuard
```

2. Build the C++ library:
```bash
mkdir build && cd build
cmake .. -DAGENTGUARD_BUILD_TESTS=ON -DAGENTGUARD_BUILD_EXAMPLES=ON
cmake --build . --parallel
```

3. Install Python bindings in development mode:
```bash
pip install -e ".[dev]"
```

### Running Tests

**C++ tests:**
```bash
cd build && ctest --output-on-failure
```

**Python tests:**
```bash
pytest python/tests/ -v
```

## How to Contribute

### Reporting Issues

- Use the [GitHub issue tracker](https://github.com/100rabhkr/AgentGuard/issues)
- Include a minimal reproducible example
- Specify your OS, compiler version, and Python version
- For build issues, include the full CMake and compiler output

### Submitting Pull Requests

1. Fork the repository
2. Create a feature branch from `main`
3. Make your changes
4. Add tests for new functionality
5. Ensure all existing tests pass
6. Submit a pull request with a clear description of changes

### Code Style

- **C++**: Follow the existing code style (C++17, `snake_case` for functions/variables, `PascalCase` for types)
- **Python**: Follow PEP 8
- Keep commits focused and atomic

### Areas for Contribution

- Bug fixes and test improvements
- Documentation improvements
- New scheduling policies
- Performance optimizations
- Additional framework integrations (AutoGen, CrewAI)
- Platform-specific build fixes

## Questions?

Open an issue on GitHub or reach out to the maintainer.
