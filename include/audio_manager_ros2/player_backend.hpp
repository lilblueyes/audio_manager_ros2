#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio_manager_ros2/backend.hpp"

namespace audio_manager_ros2
{

class PlayerBackend : public Backend
{
public:
  PlayerBackend();
  ~PlayerBackend() override;

  std::string backend_name() const override;
  bool can_play_audio() const override;

  std::shared_ptr<PlaybackHandle> play(const std::string & file, bool loop, double gain) override;
  void stop(const std::shared_ptr<PlaybackHandle> & handle) override;

  bool is_running(const std::shared_ptr<PlaybackHandle> & handle) override;
  bool set_gain(const std::shared_ptr<PlaybackHandle> & handle, double gain) override;

private:
  enum class PlayerKind { GSTPLAY, FFPLAY, PAPLAY, APLAY, NONE };

  struct ProcessHandle : public PlaybackHandle
  {
    pid_t pid{-1};
    std::atomic<bool> stop_requested{false};
    std::thread watcher;
  };

  bool command_exists_(const std::string & name) const;
  PlayerKind detect_player_() const;
  std::vector<std::string> build_command_(PlayerKind kind, const std::string & file, bool loop, double gain) const;
  pid_t spawn_(const std::vector<std::string> & argv) const;

  PlayerKind player_{PlayerKind::NONE};
};

}  // namespace audio_manager_ros2
