cmake_minimum_required(VERSION 3.10)

project(TTXMCP)

set(SRC_FILES
  ttxmcp.c
)

add_library(${PROJECT_NAME} SHARED ${SRC_FILES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/teraterm/common
  ${CMAKE_SOURCE_DIR}/teraterm/teraterm
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
  WIN32
  _WINDOWS
  _USRDLL
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
  ws2_32
  user32
)

# Output name
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
