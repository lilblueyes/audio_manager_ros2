cmake_minimum_required(VERSION 3.8)
project(audio_manager_ros2)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(yaml-cpp REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/AudioEvent.msg"
  "msg/AudioStatus.msg"
  "srv/Stop.srv"
  "srv/SetMode.srv"
  "srv/ReloadConfig.srv"
  DEPENDENCIES builtin_interfaces
)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")

add_library(audio_manager_ros2_lib SHARED
  src/core/config.cpp
  src/core/player_backend.cpp
  src/core/null_backend.cpp
  src/core/audio_server.cpp
)
target_include_directories(audio_manager_ros2_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(audio_manager_ros2_lib
  rclcpp
  ament_index_cpp
)
target_link_libraries(audio_manager_ros2_lib yaml-cpp)
add_dependencies(audio_manager_ros2_lib ${PROJECT_NAME}__rosidl_generator_cpp)
target_link_libraries(audio_manager_ros2_lib ${cpp_typesupport_target})

add_executable(audio_server_node src/audio_server_main.cpp)
ament_target_dependencies(audio_server_node rclcpp)
target_link_libraries(audio_server_node audio_manager_ros2_lib)
target_link_libraries(audio_server_node ${cpp_typesupport_target})

install(TARGETS audio_manager_ros2_lib
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(TARGETS audio_server_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
