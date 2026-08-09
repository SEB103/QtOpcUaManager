# QDoc Style Reference

This directory contains QDoc styling reference material only.

The active project documentation currently uses the official Qt QDoc template through the generated configuration from `cmake/OpcUaManagerDocs.cmake`. The `__qdoc-qtcreator-dark-example.css` file is not included by `doc/opcuamanager.qdocconf`; it is kept only as an example of a manual theme.

## Local Qt Styles

The installed Qt 6.11.1 documentation configuration is located here:

```text
C:/Qt/6.11.1/msvc2022_64/doc/global/
```

Main QDoc configuration files:

```text
C:/Qt/6.11.1/msvc2022_64/doc/global/qt-module-defaults.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/qt-module-defaults-offline.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/qt-html-templates-offline.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/qt-html-templates-offline-simple.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/html-header-offline.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/html-footer.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/macros.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/qt-cpp-defines.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/fileextensions.qdocconf
C:/Qt/6.11.1/msvc2022_64/doc/global/compat.qdocconf
```

Official Qt QDoc template CSS files:

```text
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/offline.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/offline-simple.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/offline-dark.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/online.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/htmltabs.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/gsc.css
C:/Qt/6.11.1/msvc2022_64/doc/global/template/style/cookie-confirm.css
```

Official Qt QDoc template image assets:

```text
C:/Qt/6.11.1/msvc2022_64/doc/global/template/images/
```

## Upstream Qt repository

These files are provided by the QtBase repository under the `doc/global` directory.

Main directory:

```text
https://github.com/qt/qtbase/tree/6.11/doc/global
```

Template styles:

```text
https://github.com/qt/qtbase/tree/6.11/doc/global/template/style
```

Individual files:

```text
https://github.com/qt/qtbase/blob/6.11/doc/global/qt-module-defaults.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/qt-module-defaults-offline.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/qt-html-templates-offline.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/qt-html-templates-offline-simple.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/html-header-offline.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/html-footer.qdocconf
https://github.com/qt/qtbase/blob/6.11/doc/global/template/style/offline.css
https://github.com/qt/qtbase/blob/6.11/doc/global/template/style/offline-simple.css
https://github.com/qt/qtbase/blob/6.11/doc/global/template/style/offline-dark.css
https://github.com/qt/qtbase/blob/6.11/doc/global/template/style/online.css
```

## How To Include A Manual Stylesheet If Needed

For an active override, explicitly add the stylesheet to `doc/opcuamanager.qdocconf`:

```qdocconf
HTML.stylesheets += \
    style/my-style.css

HTML.headerstyles += \
    "  <link rel=\"stylesheet\" type=\"text/css\" href=\"style/my-style.css\" />\n"

qhp.extraFiles += \
    style/my-style.css
```

For the current project, prefer the official Qt defaults template from `qt-module-defaults.qdocconf`; keep manual CSS files only as examples unless a project-specific override is required.
