# MealKit Contract

The contract between the [MealKit](https://github.com/MealKit-Engine/MealKit) engine, its editor, [MealKit Manager](https://github.com/MealKit-Engine/MealKit-Manager)
and MealKit tools such as [MealKit Audio](https://github.com/MealKit-Engine/MealKit-Audio): the spec ([docs/CONTRACT.md](docs/CONTRACT.md)) and its C++ implementation `mkcontract`. Free, MIT.

They share files and a local HTTP connection only, so each one ships on its own schedule. `mkcontract` is the one place those rules live:

| Header | What |
|---|---|
| `mkcontract/Text.h` | `@format <kind> <N>` lines, `key value` lines, line splitting |
| `mkcontract/Files.h` | UTF-8 paths, list files, `readKey`, folder comparison |
| `mkcontract/User.h` | the per-user `MealKit` folder: recent/favorite projects, added engines, editor settings |
| `mkcontract/Tool.h` | `mealkit-tool.txt` manifests and finding tools |
| `mkcontract/Live.h`, `Json.h` | live connection info files (`build/live/<kind>-<pid>.json`), `live::kProtocol`, small JSON |
| `mkcontract/Client.h` | live connection client (separate target `mkcontract_client`, sockets) |
| `mkcontract/Modules.h` | the `module.ps1 search -Tsv` lines |

```cpp
for (const mkcontract::live::Instance& in : mkcontract::live::findInstances(projectFolder))
    if (in.protocol == mkcontract::live::kProtocol) mkcontract::live::request(in, "POST", "audio/reload");
```

C++20 and the standard library only (plus Winsock/BSD sockets in `mkcontract_client`). No MealKit headers.

## Use it

Copy a tag into your repository and add it with CMake:

```
powershell -ExecutionPolicy Bypass -File tools/vendor.ps1 -To <your repo>/third_party/mkcontract [-Ref v0.1.0]
```

```cmake
add_subdirectory(third_party/mkcontract)
target_link_libraries(my_tool PRIVATE mkcontract_client)   # or mkcontract without the client
```

The MealKit engine pulls it with its own `tools\update_mkcontract.ps1` (which also updates its third-party notices) and builds against a checkout with `-DMEALKIT_MKCONTRACT_DIR=<folder>`.

## Build and test

Windows (Visual Studio 2022, CMake 3.25+, Ninja): `build.bat`. Elsewhere:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CI builds and tests on Windows, Linux and macOS.

## Changing the contract

Change `docs/CONTRACT.md`, the code and `tests/ContractTests.cpp` together. Bump `live::kProtocol` when a live connection path or response shape changes,
and a format's `@format` version when a file format changes. Then tag a release and update the users (engine, Manager, tools).
