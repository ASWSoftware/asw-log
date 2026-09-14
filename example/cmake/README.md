# ASWLog CMake Example

Configure and build from the repository root:

```text
cmake -S example/cmake -B example/cmake/build
cmake --build example/cmake/build --config Release
```

The example executable is written to the same configuration-specific
`example/build/bin/<Configuration>` directory used by the RAD Studio project.
Logs are written relative to the executable working directory.
