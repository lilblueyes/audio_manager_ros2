#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "builtin_interfaces/msg/time.hpp"
#include "rclcpp/rclcpp.hpp"

#include "audio_manager_ros2/backend.hpp"
#include "audio_manager_ros2/config.hpp"
#include "audio_manager_ros2/msg/audio_event.hpp"
#include "audio_manager_ros2/msg/audio_status.hpp"
#include "audio_manager_ros2/srv/reload_config.hpp"
#include "audio_manager_ros2/srv/set_mode.hpp"
#include "audio_manager_ros2/srv/stop.hpp"

namespace audio_manager_ros2
{

class AudioServerNode : public rclcpp::Node
{
public:
  AudioServerNode();

private:
  struct ChannelState
  {
    std::shared_ptr<PlaybackHandle> handle;
    std::string event_id;
    std::string file;
    std::string layer;
    uint8_t priority{0};
    bool loop{false};
    bool virtual_active{false};
    rclcpp::Time virtual_until{0, 0, RCL_ROS_TIME};
  };

  void load_config_();
  std::string resolve_default_config_path_() const;

  void on_event_(const audio_manager_ros2::msg::AudioEvent::SharedPtr msg);

  bool should_accept_event_(const audio_manager_ros2::msg::AudioEvent & ev) const;
  bool cooldown_ok_(const std::string & event_id, int cooldown_ms, bool force);
  void mark_played_(const std::string & event_id);

  void stop_layer_(const std::string & layer);
  void stop_all_();
  void stop_music_();
  void stop_fx_();

  bool play_music_(const std::string & event_id, const std::string & file, bool loop, uint8_t priority, bool force);
  bool play_fx_(const std::string & event_id, const std::string & file, const std::string & layer, uint8_t priority, bool force);
  bool channel_active_(const ChannelState & channel) const;
  bool is_mute_mode_() const;

  void activate_ducking_();
  void maybe_release_ducking_();

  void cleanup_and_publish_();

  // ROS
  rclcpp::Subscription<audio_manager_ros2::msg::AudioEvent>::SharedPtr event_sub_;
  rclcpp::Publisher<audio_manager_ros2::msg::AudioStatus>::SharedPtr status_pub_;
  rclcpp::Service<audio_manager_ros2::srv::Stop>::SharedPtr stop_srv_;
  rclcpp::Service<audio_manager_ros2::srv::SetMode>::SharedPtr mode_srv_;
  rclcpp::Service<audio_manager_ros2::srv::ReloadConfig>::SharedPtr reload_srv_;
  rclcpp::TimerBase::SharedPtr timer_;

  mutable std::mutex mutex_;
  Config config_;
  std::unique_ptr<Backend> backend_;
  std::string mode_{"normal"};
  int mute_oneshot_hold_ms_{900};

  ChannelState music_;
  ChannelState fx_;

  bool music_ducked_by_gain_{false};
  bool resume_music_after_duck_{false};
  std::string resume_music_event_;
  std::string resume_music_file_;
  uint8_t resume_music_priority_{0};

  rclcpp::Time duck_until_{0, 0, RCL_ROS_TIME};
  std::unordered_map<std::string, rclcpp::Time> last_play_time_;

  std::string last_event_;
  builtin_interfaces::msg::Time last_event_stamp_{};
};

}  // namespace audio_manager_ros2
