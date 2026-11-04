# Installation (user-level, no root needed):
#
#   cmake --install build/clang-release --prefix ~/.local --strip
#   cmake --build build/clang-release --target uninstall   # removes it again
#
# Layout:
#   bin/woodyn, bin/woodyn-bench
#   share/woodyn/shaders/*.spv, share/woodyn/assets/...
#   share/applications/woodyn.desktop, share/icons/hicolor/scalable/apps/woodyn.svg
#   share/doc/woodyn/ (README, LICENSE, third-party notices)

set(WOODYN_DATA_DESTINATION ${CMAKE_INSTALL_DATADIR}/woodyn)

# Executables get short command names; `--strip` strips them like install(TARGETS) would.
function(woodyn_install_program target name)
    install(PROGRAMS $<TARGET_FILE:${target}> DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME ${name})
    install(CODE "
        if(CMAKE_INSTALL_DO_STRIP AND NOT \"${CMAKE_STRIP}\" STREQUAL \"\")
            execute_process(COMMAND \"${CMAKE_STRIP}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${name}\")
        endif()")
endfunction()

woodyn_install_program(woodyn_bench woodyn-bench)

if(WOODYN_BUILD_VIEWER)
    woodyn_install_program(woodyn_viewer woodyn)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION ${WOODYN_DATA_DESTINATION}/shaders
            FILES_MATCHING PATTERN "*.spv")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION ${WOODYN_DATA_DESTINATION}/assets)

    # Desktop entry with the absolute path of the installed executable (resolved
    # at install time, so `--prefix` is honored).
    install(CODE "
        set(bin \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/woodyn\")
        configure_file(\"${CMAKE_SOURCE_DIR}/packaging/woodyn.desktop.in\"
                       \"${CMAKE_BINARY_DIR}/packaging/woodyn.desktop\" @ONLY)")
    install(FILES ${CMAKE_BINARY_DIR}/packaging/woodyn.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/woodyn.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/THIRD_PARTY.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Uninstall: removes exactly the files listed in install_manifest.txt.
if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/Uninstall.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Removing the files of the last install"
        VERBATIM)
endif()
