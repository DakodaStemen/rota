cmake_minimum_required(VERSION 3.16)
project(rotate_frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

option(ROTATE_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf QUIET)
if(NOT TARGET SDL2_ttf::SDL2_ttf)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
  add_library(SDL2_ttf::SDL2_ttf ALIAS PkgConfig::SDL2_TTF)
endif()
if(TARGET SDL2::SDL2)
  set(ROTATE_SDL SDL2::SDL2)
else()
  set(ROTATE_SDL ${SDL2_LIBRARIES})
endif()

# SDL-free core: config, library scan, swivel FSM/evdev/daemon, power, RetroArch control, menu model.
add_library(rotate_core STATIC
  src/util.cpp src/ini.cpp src/config.cpp src/library.cpp src/power.cpp src/sysinfo.cpp
  src/retroarch.cpp src/swivel.cpp src/swiveld.cpp src/system_actions.cpp src/client.cpp
  src/model.cpp src/touch.cpp src/png.cpp)
target_include_directories(rotate_core PUBLIC src)
target_link_libraries(rotate_core PUBLIC Threads::Threads ZLIB::ZLIB)

add_library(rotate_ui STATIC src/ui.cpp)
target_compile_definitions(rotate_ui PRIVATE ROTATE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(rotate_ui PUBLIC rotate_core ${ROTATE_SDL} SDL2_ttf::SDL2_ttf)

add_executable(rotate-frontend src/main.cpp)
target_link_libraries(rotate-frontend PRIVATE rotate_ui)
add_executable(rotate-swiveld src/swiveld_main.cpp)
target_link_libraries(rotate-swiveld PRIVATE rotate_core)
add_executable(rotate-shots src/shots.cpp)
target_link_libraries(rotate-shots PRIVATE rotate_ui)

include(GNUInstallDirs)
install(TARGETS rotate-frontend rotate-swiveld RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/rotate-power-helper scripts/swivel-down scripts/swivel-up DESTINATION lib/rotate)
install(FILES config/frontend.ini DESTINATION /etc/rotate)
install(DIRECTORY assets/fonts DESTINATION share/rotate)

if(ROTATE_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  set(T ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  foreach(t core swivel launch power)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE rotate_core)
    target_compile_definitions(test_${t} PRIVATE
      ROTATE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
      ROTATE_SWIVELD_BIN="$<TARGET_FILE:rotate-swiveld>")
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
  add_executable(test_uinput tests/test_uinput.cpp)
  target_link_libraries(test_uinput PRIVATE rotate_core)
  target_compile_definitions(test_uinput PRIVATE
    ROTATE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    ROTATE_SWIVELD_BIN="$<TARGET_FILE:rotate-swiveld>")
  add_test(NAME uinput_e2e COMMAND ${T}/run_uinput.sh $<TARGET_FILE:test_uinput>)
  set_tests_properties(uinput_e2e PROPERTIES SKIP_RETURN_CODE 77)

  add_test(NAME screenshots COMMAND rotate-shots --out ${CMAKE_CURRENT_SOURCE_DIR}/artifacts)
  set_tests_properties(screenshots PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME idle_cpu COMMAND ${T}/idle_cpu.sh $<TARGET_FILE:rotate-frontend>)
endif()
