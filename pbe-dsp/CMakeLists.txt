add_library(pbe-dsp STATIC
  pbe-dsp.c
  cross-over.c
  full-wave-integrator.c
  band-pass.c
  select-columns.c
  concat-columns.c
)
target_include_directories(pbe-dsp PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/pbe-dsp>
)
target_link_libraries(pbe-dsp PUBLIC m)

include(GNUInstallDirs)
install(TARGETS pbe-dsp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  pbe-dsp.h
  cross-over.h
  full-wave-integrator.h
  band-pass.h
  select-columns.h
  concat-columns.h
  fixed-math.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pbe-dsp
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
