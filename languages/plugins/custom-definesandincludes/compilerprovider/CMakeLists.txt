include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

set( compilerprovider_SRCS
        compilerprovider.cpp
        icompiler.cpp
        gcclikecompiler.cpp
        msvccompiler.cpp
        compilerfactories.cpp
        settingsmanager.cpp
        widget/compilersmodel.cpp
        widget/compilerswidget.cpp
        ${kdevdefinesandincludesmanager_LOG_SRCS}
    )

ki18n_wrap_ui(compilerprovider_SRCS
    widget/compilerswidget.ui
)

add_library(kdevcompilerprovider STATIC
        ${compilerprovider_SRCS})
target_link_libraries( kdevcompilerprovider LINK_PRIVATE
        KDev::Project
        KDev::Util
        KDev::Language )
set_target_properties(kdevcompilerprovider PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(BUILD_kdev_msvcdefinehelper "Build the msvcdefinehelper tool for retrieving msvc standard macro definitions" OFF)

if(BUILD_kdev_msvcdefinehelper)
    set(kdevmsvcdefinehelper_SRCS msvcdefinehelper.cpp)
    add_executable(kdevmsvcdefinehelper ${kdevmsvcdefinehelper_SRCS})
    install(TARGETS kdevmsvcdefinehelper RUNTIME DESTINATION bin
                                         LIBRARY DESTINATION lib)
endif()

add_subdirectory(tests)
