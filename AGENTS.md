# Qt Creator Reference Project Instructions

## Project Identity

This is the Qt Creator-only reference project for `OpcUaManager`. It is related
to the main Qt Design Studio project, but it is not required to remain
Qt Design Studio compatible.

Use this project only when the user explicitly targets the reference project or
when comparing behavior against the main project.

## Main Structure

- `CMakeLists.txt` - hand-maintained root CMake project opened by Qt Creator.
- `src/` - hand-maintained C++ application and backend area.
  - `src/main.cpp` is the active entry point.
  - `src/core/opcuamanager.*` implements the QML-facing OPC UA manager.
  - `src/core/opcuaservice.*` implements the worker-thread OPC UA service.
  - `src/models/opcuamodel.*` and `treeitem.*` implement the lazy OPC UA tree
    model for QML `TreeView`.
  - `src/gui/appengine.*` creates the QML engine and exposes
    `cppManagerOpcUa`.
- `qml/` - main QML module area.
  - `qml/Main.qml` is the application root QML type.
  - `qml/MainScreen.ui.qml` is a visual UI form.
- `qml/Base/` - reusable QML component module area.
- `tests/` - reserved for reference-project tests if active tests are added.
- `font/`, `pki/`, and `translations/` - runtime assets, OPC UA PKI material
  location, and Qt Linguist files.
- `#Archve/` - historical/reference material. Treat it as opt-in input.
- `OpcUaManager.pro` and `additional_functions.prf` - legacy/reference QMake
  files. Do not expand or modernize them unless explicitly requested.

## Current Integration Facts

- `src/main.cpp` instantiates `AppEngine`.
- `AppEngine` registers `cppManagerOpcUa` as a QML context property.
- The CMake path uses `engine.loadFromModule("OpcUaManager", "Main")`.
- The QMake conditional path still references `qrc:/qml/Main.qml`, but CMake is
  the preferred workflow.
- The root `CMakeLists.txt` defines the executable, C++ source lists, QML
  modules, runtime copy steps, and translations directly.

Reconfirm these facts when changing entry-point, QML load path, context
properties, QML modules, or CMake wiring.

## Reference Project Change Rules

- Use `src/` as the hand-maintained C++ application area.
- Use `qml/` and `qml/Base/` as the QML module areas.
- Root `CMakeLists.txt` is hand-maintained and may be edited when the task
  targets this project.
- Use target-based CMake and Qt CMake APIs for new build work.
- Do not add new QMake wiring. Existing `.pro` and `.prf` files are
  legacy/reference artifacts unless the user explicitly asks to work on them.
- Do not impose main-project QDS restrictions here. There is no `qds.cmake`,
  `App/`, `OpcUaManagerContent/`, `Dependencies/`, or `App/autogen/` boundary in
  this project.
- Keep `.ui.qml` files mostly declarative and visual, but this is a Qt Quick UI
  form maintainability rule, not a Qt Design Studio compatibility requirement.
- Treat `#Archve/`, build directories, generated compiler output, and Qt
  Creator metadata as opt-in/noise unless a task explicitly requires them.

## Reference Project Git Notes

This project currently has a nested `.git` repository and an existing dirty
index/worktree. Do not revert, reset, or clean unrelated changes. Work with the
existing state and only stage/commit files when the user explicitly asks for
Git commit work.

## Reference Project Validation

Codex must not build or run this project. User validation is required from
Qt Creator with the intended Qt 6.9.1 MSVC kit. For command-equivalent
validation, use:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Run the application from Qt Creator and check Application Output for the first
relevant `qCritical()` or `qWarning()` block when debugging startup, QML load,
or OPC UA runtime behavior.
