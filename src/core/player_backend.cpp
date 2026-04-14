#include "audio_manager_ros2/player_backend.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
extern char ** environ;

namespace audio_manager_ros2
{

PlayerBackend::PlayerBackend()
{
  player_ = detect_player_();
}

PlayerBackend::~PlayerBackend() = default;

bool PlayerBackend::command_exists_(const std::string & name) const
{
  const char * path_env = std::getenv("PATH");
  if (!path_env) return false;

  std::stringstream ss(path_env);
  std::string dir;
  while (std::getline(ss, dir, ':')) {
    const fs::path p = fs::path(dir) / name;
    if (fs::exists(p) && ::access(p.c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}

PlayerBackend::PlayerKind PlayerBackend::detect_player_() const
{
  if (command_exists_("gst-play-1.0")) return PlayerKind::GSTPLAY;
  if (command_exists_("ffplay")) return PlayerKind::FFPLAY;
  if (command_exists_("paplay")) return PlayerKind::PAPLAY;
  if (command_exists_("aplay")) return PlayerKind::APLAY;
  return PlayerKind::NONE;
}

std::vector<std::string> PlayerBackend::build_command_(
  PlayerKind kind, const std::string & file, bool loop, double gain) const
{
  (void)gain;  // Runtime gain updates are generally unsupported by these players.

  switch (kind) {
    case PlayerKind::GSTPLAY: {
      std::vector<std::string> cmd;
      cmd.push_back("gst-play-1.0");
      cmd.push_back("--quiet");
      cmd.push_back("--no-interactive");
      (void)loop;

      std::string uri = file;
      if (uri.rfind("file://", 0) != 0) {
        uri = "file://" + uri;
      }
      cmd.push_back(uri);
      return cmd;
    }
    case PlayerKind::FFPLAY: {
      std::vector<std::string> cmd = {
        "ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet"
      };
      if (loop) {
        cmd.push_back("-loop");
        cmd.push_back("-1");
      }
      cmd.push_back(file);
      return cmd;
    }
    case PlayerKind::PAPLAY:
      return {"paplay", file};
    case PlayerKind::APLAY:
      return {"aplay", "-q", file};
    default:
      return {};
  }
}

pid_t PlayerBackend::spawn_(const std::vector<std::string> & argv) const
{
  if (argv.empty()) return -1;

  std::vector<char *> cargv;
  cargv.reserve(argv.size() + 1);
  for (const auto & arg : argv) {
    cargv.push_back(const_cast<char *>(arg.c_str()));
  }
  cargv.push_back(nullptr);

  posix_spawnattr_t attr;
  if (::posix_spawnattr_init(&attr) != 0) {
    return -1;
  }

  posix_spawn_file_actions_t file_actions;
  if (::posix_spawn_file_actions_init(&file_actions) != 0) {
    (void)::posix_spawnattr_destroy(&attr);
    return -1;
  }
  (void)::posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  (void)::posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  (void)::posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

  sigset_t def_set;
  sigemptyset(&def_set);
  sigaddset(&def_set, SIGINT);
  sigaddset(&def_set, SIGTERM);
  (void)::posix_spawnattr_setsigdefault(&attr, &def_set);

  sigset_t mask_set;
  sigemptyset(&mask_set);
  (void)::posix_spawnattr_setsigmask(&attr, &mask_set);

  short flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP;
  (void)::posix_spawnattr_setflags(&attr, flags);
  (void)::posix_spawnattr_setpgroup(&attr, 0);

  pid_t pid = -1;
  const int rc = ::posix_spawnp(&pid, cargv[0], &file_actions, &attr, cargv.data(), environ);
  (void)::posix_spawn_file_actions_destroy(&file_actions);
  (void)::posix_spawnattr_destroy(&attr);
  if (rc != 0) {
    return -1;
  }

  return pid;
}

std::string PlayerBackend::backend_name() const
{
  switch (player_) {
    case PlayerKind::GSTPLAY:
      return "player_backend(gst-play-1.0)";
    case PlayerKind::FFPLAY:
      return "player_backend(ffplay)";
    case PlayerKind::PAPLAY:
      return "player_backend(paplay)";
    case PlayerKind::APLAY:
      return "player_backend(aplay)";
    default:
      return "player_backend(unavailable)";
  }
}

bool PlayerBackend::can_play_audio() const
{
  return player_ != PlayerKind::NONE;
}

std::shared_ptr<PlaybackHandle> PlayerBackend::play(const std::string & file, bool loop, double gain)
{
  if (!can_play_audio()) {
    throw std::runtime_error("No system audio player found (gst-play-1.0, ffplay, paplay, aplay).");
  }

  const bool native_loop = (player_ == PlayerKind::FFPLAY);
  const auto cmd = build_command_(player_, file, loop && native_loop, gain);
  const pid_t pid = spawn_(cmd);
  if (pid <= 0) {
    throw std::runtime_error("Failed to spawn player process.");
  }

  auto handle = std::make_shared<ProcessHandle>();
  handle->file = file;
  handle->loop = loop;
  handle->pid = pid;

  if (loop && !native_loop) {
    handle->watcher = std::thread([this, handle, gain]() {
      while (!handle->stop_requested.load()) {
        int status = 0;
        pid_t done = ::waitpid(handle->pid, &status, 0);
        if (done <= 0 || handle->stop_requested.load()) {
          break;
        }
        const auto restart_cmd = build_command_(player_, handle->file, false, gain);
        const pid_t new_pid = spawn_(restart_cmd);
        if (new_pid <= 0) {
          break;
        }
        handle->pid = new_pid;
      }
    });
  }

  return handle;
}

void PlayerBackend::stop(const std::shared_ptr<PlaybackHandle> & handle)
{
  auto proc = std::dynamic_pointer_cast<ProcessHandle>(handle);
  if (!proc) return;

  proc->stop_requested.store(true);

  if (proc->pid > 0) {
    const pid_t pid = proc->pid;
    (void)::kill(pid, SIGTERM);

    bool exited = false;
    for (int i = 0; i < 10; ++i) {
      int status = 0;
      const pid_t w = ::waitpid(pid, &status, WNOHANG);
      if (w == pid) {
        exited = true;
        break;
      }
      if (w < 0 && errno == ECHILD) {
        exited = true;
        break;
      }
      ::usleep(20000);
    }

    if (!exited) {
      errno = 0;
      if (::kill(pid, 0) == 0 || errno == EPERM) {
        (void)::kill(pid, SIGKILL);
      }
      int status = 0;
      (void)::waitpid(pid, &status, WNOHANG);
    }

    proc->pid = -1;
  }

  if (proc->watcher.joinable()) {
    proc->watcher.join();
  }
}

bool PlayerBackend::is_running(const std::shared_ptr<PlaybackHandle> & handle)
{
  auto proc = std::dynamic_pointer_cast<ProcessHandle>(handle);
  if (!proc || proc->pid <= 0) return false;

  if (proc->loop) {
    return !proc->stop_requested.load();
  }

  int status = 0;
  const pid_t w = ::waitpid(proc->pid, &status, WNOHANG);
  if (w == 0) {
    return true;
  }
  if (w == proc->pid) {
    proc->pid = -1;
    return false;
  }
  if (w < 0 && errno == ECHILD) {
    errno = 0;
    if (::kill(proc->pid, 0) == 0 || errno == EPERM) {
      return true;
    }
  }
  return false;
}

bool PlayerBackend::set_gain(const std::shared_ptr<PlaybackHandle> & handle, double gain)
{
  (void)handle;
  (void)gain;
  // System players selected here don't provide a portable runtime gain API.
  return false;
}

}  // namespace audio_manager_ros2
