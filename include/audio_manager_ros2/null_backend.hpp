#pragma once

#include <memory>

#include "audio_manager_ros2/backend.hpp"

namespace audio_manager_ros2
{

class NullBackend : public Backend
{
public:
  std::string backend_name() const override;
  bool can_play_audio() const override;

  std::shared_ptr<PlaybackHandle> play(const std::string & file, bool loop, double gain) override;
  void stop(const std::shared_ptr<PlaybackHandle> & handle) override;

  bool is_running(const std::shared_ptr<PlaybackHandle> & handle) override;
  bool set_gain(const std::shared_ptr<PlaybackHandle> & handle, double gain) override;
};

}  // namespace audio_manager_ros2
