add_library(nesso_platform assert.cpp)
target_include_directories(nesso_platform
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nesso_platform PRIVATE nesso_options)
if(WIN32)
  target_sources(nesso_platform PRIVATE file_win32.cpp file_stamp_win32.cpp
                                        terminal_win32.cpp)
  target_link_libraries(nesso_platform PRIVATE Psapi)
else()
  target_sources(nesso_platform PRIVATE file_posix.cpp file_stamp_posix.cpp
                                        terminal_posix.cpp)
endif()
