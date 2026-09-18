# Writes the documentation site's root language chooser (site/index.html).
#
# Invoked by the `docs` target with:
#   OUTPUT_FILE   absolute path of the index.html to write
#   FALLBACK_LANG language code used when the viewer language has no manual
#   LANGS         CMake list (";"-separated) of available manual language codes

if(NOT DEFINED OUTPUT_FILE OR NOT DEFINED FALLBACK_LANG OR NOT DEFINED LANGS)
    message(FATAL_ERROR "WriteDocsIndex.cmake requires OUTPUT_FILE, FALLBACK_LANG and LANGS")
endif()

# LANGS arrives comma-separated (a ";"-list would be split across -D arguments).
string(REPLACE "," ";" LANGS "${LANGS}")

set(_names_en "English")
set(_names_de "Deutsch")
set(_names_fr "Français")
set(_names_it "Italiano")
set(_names_ru "Русский")
set(_names_uk "Українська")

# Human-readable link list and a JS array of available codes.
set(_links "")
set(_js_langs "")
foreach(_lang IN LISTS LANGS)
    if(DEFINED _names_${_lang})
        set(_label "${_names_${_lang}}")
    else()
        set(_label "${_lang}")
    endif()
    set(_links "${_links}      <li><a href=\"${_lang}/index.html\">${_label}</a></li>\n")
    if(_js_langs STREQUAL "")
        set(_js_langs "'${_lang}'")
    else()
        set(_js_langs "${_js_langs}, '${_lang}'")
    endif()
endforeach()

set(_html "<!DOCTYPE html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\">
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
  <title>OpcUaManager Documentation</title>
  <script>
    // Redirect to the manual matching the browser/UI language, else the fallback.
    var available = [${_js_langs}]
    var fallback = '${FALLBACK_LANG}'
    var pref = (navigator.language || 'en').slice(0, 2).toLowerCase()
    var target = available.indexOf(pref) !== -1 ? pref : fallback
    // ?lang=xx overrides (the in-app viewer passes the current UI language).
    var q = new URLSearchParams(window.location.search).get('lang')
    if (q) { q = q.slice(0, 2).toLowerCase(); if (available.indexOf(q) !== -1) target = q }
    window.location.replace(target + '/index.html')
  </script>
</head>
<body>
  <h1>OpcUaManager Documentation</h1>
  <p>Choose a language:</p>
  <ul>
${_links}  </ul>
  <p><a href=\"opcuamanager/index.html\">API Reference (English)</a></p>
</body>
</html>
")

file(WRITE "${OUTPUT_FILE}" "${_html}")
message(STATUS "Wrote documentation language chooser: ${OUTPUT_FILE}")
