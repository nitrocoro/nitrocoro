add_library(nitrocoro-websocket STATIC
    src/WsConnection.cc
    src/WsContextImpl.cc
    src/WsServer.cc
    src/WsRouter.cc
)

target_include_directories(nitrocoro-websocket PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nitrocoro-websocket PUBLIC
    nitrocoro-http
)

include(GNUInstallDirs)

install(TARGETS nitrocoro-websocket
    EXPORT nitrocoroTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/nitrocoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

option(BUILD_TESTING "Build tests" ON)
if (BUILD_TESTING)
    enable_testing()
    add_executable(ws_test tests/ws_test.cc)
    target_link_libraries(ws_test PRIVATE nitrocoro-websocket)
    add_test(NAME ws_test COMMAND ws_test)
endif ()
