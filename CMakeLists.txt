cmake_minimum_required(VERSION 3.16)
project(notification-tray VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(ENABLE_TESTING "Enable unit tests" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Multimedia DBus Test Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(GLIB REQUIRED glib-2.0)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DBUS_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

# Library sources (for testing, excludes main.cpp)
set(LIB_SOURCES
    src/system_tray_file_browser.cpp
    src/notification_service.cpp
    src/notification_cacher.cpp
    src/notifier.cpp
    src/tray.cpp
    src/notification_widget.cpp
    src/notification_timer.cpp
    src/utils/settings.cpp
    src/utils/paths.cpp
    src/utils/logging.cpp
)

set(HEADERS
    include/system_tray_file_browser.h
    include/notification_service.h
    include/notification_cacher.h
    include/notifier.h
    include/tray.h
    include/notification_widget.h
    include/notification_timer.h
    include/notification_types.h
    include/utils/settings.h
    include/utils/paths.h
    include/utils/logging.h
)

set(UI_FILES
    src/notification_widget.ui
)

# Coverage flags
if(ENABLE_COVERAGE)
    set(COVERAGE_FLAGS "-fprofile-arcs -ftest-coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
endif()

# Sanitizer flags
if(ENABLE_SANITIZERS)
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}")
endif()

# Create a static library for testing
add_library(notification-tray-lib STATIC ${LIB_SOURCES} ${HEADERS} ${UI_FILES})
target_link_libraries(notification-tray-lib
    Qt5::Core
    Qt5::Widgets
    Qt5::Multimedia
    Qt5::DBus
    Qt5::Qml
    ${DBUS_LIBRARIES}
    ${GLIB_LIBRARIES}
)

# Main executable
add_executable(notification-tray src/main.cpp)
target_link_libraries(notification-tray notification-tray-lib)

install(TARGETS notification-tray DESTINATION bin)
install(FILES notification-tray.service DESTINATION lib/systemd/user)

# Testing configuration
if(ENABLE_TESTING)
    enable_testing()
    find_package(GTest QUIET)

    if(GTest_FOUND)
        message(STATUS "Google Test found, enabling unit tests")

        set(TEST_SOURCES
            tests/test_main.cpp
            tests/test_notification_timer.cpp
            tests/test_settings.cpp
            tests/test_paths.cpp
            tests/test_logging.cpp
            tests/test_notification_types.cpp
            tests/test_notification_cacher.cpp
            tests/test_notification_service.cpp
            tests/test_notifier.cpp
        )

        add_executable(notification-tray-tests ${TEST_SOURCES})
        target_include_directories(notification-tray-tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_link_libraries(notification-tray-tests
            notification-tray-lib
            GTest::gtest
            GTest::gmock
            Qt5::Test
        )

        include(GoogleTest)
        gtest_discover_tests(notification-tray-tests)

        # Coverage target
        if(ENABLE_COVERAGE)
            find_program(LCOV lcov)
            find_program(GENHTML genhtml)

            if(LCOV AND GENHTML)
                add_custom_target(coverage
                    COMMAND ${LCOV} --directory . --zerocounters
                    COMMAND $<TARGET_FILE:notification-tray-tests>
                    COMMAND ${LCOV} --directory . --capture --output-file coverage.info
                    COMMAND ${LCOV} --remove coverage.info '/usr/*' '*/tests/*' '*/Qt*' --output-file coverage.info.cleaned
                    COMMAND ${GENHTML} coverage.info.cleaned --output-directory coverage_report
                    COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in coverage_report/index.html"
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                    DEPENDS notification-tray-tests
                    COMMENT "Generating code coverage report"
                )
            else()
                message(WARNING "lcov or genhtml not found, coverage target disabled")
            endif()
        endif()
    else()
        message(STATUS "Google Test not found. Install with: sudo pacman -S gtest (Arch) or sudo apt install libgtest-dev (Debian)")
    endif()
endif()

# Clang-format target
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format on all source files"
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking code formatting with clang-format"
    )
endif()

# Clang-tidy target
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY})

    add_custom_target(tidy
        COMMAND ${CLANG_TIDY}
            -p ${CMAKE_BINARY_DIR}
            ${LIB_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy on source files"
    )
endif()

# Cppcheck target
find_program(CPPCHECK cppcheck)
if(CPPCHECK)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK}
            --enable=all
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --std=c++20
            -I ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Running cppcheck on source files"
    )
endif()

# Combined analysis target
add_custom_target(analyze
    COMMENT "Running all static analysis tools"
)

if(CLANG_FORMAT)
    add_dependencies(analyze format-check)
endif()

if(CPPCHECK)
    add_dependencies(analyze cppcheck)
endif()

# CI target - runs everything needed to pass CI
add_custom_target(ci
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --parallel
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure --parallel
    COMMENT "Running full CI suite: build, test, and analysis"
)

add_dependencies(ci analyze)

if(TARGET notification-tray-tests)
    add_dependencies(ci notification-tray-tests)
endif()
