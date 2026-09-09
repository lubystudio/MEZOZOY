cmake_minimum_required(VERSION 3.20)
project(Mezozoy VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

file(GLOB_RECURSE MEZOZOY_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(Mezozoy WIN32 ${MEZOZOY_SOURCES} Mezozoy.rc)
if(MINGW)
  set_source_files_properties(Mezozoy.rc PROPERTIES COMPILE_FLAGS "--codepage=65001")
endif()
target_link_libraries(Mezozoy PRIVATE
  comctl32 comdlg32 d2d1 dwrite dwmapi gdi32 gdiplus ole32 shell32 shlwapi user32 uxtheme winhttp winspool
)
if(MINGW)
  target_link_options(Mezozoy PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

add_executable(MezozoyCaptureWindow EXCLUDE_FROM_ALL tests/capture-window.cpp)
target_link_libraries(MezozoyCaptureWindow PRIVATE gdi32 gdiplus user32)
