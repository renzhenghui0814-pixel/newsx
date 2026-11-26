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

# Microsoft WebView2 SDK, fixed version and checksum; runtime uses installed Edge.
set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/webview2/sdk")
if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.tools/webview2")
    file(DOWNLOAD
        "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.2903.40/microsoft.web.webview2.1.0.2903.40.nupkg"
        "${CMAKE_CURRENT_SOURCE_DIR}/.tools/webview2/sdk.zip"
        EXPECTED_HASH SHA256=EF128016DD1E51C59178C827ED5B8AA3322C57AFA8675D930F8109505542AD74
        TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_SOURCE_DIR}/.tools/webview2/sdk.zip" DESTINATION "${WEBVIEW2_DIR}")
endif()

qt_add_executable(NewsXNative
    resources/newsx.rc
    src/main.cpp
    src/edge_hide_controller.cpp
    src/edge_hide_controller.h
    src/app_controller.cpp
    src/app_controller.h
    src/settings.cpp
    src/settings.h
    src/cache_store.cpp
    src/cache_store.h
    src/news_service.cpp
    src/news_service.h
    src/desktop_weibo.cpp
    src/desktop_weibo.h
    src/weibo_session.cpp
    src/weibo_session.h
    src/weibo_login_dialog.cpp
    src/weibo_login_dialog.h
    src/weibo_web_view.cpp
    src/weibo_web_view.h
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
    FILES resources/newsx.png resources/weibo.svg resources/xiaohongshu.svg
        resources/mode-normal.svg resources/mode-minimal.svg resources/mode-hidden.svg)
# Localized MSVC /showIncludes output can omit project headers from the
# scanner's dependency list. Never mix stale object layouts after header edits.
file(GLOB_RECURSE NEWSX_PROJECT_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
get_target_property(NEWSX_APP_SOURCES NewsXNative SOURCES)
foreach(NEWSX_APP_SOURCE IN LISTS NEWSX_APP_SOURCES)
    if(NEWSX_APP_SOURCE MATCHES "\\.cpp$")
        set_property(SOURCE "${NEWSX_APP_SOURCE}" APPEND PROPERTY OBJECT_DEPENDS "${NEWSX_PROJECT_HEADERS}")
    endif()
endforeach()
target_include_directories(NewsXNative PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(NEWSX_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/app" CACHE PATH "NewsX runtime output directory")
set_target_properties(NewsXNative PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${NEWSX_RUNTIME_OUTPUT_DIR}")
target_include_directories(NewsXNative PRIVATE "${TORA_DIR}")
target_include_directories(NewsXNative PRIVATE src "${WEBVIEW2_DIR}/build/native/include")
target_compile_definitions(NewsXNative PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(NewsXNative PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia Qt6::Widgets
    "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib" ole32 oleaut32 shlwapi version crypt32
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
        qml/components/ModeMenuItem.qml
        qml/components/VideoPlayer.qml
        qml/components/CircularAvatar.qml
        qml/components/ScrollBarIndicator.qml
        qml/components/WheelScroller.qml
        qml/components/WeiboWebsite.qml
        qml/components/XiaohongshuWebsite.qml
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

# Explicit opt-in live probe; excluded from normal builds and CTest.
add_executable(weibo_anonymous_probe EXCLUDE_FROM_ALL
    tests/weibo_anonymous_probe.cpp
    src/news_service.cpp src/news_service.h
    src/desktop_weibo.cpp src/desktop_weibo.h
    src/weibo_session.cpp src/weibo_session.h
    src/settings.cpp src/market/symbol.cpp)
target_include_directories(weibo_anonymous_probe PRIVATE src)
target_compile_definitions(weibo_anonymous_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(weibo_anonymous_probe PRIVATE Qt6::Gui Qt6::Network crypt32)

add_executable(weibo_desktop_probe EXCLUDE_FROM_ALL
    tests/weibo_desktop_probe.cpp
    src/desktop_weibo.cpp src/desktop_weibo.h
    src/news_service.cpp src/news_service.h
    src/weibo_session.cpp src/weibo_session.h
    src/settings.cpp src/market/symbol.cpp)
target_include_directories(weibo_desktop_probe PRIVATE src)
target_compile_definitions(weibo_desktop_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(weibo_desktop_probe PRIVATE Qt6::Gui Qt6::Network crypt32)
