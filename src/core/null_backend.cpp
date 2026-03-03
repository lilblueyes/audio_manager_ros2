#include "audio_manager_ros2/null_backend.hpp"

namespace audio_manager_ros2
{

std::string NullBackend::backend_name() const
{
  return "null_backend";
}

bool NullBackend::can_play_audio() const
{
  return false;
}

std::shared_ptr<PlaybackHandle> NullBackend::play(const std::string & file, bool loop, double gain)
{
  (void)gain;
  auto handle = std::make_shared<PlaybackHandle>();
  handle->file = file;
  handle->loop = loop;
  return handle;
}

void NullBackend::stop(const std::shared_ptr<PlaybackHandle> & handle)
{
  (void)handle;
}

bool NullBackend::is_running(const std::shared_ptr<PlaybackHandle> & handle)
{
  (void)handle;
  return false;
}

bool NullBackend::set_gain(const std::shared_ptr<PlaybackHandle> & handle, double gain)
{
  (void)handle;
  (void)gain;
  return false;
}

}  // namespace audio_manager_ros2
