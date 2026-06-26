# Optional imported UI content

This directory is an intentionally preserved extension point. It may later
contain QML screens, components, images, and other assets exported from a
Figma-to-Qt workflow, Qt Design Studio, or another UI generator.

To activate it, add:

```text
importedcontent/CMakeLists.txt
```

The root project detects that file automatically. The imported CMake file may
create an independent QML module or attach files to `appOpcUaManager` with
`qt_target_qml_sources()` or `qt_add_resources()`.

The manually maintained application UI remains in `qml/` and does not depend
on this directory.
