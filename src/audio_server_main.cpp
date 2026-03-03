#include "audio_manager_ros2/audio_server.hpp"

#include "rclcpp/rclcpp.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<audio_manager_ros2::AudioServerNode>());
  rclcpp::shutdown();
  return 0;
}
