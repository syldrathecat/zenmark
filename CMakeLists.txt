cmake_minimum_required(VERSION 2.8.12)

project(zenmark)
add_executable(zenmark "main.cpp" "hwmon.c" "hwmon.h" "util.hpp")
set_property(TARGET zenmark PROPERTY CXX_STANDARD 17)
target_link_libraries(zenmark PRIVATE pthread)

add_custom_command(TARGET zenmark POST_BUILD
	COMMAND sudo setcap cap_sys_rawio=ep ${CMAKE_CURRENT_BINARY_DIR}/zenmark
)
