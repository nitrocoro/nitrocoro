add_library(nitrocoro-https STATIC
    src/HttpsClient.cc
)

target_include_directories(nitrocoro-https
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(nitrocoro-https
    PUBLIC
        nitrocoro-http
        nitrocoro-tls
)

include(GNUInstallDirs)

install(TARGETS nitrocoro-https
    EXPORT nitrocoroTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/nitrocoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
