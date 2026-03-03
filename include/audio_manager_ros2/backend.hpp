#pragma once

#include <memory>
#include <string>

namespace audio_manager_ros2
{

struct PlaybackHandle
{
  virtual ~PlaybackHandle() = default;
  std::string file;
  bool loop{false};
};

class Backend
{
public:
  virtual ~Backend() = default;

  virtual std::string backend_name() const = 0;
  virtual bool can_play_audio() const = 0;

  virtual std::shared_ptr<PlaybackHandle> play(const std::string & file, bool loop, double gain) = 0;
  virtual void stop(const std::shared_ptr<PlaybackHandle> & handle) = 0;

  virtual bool is_running(const std::shared_ptr<PlaybackHandle> & handle) = 0;
  virtual bool set_gain(const std::shared_ptr<PlaybackHandle> & handle, double gain) = 0;
};

}  // namespace audio_manager_ros2
