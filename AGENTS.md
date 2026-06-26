# OpcUaManager project instructions

## Canonical project

- This is one canonical Qt 6.11.1 CMake project.
- Develop and build it in Qt Creator.
- Do not add qmake `.pro`, `.pri`, or `.prf` support.
- Do not add Qt Virtual Keyboard.

## C++ structure

- `app/` contains only the executable entry point.
- `src/core/` contains application helpers and the OPC UA worker service.
- `src/models/` contains Qt item models and their internal model items.
- `src/qmlapi/` contains C++ facade classes exposed to QML.
- `src/gui/` contains QML engine integration.

## QML structure

- `qml/Main.qml` contains application-level behavior.
- `qml/MainScreen.ui.qml` contains declarative visual composition.
- `qml/Base/` is the reusable `Base` QML module and owns its own CMakeLists.txt.
- The main QML module URI is `OpcUaManager`.
- The reusable component module URI is `Base`.
- Load the application with `engine.loadFromModule("OpcUaManager", "Main")`.
- Keep Material light/dark switching functional through the View menu.

## Build rules

- Prefer target-based CMake APIs.
- Keep machine-specific paths in cache variables or isolated helper modules.
- Do not modify global compiler flags with `CACHE ... FORCE`.
- Preserve the C++ target boundaries so the model remains independently testable.
