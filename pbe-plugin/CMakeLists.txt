# VST3 + CLAP plugin around pbe-dsp, using CPLUG for the plugin formats and
# pugl + NanoVG for the editor. One module is built and laid out as both a
# .vst3 bundle and a .clap; CPLUG exports both entry points from it.
#
# Third-party code is compiled from source (nothing is installed on the
# system): point the PBE_*_DIR variables at checkouts of
#   https://github.com/Tremus/CPLUG
#   https://github.com/lv2/pugl
#   https://github.com/memononen/nanovg
# The nix dev shell and package set these for you.

# File/bundle name (no spaces); the display name lives in config.h.
set(PBE_PLUGIN_NAME "PhantomBass")
set(PBE_PLUGIN_VERSION "${PBE_VERSION}") # from the top-level VERSION file
set(PBE_BUNDLE_ID "com.daltoncaron.phantom-bass")

set(PBE_CPLUG_DIR "$ENV{PBE_CPLUG_DIR}" CACHE PATH "CPLUG source tree")
set(PBE_PUGL_DIR "$ENV{PBE_PUGL_DIR}" CACHE PATH "pugl source tree")
set(PBE_NANOVG_DIR "$ENV{PBE_NANOVG_DIR}" CACHE PATH "NanoVG source tree")
set(PBE_GUI_FONT "$ENV{PBE_GUI_FONT}" CACHE FILEPATH "TrueType font embedded in the editor")
option(PBE_PLUGIN_TOOLS "Build the host-less test tools (clap-smoke, gui-preview)" ON)

foreach(dep CPLUG PUGL NANOVG)
  if(NOT PBE_${dep}_DIR OR NOT IS_DIRECTORY "${PBE_${dep}_DIR}")
    message(FATAL_ERROR "PBE_${dep}_DIR must point at a source checkout (got '${PBE_${dep}_DIR}')")
  endif()
endforeach()

if(NOT PBE_GUI_FONT)
  find_file(PBE_GUI_FONT_DEFAULT NAMES DejaVuSans.ttf
    PATHS /usr/share/fonts/TTF /usr/share/fonts/truetype/dejavu /usr/share/fonts/dejavu
          /usr/local/share/fonts /Library/Fonts ~/Library/Fonts)
  set(PBE_GUI_FONT "${PBE_GUI_FONT_DEFAULT}")
endif()
if(NOT PBE_GUI_FONT OR NOT EXISTS "${PBE_GUI_FONT}")
  message(FATAL_ERROR "PBE_GUI_FONT must point at a .ttf file (DejaVuSans.ttf was not found)")
endif()
message(STATUS "Editor font: ${PBE_GUI_FONT}")

# ---- Embedded font -----------------------------------------------------------
set(font_c "${CMAKE_CURRENT_BINARY_DIR}/font-data.c")
add_custom_command(
  OUTPUT "${font_c}"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${PBE_GUI_FONT} -DOUTPUT=${font_c} -DSYMBOL=pbe_font_data
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS "${PBE_GUI_FONT}" cmake/EmbedFile.cmake
  COMMENT "Embedding ${PBE_GUI_FONT}"
)

