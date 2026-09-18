# OpcUaManager documentation generation.
#
# Produces one connected, offline HTML documentation site under
#   ${CMAKE_BINARY_DIR}/doc/site/
# containing:
#   site/opcuamanager/   the EN-only C++ and QML API reference (generated from the
#                        QDoc /*! ... */ comments in the .cpp and .qml sources)
#   site/<lang>/         the hand-written manual (user + developer guide) per language
#   site/index.html      a small language chooser that redirects into a manual
#
# The manual sets link into the API set through QDoc's cross-set index mechanism
# (depends + -indexdir), so architecture/project-structure pages resolve
# \l ClassName to the generated API pages.
#
# The C++ headers use compact Doxygen /** ... */ comments and are intentionally
# NOT a QDoc source (documentationinheaders stays false); the API reference comes
# from the \class/\struct topics (doc/api and some .cpp) plus the member /*! ... */
# comments in the .cpp files. The separate docs-doxygen target below builds the
# header-based Doxygen reference and is left unchanged.

option(QDOC_DOCUMENTATION_IN_HEADERS "Let QDoc parse documentation from C++ headers" OFF)

# --- Paths -------------------------------------------------------------------

set(OPCUAMANAGER_DOC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/doc")
set(OPCUAMANAGER_QDOC_BASE_CONFIG "${OPCUAMANAGER_DOC_SOURCE_DIR}/opcuamanager.qdocconf")
set(OPCUAMANAGER_DOC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/doc/gen")
set(OPCUAMANAGER_DOC_SITE_DIR "${CMAKE_CURRENT_BINARY_DIR}/doc/site")
set(OPCUAMANAGER_API_OUTPUT_DIR "${OPCUAMANAGER_DOC_SITE_DIR}/opcuamanager")
set(OPCUAMANAGER_QCH_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doc/qch")

file(MAKE_DIRECTORY "${OPCUAMANAGER_DOC_GEN_DIR}")
file(MAKE_DIRECTORY "${OPCUAMANAGER_DOC_SOURCE_DIR}/images")

if(QDOC_DOCUMENTATION_IN_HEADERS)
    set(OPCUAMANAGER_QDOC_DOCUMENTATION_IN_HEADERS_VALUE "true")
else()
    set(OPCUAMANAGER_QDOC_DOCUMENTATION_IN_HEADERS_VALUE "false")
endif()

# Manual languages: every doc/manual/<lang> that has an index.qdoc is built.
set(OPCUAMANAGER_MANUAL_LANGUAGES)
file(GLOB OPCUAMANAGER_MANUAL_CANDIDATES
    LIST_DIRECTORIES true
    RELATIVE "${OPCUAMANAGER_DOC_SOURCE_DIR}/manual"
    "${OPCUAMANAGER_DOC_SOURCE_DIR}/manual/*"
)
foreach(OPCUAMANAGER_MANUAL_CANDIDATE IN LISTS OPCUAMANAGER_MANUAL_CANDIDATES)
    if(EXISTS "${OPCUAMANAGER_DOC_SOURCE_DIR}/manual/${OPCUAMANAGER_MANUAL_CANDIDATE}/index.qdoc")
        list(APPEND OPCUAMANAGER_MANUAL_LANGUAGES "${OPCUAMANAGER_MANUAL_CANDIDATE}")
    endif()
endforeach()
if(NOT OPCUAMANAGER_MANUAL_LANGUAGES)
    set(OPCUAMANAGER_MANUAL_LANGUAGES en)
endif()
# The chooser redirects here when the UI language has no manual.
if("en" IN_LIST OPCUAMANAGER_MANUAL_LANGUAGES)
    set(OPCUAMANAGER_MANUAL_FALLBACK_LANGUAGE en)
else()
    list(GET OPCUAMANAGER_MANUAL_LANGUAGES 0 OPCUAMANAGER_MANUAL_FALLBACK_LANGUAGE)
endif()

# --- Qt module defaults (for module CSS/scripts and Qt type cross-links) ------

file(TO_CMAKE_PATH "${OPCUAMANAGER_QDOC_BASE_CONFIG}" OPCUAMANAGER_QDOC_BASE_CONFIG_QDOC)

set(OPCUAMANAGER_QDOC_QT_DEFAULTS_INCLUDE)
if(DEFINED Qt6_DIR)
    get_filename_component(OPCUAMANAGER_QT_KIT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    set(OPCUAMANAGER_QDOC_QT_DEFAULTS
        "${OPCUAMANAGER_QT_KIT_DIR}/doc/global/qt-module-defaults.qdocconf"
    )
    if(EXISTS "${OPCUAMANAGER_QDOC_QT_DEFAULTS}")
        file(TO_CMAKE_PATH "${OPCUAMANAGER_QDOC_QT_DEFAULTS}" OPCUAMANAGER_QDOC_QT_DEFAULTS_QDOC)
        string(REPLACE "." "" OPCUAMANAGER_QDOC_QT_VERSION_TAG "${Qt6_VERSION}")
        set(OPCUAMANAGER_QDOC_QT_DEFAULTS_INCLUDE
            "QT_VERSION = ${Qt6_VERSION}\nQT_VER = ${Qt6_VERSION}\nQT_VERSION_TAG = ${OPCUAMANAGER_QDOC_QT_VERSION_TAG}\ninclude(${OPCUAMANAGER_QDOC_QT_DEFAULTS_QDOC})\n"
        )
    endif()
endif()

# Absolute source/header/include directories for the API set.
file(TO_CMAKE_PATH "${OPCUAMANAGER_DOC_SOURCE_DIR}/api" OPCUAMANAGER_DIR_DOC_API)
file(TO_CMAKE_PATH "${OPCUAMANAGER_DOC_SOURCE_DIR}/images" OPCUAMANAGER_DIR_DOC_IMAGES)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app" OPCUAMANAGER_DIR_APP)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src" OPCUAMANAGER_DIR_SRC)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/runtime" OPCUAMANAGER_DIR_RUNTIME)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml" OPCUAMANAGER_DIR_QML)
set(OPCUAMANAGER_API_HEADERDIRS
    "${OPCUAMANAGER_DIR_APP} ${OPCUAMANAGER_DIR_SRC} ${OPCUAMANAGER_DIR_RUNTIME}"
)
set(OPCUAMANAGER_API_SOURCEDIRS
    "${OPCUAMANAGER_DIR_DOC_API} ${OPCUAMANAGER_DIR_APP} ${OPCUAMANAGER_DIR_SRC} ${OPCUAMANAGER_DIR_RUNTIME} ${OPCUAMANAGER_DIR_QML}"
)

# Qt include directories so libclang can parse the translation units.
set(OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS)
foreach(OPCUAMANAGER_QDOC_QT_TARGET IN ITEMS
    Qt6::Core Qt6::Gui Qt6::Network Qt6::OpcUa Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
    get_target_property(OPCUAMANAGER_QDOC_QT_TARGET_INCLUDE_DIRS
        ${OPCUAMANAGER_QDOC_QT_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(OPCUAMANAGER_QDOC_QT_TARGET_INCLUDE_DIRS)
        list(APPEND OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS ${OPCUAMANAGER_QDOC_QT_TARGET_INCLUDE_DIRS})
    endif()
endforeach()
list(REMOVE_DUPLICATES OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS)
set(OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS_QDOC)
foreach(OPCUAMANAGER_QDOC_QT_INCLUDE_DIR IN LISTS OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS)
    file(TO_CMAKE_PATH "${OPCUAMANAGER_QDOC_QT_INCLUDE_DIR}" OPCUAMANAGER_QDOC_QT_INCLUDE_DIR_QDOC)
    list(APPEND OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS_QDOC "${OPCUAMANAGER_QDOC_QT_INCLUDE_DIR_QDOC}")
endforeach()
list(JOIN OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS_QDOC " " OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS_QDOC)

# --- Generated config: API set ----------------------------------------------

set(OPCUAMANAGER_API_CONFIG "${OPCUAMANAGER_DOC_GEN_DIR}/opcuamanager-api.qdocconf")
file(WRITE "${OPCUAMANAGER_API_CONFIG}"
    "${OPCUAMANAGER_QDOC_QT_DEFAULTS_INCLUDE}"
    "include(${OPCUAMANAGER_QDOC_BASE_CONFIG_QDOC})\n"
    "project = OpcUaManager\n"
    "version = ${PROJECT_VERSION}\n"
    # The small right-aligned nav tag: our own product, linking to our local home
    # (the genuine Qt crumb is navigation.homepage, set to the online Qt docs).
    "buildversion = \"OpcUaManager ${PROJECT_VERSION}\"\n"
    # Relative base so the manual sets (site/<lang>) cross-link to this API set
    # (site/opcuamanager) locally and offline, overriding the online Qt url that
    # qt-module-defaults sets globally.
    "url = ../opcuamanager\n"
    "headerdirs = ${OPCUAMANAGER_API_HEADERDIRS}\n"
    "sourcedirs = ${OPCUAMANAGER_API_SOURCEDIRS}\n"
    "includepaths = ${OPCUAMANAGER_API_HEADERDIRS} ${OPCUAMANAGER_QDOC_QT_INCLUDE_DIRS_QDOC}\n"
    "imagedirs = ${OPCUAMANAGER_DIR_DOC_IMAGES}\n"
    "documentationinheaders = ${OPCUAMANAGER_QDOC_DOCUMENTATION_IN_HEADERS_VALUE}\n"
    "macro.param = \"\\\\a\"\n"
    "macro.appversion = \"${PROJECT_VERSION}\"\n"
    "clangcompilationdatabase = ${CMAKE_BINARY_DIR}\n"
    "navigation.landingpage = \"OpcUaManager API Reference\"\n"
    "navigation.cppclassespage = \"OpcUaManager C++ Classes\"\n"
    "navigation.qmltypespage = \"OpcUaManager QML Types\"\n"
    "qhp.projects = OpcUaManager\n"
    "qhp.OpcUaManager.file = OpcUaManager.qhp\n"
    "qhp.OpcUaManager.namespace = org.opcuamanager.api.${PROJECT_VERSION}\n"
    "qhp.OpcUaManager.virtualFolder = api\n"
    "qhp.OpcUaManager.indexTitle = OpcUaManager API Reference\n"
    "qhp.OpcUaManager.subprojects = classes qmltypes\n"
    "qhp.OpcUaManager.subprojects.classes.title = C++ Classes\n"
    "qhp.OpcUaManager.subprojects.classes.indexTitle = OpcUaManager C++ Classes\n"
    "qhp.OpcUaManager.subprojects.classes.selectors = class function namespace fake:headerfile\n"
    "qhp.OpcUaManager.subprojects.classes.sortPages = true\n"
    "qhp.OpcUaManager.subprojects.qmltypes.title = QML Types\n"
    "qhp.OpcUaManager.subprojects.qmltypes.indexTitle = OpcUaManager QML Types\n"
    "qhp.OpcUaManager.subprojects.qmltypes.selectors = qmltype\n"
    "qhp.OpcUaManager.subprojects.qmltypes.sortPages = true\n"
)

# --- Generated config: one manual set per language --------------------------

set(OPCUAMANAGER_MANUAL_CONFIGS)
foreach(OPCUAMANAGER_LANG IN LISTS OPCUAMANAGER_MANUAL_LANGUAGES)
    file(TO_CMAKE_PATH "${OPCUAMANAGER_DOC_SOURCE_DIR}/manual/${OPCUAMANAGER_LANG}" OPCUAMANAGER_DIR_MANUAL_LANG)
    set(OPCUAMANAGER_MANUAL_CONFIG "${OPCUAMANAGER_DOC_GEN_DIR}/opcuamanager-manual-${OPCUAMANAGER_LANG}.qdocconf")
    # Localized title of the manual's landing page. Must match the \title of the
    # language's index.qdoc so navigation resolves.
    if(OPCUAMANAGER_LANG STREQUAL "de")
        set(_guide_title "OpcUaManager-Handbuch")
    elseif(OPCUAMANAGER_LANG STREQUAL "fr")
        set(_guide_title "Guide OpcUaManager")
    elseif(OPCUAMANAGER_LANG STREQUAL "it")
        set(_guide_title "Guida OpcUaManager")
    elseif(OPCUAMANAGER_LANG STREQUAL "ru")
        set(_guide_title "Руководство OpcUaManager")
    elseif(OPCUAMANAGER_LANG STREQUAL "uk")
        set(_guide_title "Посібник OpcUaManager")
    else()
        set(_guide_title "OpcUaManager Guide")
    endif()
    file(WRITE "${OPCUAMANAGER_MANUAL_CONFIG}"
        "${OPCUAMANAGER_QDOC_QT_DEFAULTS_INCLUDE}"
        "include(${OPCUAMANAGER_QDOC_BASE_CONFIG_QDOC})\n"
        "project = OpcUaManagerManual\n"
        "version = ${PROJECT_VERSION}\n"
        "buildversion = \"OpcUaManager ${PROJECT_VERSION}\"\n"
        "naturallanguage = ${OPCUAMANAGER_LANG}\n"
        "sourcedirs = ${OPCUAMANAGER_DIR_MANUAL_LANG}\n"
        "imagedirs = ${OPCUAMANAGER_DIR_DOC_IMAGES}\n"
        "macro.appversion = \"${PROJECT_VERSION}\"\n"
        "depends += opcuamanager\n"
        # navigation.homepage is set by the shared base to the online Qt reference
        # (the "Qt 6.11.1" crumb); our own landing is the landingpage crumb.
        "navigation.landingpage = \"${_guide_title}\"\n"
        "qhp.projects = OpcUaManagerManual\n"
        "qhp.OpcUaManagerManual.file = OpcUaManagerManual.qhp\n"
        "qhp.OpcUaManagerManual.namespace = org.opcuamanager.manual.${OPCUAMANAGER_LANG}.${PROJECT_VERSION}\n"
        "qhp.OpcUaManagerManual.virtualFolder = manual\n"
        "qhp.OpcUaManagerManual.indexTitle = ${_guide_title}\n"
        "qhp.OpcUaManagerManual.subprojects = pages\n"
        "qhp.OpcUaManagerManual.subprojects.pages.title = Pages\n"
        "qhp.OpcUaManagerManual.subprojects.pages.selectors = fake:page\n"
        "qhp.OpcUaManagerManual.subprojects.pages.sortPages = true\n"
    )
    list(APPEND OPCUAMANAGER_MANUAL_CONFIGS "${OPCUAMANAGER_LANG}=${OPCUAMANAGER_MANUAL_CONFIG}=${OPCUAMANAGER_DOC_SITE_DIR}/${OPCUAMANAGER_LANG}")
endforeach()

# --- Tools -------------------------------------------------------------------

set(OPCUAMANAGER_QDOC_HINTS)
if(DEFINED Qt6_DIR)
    list(APPEND OPCUAMANAGER_QDOC_HINTS "${Qt6_DIR}/../../../bin" "${Qt6_DIR}/../../../libexec")
endif()
if(DEFINED ENV{QTDIR})
    list(APPEND OPCUAMANAGER_QDOC_HINTS "$ENV{QTDIR}/bin")
endif()

find_program(OPCUAMANAGER_QDOC_EXECUTABLE NAMES qdoc qdoc.exe HINTS ${OPCUAMANAGER_QDOC_HINTS})

# Qt documentation index dir (to resolve \l QObject etc.).
set(OPCUAMANAGER_QT_INDEX_ARGS)
if(DEFINED Qt6_DIR AND DEFINED Qt6_VERSION)
    get_filename_component(OPCUAMANAGER_QT_KIT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(OPCUAMANAGER_QT_VERSION_DIR "${OPCUAMANAGER_QT_KIT_DIR}/.." ABSOLUTE)
    get_filename_component(OPCUAMANAGER_QT_ROOT_DIR "${OPCUAMANAGER_QT_VERSION_DIR}/.." ABSOLUTE)
    set(OPCUAMANAGER_QT_INDEX_DIR "${OPCUAMANAGER_QT_ROOT_DIR}/Docs/Qt-${Qt6_VERSION}")
    if(EXISTS "${OPCUAMANAGER_QT_INDEX_DIR}")
        list(APPEND OPCUAMANAGER_QT_INDEX_ARGS -indexdir "${OPCUAMANAGER_QT_INDEX_DIR}")
    endif()
endif()

# --- docs target -------------------------------------------------------------

if(OPCUAMANAGER_QDOC_EXECUTABLE)
    set(OPCUAMANAGER_DOCS_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OPCUAMANAGER_DOC_SITE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPCUAMANAGER_API_OUTPUT_DIR}"
        # 1) API set first: it produces site/opcuamanager/opcuamanager.index.
        COMMAND "${OPCUAMANAGER_QDOC_EXECUTABLE}"
            -outputdir "${OPCUAMANAGER_API_OUTPUT_DIR}"
            ${OPCUAMANAGER_QT_INDEX_ARGS}
            "${OPCUAMANAGER_API_CONFIG}"
    )
    # 2) Each manual set, linking against the API index via -indexdir <site>.
    foreach(OPCUAMANAGER_MANUAL_ENTRY IN LISTS OPCUAMANAGER_MANUAL_CONFIGS)
        string(REPLACE "=" ";" OPCUAMANAGER_MANUAL_PARTS "${OPCUAMANAGER_MANUAL_ENTRY}")
        list(GET OPCUAMANAGER_MANUAL_PARTS 1 OPCUAMANAGER_MANUAL_CFG)
        list(GET OPCUAMANAGER_MANUAL_PARTS 2 OPCUAMANAGER_MANUAL_OUT)
        list(APPEND OPCUAMANAGER_DOCS_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPCUAMANAGER_MANUAL_OUT}"
            COMMAND "${OPCUAMANAGER_QDOC_EXECUTABLE}"
                -outputdir "${OPCUAMANAGER_MANUAL_OUT}"
                -indexdir "${OPCUAMANAGER_DOC_SITE_DIR}"
                ${OPCUAMANAGER_QT_INDEX_ARGS}
                "${OPCUAMANAGER_MANUAL_CFG}"
        )
    endforeach()
    # 3) The root language chooser. Pass the language list comma-separated: a
    #    CMake list (";"-separated) would be split into separate -D arguments.
    string(REPLACE ";" "," OPCUAMANAGER_MANUAL_LANGUAGES_CSV "${OPCUAMANAGER_MANUAL_LANGUAGES}")
    list(APPEND OPCUAMANAGER_DOCS_COMMANDS
        COMMAND "${CMAKE_COMMAND}"
            -D "OUTPUT_FILE=${OPCUAMANAGER_DOC_SITE_DIR}/index.html"
            -D "FALLBACK_LANG=${OPCUAMANAGER_MANUAL_FALLBACK_LANGUAGE}"
            -D "LANGS=${OPCUAMANAGER_MANUAL_LANGUAGES_CSV}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteDocsIndex.cmake"
    )
    add_custom_target(docs
        ${OPCUAMANAGER_DOCS_COMMANDS}
        WORKING_DIRECTORY "${OPCUAMANAGER_DOC_SOURCE_DIR}"
        COMMENT "Generating OpcUaManager documentation site (API + manual)"
        VERBATIM
    )
else()
    message(STATUS "QDoc executable not found; the docs target will report this when invoked.")
    add_custom_target(docs
        COMMAND "${CMAKE_COMMAND}" -E echo "QDoc executable not found; cannot generate the documentation site."
        COMMAND "${CMAKE_COMMAND}" -E false
        COMMENT "QDoc is unavailable"
        VERBATIM
    )
endif()

# --- docs-doxygen: header-based reference (unchanged) ------------------------

find_program(OPCUAMANAGER_QHELPGENERATOR_EXECUTABLE
    NAMES qhelpgenerator qhelpgenerator.exe HINTS ${OPCUAMANAGER_QDOC_HINTS})

find_package(Doxygen QUIET)

set(OPCUAMANAGER_DOXYGEN_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile")
set(OPCUAMANAGER_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doc/doxygen")
set(OPCUAMANAGER_DOXYGEN_QCH_FILE "${OPCUAMANAGER_QCH_OUTPUT_DIR}/OpcUaManagerDoxygen.qch")

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app" OPCUAMANAGER_DOXYGEN_INPUT_APP)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src" OPCUAMANAGER_DOXYGEN_INPUT_SRC)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests" OPCUAMANAGER_DOXYGEN_INPUT_TESTS)
file(TO_CMAKE_PATH "${OPCUAMANAGER_DOXYGEN_OUTPUT_DIR}" OPCUAMANAGER_DOXYGEN_OUTPUT_DIR_DOXYGEN)
file(TO_CMAKE_PATH "${OPCUAMANAGER_DOXYGEN_QCH_FILE}" OPCUAMANAGER_DOXYGEN_QCH_FILE_DOXYGEN)
file(TO_CMAKE_PATH "${OPCUAMANAGER_QHELPGENERATOR_EXECUTABLE}" OPCUAMANAGER_QHELPGENERATOR_EXECUTABLE_DOXYGEN)

file(WRITE "${OPCUAMANAGER_DOXYGEN_CONFIG}"
    "PROJECT_NAME = \"${PROJECT_NAME}\"\n"
    "PROJECT_NUMBER = \"${PROJECT_VERSION}\"\n"
    "OUTPUT_DIRECTORY = \"${OPCUAMANAGER_DOXYGEN_OUTPUT_DIR_DOXYGEN}\"\n"
    "GENERATE_HTML = YES\n"
    "JAVADOC_AUTOBRIEF = YES\n"
    "GENERATE_QHP = YES\n"
    "QCH_FILE = \"${OPCUAMANAGER_DOXYGEN_QCH_FILE_DOXYGEN}\"\n"
    "QHP_NAMESPACE = org.opcuamanager.doxygen\n"
    "QHP_VIRTUAL_FOLDER = doc\n"
    "QHP_CUST_FILTER_NAME = OpcUaManagerDoxygen\n"
    "QHP_CUST_FILTER_ATTRS = OpcUaManager ${PROJECT_VERSION} Doxygen\n"
    "QHG_LOCATION = \"${OPCUAMANAGER_QHELPGENERATOR_EXECUTABLE_DOXYGEN}\"\n"
    "GENERATE_LATEX = NO\n"
    "INPUT = \"${OPCUAMANAGER_DOXYGEN_INPUT_APP}\" \"${OPCUAMANAGER_DOXYGEN_INPUT_SRC}\" \"${OPCUAMANAGER_DOXYGEN_INPUT_TESTS}\"\n"
    "RECURSIVE = YES\n"
    "FILE_PATTERNS = *.h *.hpp\n"
    "EXCLUDE_PATTERNS = *.cpp *.qml *.js *.qdoc *.qdocconf\n"
    "QUIET = YES\n"
)

if(DOXYGEN_FOUND AND OPCUAMANAGER_QHELPGENERATOR_EXECUTABLE)
    add_custom_target(docs-doxygen
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OPCUAMANAGER_DOXYGEN_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPCUAMANAGER_DOXYGEN_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPCUAMANAGER_QCH_OUTPUT_DIR}"
        COMMAND "${DOXYGEN_EXECUTABLE}" "${OPCUAMANAGER_DOXYGEN_CONFIG}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating OpcUaManager Doxygen HTML and QCH documentation"
        VERBATIM
    )
else()
    add_custom_target(docs-doxygen
        COMMAND "${CMAKE_COMMAND}" -E echo "Doxygen or qhelpgenerator executable not found; cannot generate Doxygen documentation."
        COMMAND "${CMAKE_COMMAND}" -E false
        COMMENT "Doxygen or qhelpgenerator is unavailable"
        VERBATIM
    )
endif()
