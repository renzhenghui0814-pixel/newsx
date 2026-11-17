cmake_minimum_required(VERSION 3.24)
project(NewsXNative VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOGEN_PARALLEL 1)
enable_testing()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "NewsXNative requires 64-bit Windows")
endif()

set(TORA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/C++_API_v1.0.9_20250825")
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Multimedia Widgets)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(NewsXNative
    resources/newsx.rc
    src/main.cpp
    src/app_controller.cpp
    src/app_controller.h
    src/settings.cpp
    src/settings.h
    src/cache_store.cpp
    src/cache_store.h
    src/news_service.cpp
    src/news_service.h
    src/media_service.cpp
    src/media_service.h
    src/market/quote.h
    src/market/quote_store.cpp
    src/market/quote_store.h
    src/market/symbol.cpp
    src/market/symbol.h
    src/market/tora_feed.cpp
    src/market/tora_feed.h
)
qt_add_resources(NewsXNative "newsx_icon"
    PREFIX "/"
    FILES resources/newsx.png)
target_include_directories(NewsXNative PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(NEWSX_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/app" CACHE PATH "NewsX runtime output directory")
set_target_properties(NewsXNative PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${NEWSX_RUNTIME_OUTPUT_DIR}")
target_include_directories(NewsXNative PRIVATE "${TORA_DIR}")
target_link_libraries(NewsXNative PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia Qt6::Widgets
    "${TORA_DIR}/x64/xmdapi.lib")
if(MSVC)
    target_compile_options(NewsXNative PRIVATE /wd4828 /wd4819)
    target_link_options(NewsXNative PRIVATE /MAP)
endif()

qt_add_qml_module(NewsXNative
    URI NewsX
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/NewsCard.qml
        qml/components/StockRow.qml
        qml/components/HotRow.qml
        qml/components/HeadlineRow.qml
        qml/components/DetailHeader.qml
        qml/components/VideoPlayer.qml
        qml/components/CircularAvatar.qml
        qml/components/ScrollBarIndicator.qml
        qml/components/WheelScroller.qml
)

add_custom_command(TARGET NewsXNative POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${TORA_DIR}/x64/xmdapi.dll" "$<TARGET_FILE_DIR:NewsXNative>")

add_executable(market_tests
    tests/market_tests.cpp
    src/market/quote_store.cpp
    src/market/symbol.cpp)
target_include_directories(market_tests PRIVATE src)
add_test(NAME market_tests COMMAND market_tests)