# ---- Third-party sources -----------------------------------------------------
set(pugl_sources ${PBE_PUGL_DIR}/src/common.c ${PBE_PUGL_DIR}/src/internal.c)
set(pugl_defs PUGL_STATIC PUGL_INTERNAL)
set(platform_libs)
if(WIN32)
  list(APPEND pugl_sources ${PBE_PUGL_DIR}/src/win.c ${PBE_PUGL_DIR}/src/win_gl.c)
  list(APPEND pugl_defs UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
  list(APPEND platform_libs opengl32 gdi32 user32 dwmapi shell32 shlwapi)
elseif(APPLE)
  enable_language(OBJC)
  list(APPEND pugl_sources ${PBE_PUGL_DIR}/src/mac.m ${PBE_PUGL_DIR}/src/mac_gl.m)
  list(APPEND pugl_defs GL_SILENCE_DEPRECATION)
  list(APPEND platform_libs "-framework Cocoa" "-framework OpenGL")
else()
  list(APPEND pugl_sources ${PBE_PUGL_DIR}/src/x11.c ${PBE_PUGL_DIR}/src/x11_gl.c)
  # Only libX11 is required; keep the optional X extensions off so the
  # plugin has no dependencies a host process won't already have loaded.
  list(APPEND pugl_defs USE_XRANDR=0 USE_XSYNC=0 USE_XCURSOR=0 _POSIX_C_SOURCE=200809L)
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  list(APPEND platform_libs X11::X11 OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
endif()

set(nanovg_sources ${PBE_NANOVG_DIR}/src/nanovg.c)
set(cplug_sources ${PBE_CPLUG_DIR}/src/cplug_vst3.c ${PBE_CPLUG_DIR}/src/cplug_clap.c)

# Third-party code is compiled as-is; don't let our warning flags apply to it.
set_source_files_properties(${pugl_sources} ${nanovg_sources} ${cplug_sources} "${font_c}"
  PROPERTIES COMPILE_OPTIONS "-w")

# CPLUG's wrappers read the plugin identity from macros they expect to be
# pre-included (their upstream build does the same with -include).
if(MSVC)
  set(config_include "/FI${CMAKE_CURRENT_SOURCE_DIR}/config.h")
else()
  set(config_include "-include${CMAKE_CURRENT_SOURCE_DIR}/config.h")
endif()

# ---- Plugin core: everything except the format wrappers ----------------------
# An OBJECT library so the host-less tools can link the same code.
add_library(pbe-plugin-core OBJECT
  src/plugin.c
  src/params.c
  src/engine.c
  src/gui/editor.c
  src/gui/knob.c
  src/gui/canvas-nanovg.c
  src/gui/gl-loader.c
  src/gui/window-pugl.c
  "${font_c}"
  ${pugl_sources}
  ${nanovg_sources}
)
target_include_directories(pbe-plugin-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${PBE_CPLUG_DIR}/src
         ${PBE_PUGL_DIR}/include
  PRIVATE ${PBE_NANOVG_DIR}/src
)
target_compile_definitions(pbe-plugin-core
  PRIVATE ${pugl_defs}
  PUBLIC PBE_VERSION_STRING="${PBE_PLUGIN_VERSION}" # consumed by config.h
)
if(NOT MSVC)
  # cplug.h defines an AUv2 four-char constant that trips -Wall.
  target_compile_options(pbe-plugin-core PUBLIC -Wno-multichar)
endif()
target_link_libraries(pbe-plugin-core PUBLIC pbe-dsp ${platform_libs})
set_target_properties(pbe-plugin-core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  C_STANDARD 11
)

# ---- The plugin module -------------------------------------------------------
add_library(pbe-plugin MODULE ${cplug_sources})
target_compile_options(pbe-plugin PRIVATE ${config_include})
target_link_libraries(pbe-plugin PRIVATE pbe-plugin-core)
set_target_properties(pbe-plugin PROPERTIES
  OUTPUT_NAME ${PBE_PLUGIN_NAME}
  PREFIX ""
  C_VISIBILITY_PRESET hidden
  C_STANDARD 11
)
# Export only the format entry points (see cmake/exports.map).
if(APPLE)
  target_link_options(pbe-plugin PRIVATE
    "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports-macos.txt")
elseif(NOT WIN32)
  target_link_options(pbe-plugin PRIVATE
    "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports.map")
endif()

# Bundle layout. Both formats are laid out under ${CMAKE_BINARY_DIR}/plugins
# after each build, and installed under lib/vst3 and lib/clap.
# https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Format.html
set(plugins_dir "${CMAKE_BINARY_DIR}/plugins")
if(APPLE)
  set(plist "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
  configure_file(cmake/Info.plist.in "${plist}" @ONLY)
  foreach(fmt vst3 clap)
    set(bundle "${plugins_dir}/${PBE_PLUGIN_NAME}.${fmt}")
    add_custom_command(TARGET pbe-plugin POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/Contents/MacOS"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:pbe-plugin>" "${bundle}/Contents/MacOS/${PBE_PLUGIN_NAME}"
      COMMAND ${CMAKE_COMMAND} -E copy "${plist}" "${bundle}/Contents/Info.plist"
      COMMAND ${CMAKE_COMMAND} -E echo_append "BNDL????" > "${bundle}/Contents/PkgInfo")
    install(DIRECTORY "${bundle}" DESTINATION lib/${fmt} USE_SOURCE_PERMISSIONS)
  endforeach()
elseif(WIN32)
  set(vst3_arch "${CMAKE_SYSTEM_PROCESSOR}")
  string(TOLOWER "${vst3_arch}" vst3_arch)
  if(vst3_arch MATCHES "amd64|x86_64")
    set(vst3_arch "x86_64-win")
  elseif(vst3_arch MATCHES "arm64|aarch64")
    set(vst3_arch "arm64-win")
  else()
    set(vst3_arch "x86-win")
  endif()
  set(vst3_dir "${plugins_dir}/${PBE_PLUGIN_NAME}.vst3/Contents/${vst3_arch}")
  add_custom_command(TARGET pbe-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${vst3_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:pbe-plugin>" "${vst3_dir}/${PBE_PLUGIN_NAME}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:pbe-plugin>" "${plugins_dir}/${PBE_PLUGIN_NAME}.clap")
  install(FILES "$<TARGET_FILE:pbe-plugin>" DESTINATION "lib/vst3/${PBE_PLUGIN_NAME}.vst3/Contents/${vst3_arch}" RENAME "${PBE_PLUGIN_NAME}.vst3")
  install(FILES "$<TARGET_FILE:pbe-plugin>" DESTINATION lib/clap RENAME "${PBE_PLUGIN_NAME}.clap")
else()
  set(vst3_arch "${CMAKE_SYSTEM_PROCESSOR}-linux")
  set(vst3_dir "${plugins_dir}/${PBE_PLUGIN_NAME}.vst3/Contents/${vst3_arch}")
  add_custom_command(TARGET pbe-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${vst3_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:pbe-plugin>" "${vst3_dir}/${PBE_PLUGIN_NAME}.so"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:pbe-plugin>" "${plugins_dir}/${PBE_PLUGIN_NAME}.clap")
  install(FILES "$<TARGET_FILE:pbe-plugin>" DESTINATION "lib/vst3/${PBE_PLUGIN_NAME}.vst3/Contents/${vst3_arch}" RENAME "${PBE_PLUGIN_NAME}.so")
  install(FILES "$<TARGET_FILE:pbe-plugin>" DESTINATION lib/clap RENAME "${PBE_PLUGIN_NAME}.clap")
endif()

# ---- Host-less tools ---------------------------------------------------------
if(PBE_PLUGIN_TOOLS AND NOT WIN32)
  # Loads the built .clap like a host would and pushes audio through it.
  add_executable(pbe-clap-smoke tools/clap-smoke.c)
  target_include_directories(pbe-clap-smoke PRIVATE ${PBE_CPLUG_DIR}/src)
  target_link_libraries(pbe-clap-smoke PRIVATE ${CMAKE_DL_LIBS} m)
  add_dependencies(pbe-clap-smoke pbe-plugin)
  # Offline renderer for measurements (tools/analyze.py drives it).
  add_executable(pbe-render tools/render.c)
  target_include_directories(pbe-render PRIVATE ${PBE_CPLUG_DIR}/src)
  target_link_libraries(pbe-render PRIVATE ${CMAKE_DL_LIBS} m)
  add_dependencies(pbe-render pbe-plugin)

  # Same, through the VST3 wrapper.
  add_executable(pbe-vst3-smoke tools/vst3-smoke.c)
  target_include_directories(pbe-vst3-smoke PRIVATE ${PBE_CPLUG_DIR}/src)
  target_link_libraries(pbe-vst3-smoke PRIVATE ${CMAKE_DL_LIBS} m)
  add_dependencies(pbe-vst3-smoke pbe-plugin)
  if(BUILD_TESTING)
    add_test(NAME pbe-clap-smoke COMMAND pbe-clap-smoke "$<TARGET_FILE:pbe-plugin>")
    add_test(NAME pbe-vst3-smoke COMMAND pbe-vst3-smoke "$<TARGET_FILE:pbe-plugin>")
  endif()

  if(NOT APPLE)
    # Opens the editor in a plain X11 window: `pbe-gui-preview [seconds]`.
    add_executable(pbe-gui-preview tools/gui-preview.c)
    target_link_libraries(pbe-gui-preview PRIVATE pbe-plugin-core)

    # Plays a test tone through the plugin to the sound card, editor open:
    # `pbe-tone [--freq HZ] [--level DBFS]`. Needs ALSA.
    find_package(ALSA)
    if(ALSA_FOUND)
      add_executable(pbe-tone tools/tone.c)
      target_link_libraries(pbe-tone PRIVATE pbe-plugin-core ALSA::ALSA)
    else()
      message(STATUS "ALSA not found: pbe-tone will not be built")
    endif()
  endif()
endif()
