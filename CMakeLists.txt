cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  # macOS 15+ SDK dropped AGL.framework (it's an empty stub on the system).
  # FindWrapOpenGL.cmake (shipped with Qt6) adds AGL unconditionally on macOS,
  # but has an early-return guard: "if(TARGET WrapOpenGL::WrapOpenGL) return()".
  # Pre-create the target with just OpenGL so that guard fires and AGL is skipped.
  if(APPLE AND NOT TARGET WrapOpenGL::WrapOpenGL)
    add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED GLOBAL)
    target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "-framework OpenGL")
    set(WrapOpenGL_FOUND ON)
  endif()
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  if(APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework CoreText" "-framework CoreFoundation")
  endif()
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/settings-dialog.hpp src/settings-dialog.cpp)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/tod-timer-source.hpp src/tod-timer-source.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# macOS: the cmake POST_BUILD rundir-copy runs before Xcode's built-in
# "Sign to Run Locally" step, so the rundir copy is never signed.
# Sign it explicitly here so the installed plugin can be loaded by OBS.
if(APPLE)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND codesign --sign - --force --deep "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}.plugin"
    COMMENT "Ad-hoc signing rundir/${CMAKE_PROJECT_NAME}.plugin"
    VERBATIM
  )
endif()
