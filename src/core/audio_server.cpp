#include "audio_manager_ros2/audio_server.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>
#include <stdexcept>

#include "ament_index_cpp/get_package_share_directory.hpp"

#include "audio_manager_ros2/null_backend.hpp"
#include "audio_manager_ros2/player_backend.hpp"

namespace audio_manager_ros2
{

namespace
{

std::unique_ptr<Backend> create_backend(const std::string & requested)
{
  if (requested == "null") {
    return std::make_unique<NullBackend>();
  }

  auto player = std::make_unique<PlayerBackend>();

  if (requested == "player") {
    if (!player->can_play_audio()) {
      throw std::runtime_error("Requested backend=player, but no player executable is available.");
    }
    return player;
  }

  // auto fallback
  if (player->can_play_audio()) {
    return player;
  }
  return std::make_unique<NullBackend>();
}

uint8_t clamp_priority(int p)
{
  if (p < 0) return 0;
  if (p > 255) return 255;
  return static_cast<uint8_t>(p);
}

const char * yesno(bool value)
{
  return value ? "yes" : "no";
}

std::string short_file(const std::string & path)
{
  try {
    return std::filesystem::path(path).filename().string();
  } catch (...) {
    return path;
  }
}

}  // namespace

AudioServerNode::AudioServerNode()
: rclcpp::Node("audio_server")
{
  declare_parameter<std::string>("config_file", resolve_default_config_path_());
  declare_parameter<std::string>("mode", "normal");
  declare_parameter<std::string>("backend", "auto");
  declare_parameter<int>("mute_oneshot_hold_ms", 900);

  mode_ = get_parameter("mode").as_string();
  mute_oneshot_hold_ms_ = get_parameter("mute_oneshot_hold_ms").as_int();
  backend_ = create_backend(get_parameter("backend").as_string());
  load_config_();

  event_sub_ = create_subscription<audio_manager_ros2::msg::AudioEvent>(
    "audio/event",
    rclcpp::QoS(10).reliable(),
    std::bind(&AudioServerNode::on_event_, this, std::placeholders::_1));

  status_pub_ = create_publisher<audio_manager_ros2::msg::AudioStatus>(
    "audio/status", rclcpp::QoS(10).reliable());

  stop_srv_ = create_service<audio_manager_ros2::srv::Stop>(
    "audio/stop",
    [this](
      const std::shared_ptr<audio_manager_ros2::srv::Stop::Request> req,
      std::shared_ptr<audio_manager_ros2::srv::Stop::Response> resp)
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (req->layer.empty()) {
        stop_all_();
        resp->ok = true;
        resp->message = "Stopped all channels.";
      } else {
        stop_layer_(req->layer);
        resp->ok = true;
        resp->message = "Stopped layer: " + req->layer;
      }
    });

  mode_srv_ = create_service<audio_manager_ros2::srv::SetMode>(
    "audio/set_mode",
    [this](
      const std::shared_ptr<audio_manager_ros2::srv::SetMode::Request> req,
      std::shared_ptr<audio_manager_ros2::srv::SetMode::Response> resp)
    {
      std::lock_guard<std::mutex> lk(mutex_);
      mode_ = req->mode;
      if (is_mute_mode_()) {
        stop_all_();
      }
      resp->ok = true;
      resp->message = "Mode set to: " + mode_;
    });

  reload_srv_ = create_service<audio_manager_ros2::srv::ReloadConfig>(
    "audio/reload_config",
    [this](
      const std::shared_ptr<audio_manager_ros2::srv::ReloadConfig::Request>,
      std::shared_ptr<audio_manager_ros2::srv::ReloadConfig::Response> resp)
    {
      try {
        load_config_();
        resp->ok = true;
        resp->message = "Config reloaded.";
      } catch (const std::exception & e) {
        resp->ok = false;
        resp->message = std::string("Reload failed: ") + e.what();
      }
    });

  timer_ = create_wall_timer(
    std::chrono::milliseconds(200),
    std::bind(&AudioServerNode::cleanup_and_publish_, this));

  RCLCPP_INFO(
    get_logger(),
    "audio_server ready. backend=%s config=%s mode=%s",
    backend_->backend_name().c_str(),
    get_parameter("config_file").as_string().c_str(),
    mode_.c_str());
}

std::string AudioServerNode::resolve_default_config_path_() const
{
  const auto share = ament_index_cpp::get_package_share_directory("audio_manager_ros2");
  return share + "/config/default.yaml";
}

void AudioServerNode::load_config_()
{
  const auto cfg_path = get_parameter("config_file").as_string();
  Config cfg = Config::LoadFromFile(cfg_path);

  std::lock_guard<std::mutex> lk(mutex_);
  config_ = std::move(cfg);

  RCLCPP_INFO(
    get_logger(),
    "config loaded: %s (events=%zu, layers=%zu, ducking=%s hold_ms=%d)",
    cfg_path.c_str(),
    config_.events.size(),
    config_.layers.size(),
    yesno(config_.ducking.enabled),
    config_.ducking.hold_ms);
}

bool AudioServerNode::should_accept_event_(const audio_manager_ros2::msg::AudioEvent & ev) const
{
  if (mode_ == "silent") {
    if (ev.force) return true;
    if (ev.priority >= 200) return true;
    return false;
  }
  return true;
}

bool AudioServerNode::is_mute_mode_() const
{
  return mode_ == "mute";
}

bool AudioServerNode::channel_active_(const ChannelState & channel) const
{
  if (channel.handle && backend_->is_running(channel.handle)) {
    return true;
  }

  if (!channel.virtual_active) {
    return false;
  }

  if (channel.loop) {
    return true;
  }

  return now() < channel.virtual_until;
}

bool AudioServerNode::cooldown_ok_(const std::string & event_id, int cooldown_ms, bool force)
{
  if (force || cooldown_ms <= 0) return true;

  const auto it = last_play_time_.find(event_id);
  if (it == last_play_time_.end()) return true;

  const double dt_ms = (now() - it->second).seconds() * 1000.0;
  return dt_ms >= static_cast<double>(cooldown_ms);
}

void AudioServerNode::mark_played_(const std::string & event_id)
{
  last_play_time_[event_id] = now();
}

void AudioServerNode::stop_music_()
{
  if (music_.handle) {
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "music stop: event=%s file=%s",
        music_.event_id.c_str(),
        short_file(music_.file).c_str());
    }
    backend_->stop(music_.handle);
  }
  music_.virtual_active = false;
  music_.virtual_until = rclcpp::Time(0, 0, RCL_ROS_TIME);
  music_ = ChannelState{};
  music_ducked_by_gain_ = false;
}

void AudioServerNode::stop_fx_()
{
  if (fx_.handle) {
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "fx stop: layer=%s event=%s file=%s",
        fx_.layer.c_str(),
        fx_.event_id.c_str(),
        short_file(fx_.file).c_str());
    }
    backend_->stop(fx_.handle);
  }
  fx_.virtual_active = false;
  fx_.virtual_until = rclcpp::Time(0, 0, RCL_ROS_TIME);
  fx_ = ChannelState{};
}

void AudioServerNode::stop_layer_(const std::string & layer)
{
  if (layer == "music") {
    stop_music_();
    return;
  }

  if (layer == "sfx" || layer == "alerts") {
    stop_fx_();
    return;
  }
}

void AudioServerNode::stop_all_()
{
  stop_music_();
  stop_fx_();
  resume_music_after_duck_ = false;
  resume_music_event_.clear();
  resume_music_file_.clear();
  resume_music_priority_ = 0;
}

bool AudioServerNode::play_music_(
  const std::string & event_id,
  const std::string & file,
  bool loop,
  uint8_t priority,
  bool force)
{
  if (channel_active_(music_)) {
    if (!force && priority < music_.priority) {
      if (mode_ == "debug") {
        RCLCPP_INFO(
          get_logger(),
          "music skip: event=%s prio=%u < current_prio=%u (force=%s)",
          event_id.c_str(),
          priority,
          music_.priority,
          yesno(force));
      }
      return false;
    }
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "music preempt: current_event=%s current_prio=%u -> new_event=%s new_prio=%u (force=%s)",
        music_.event_id.c_str(),
        music_.priority,
        event_id.c_str(),
        priority,
        yesno(force));
    }
    stop_music_();
  }

  const double gain = config_.master_gain * config_.layers["music"].gain;
  if (is_mute_mode_()) {
    music_.handle.reset();
    music_.virtual_active = true;
    music_.virtual_until = rclcpp::Time(0, 0, RCL_ROS_TIME);
  } else {
    music_.handle = backend_->play(file, loop, gain);
    music_.virtual_active = false;
    music_.virtual_until = rclcpp::Time(0, 0, RCL_ROS_TIME);
  }
  music_.event_id = event_id;
  music_.file = file;
  music_.layer = "music";
  music_.priority = priority;
  music_.loop = loop;
  if (mode_ == "debug") {
    RCLCPP_INFO(
      get_logger(),
      "music play: event=%s file=%s loop=%s prio=%u gain=%.3f backend=%s",
      event_id.c_str(),
      short_file(file).c_str(),
      yesno(loop),
      priority,
      gain,
      backend_->backend_name().c_str());
  }
  return true;
}

bool AudioServerNode::play_fx_(
  const std::string & event_id,
  const std::string & file,
  const std::string & layer,
  uint8_t priority,
  bool force)
{
  if (channel_active_(fx_)) {
    if (!force && priority < fx_.priority) {
      if (mode_ == "debug") {
        RCLCPP_INFO(
          get_logger(),
          "fx skip: layer=%s event=%s prio=%u < current_prio=%u (force=%s)",
          layer.c_str(),
          event_id.c_str(),
          priority,
          fx_.priority,
          yesno(force));
      }
      return false;
    }
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "fx preempt: current_layer=%s current_event=%s current_prio=%u -> new_layer=%s new_event=%s new_prio=%u (force=%s)",
        fx_.layer.c_str(),
        fx_.event_id.c_str(),
        fx_.priority,
        layer.c_str(),
        event_id.c_str(),
        priority,
        yesno(force));
    }
    stop_fx_();
  }

  activate_ducking_();

  const auto layer_it = config_.layers.find(layer);
  const double layer_gain = (layer_it != config_.layers.end()) ? layer_it->second.gain : 1.0;
  const double gain = config_.master_gain * layer_gain;

  if (is_mute_mode_()) {
    fx_.handle.reset();
    fx_.virtual_active = true;
    const double hold_s = static_cast<double>(std::max(50, mute_oneshot_hold_ms_)) / 1000.0;
    fx_.virtual_until = now() + rclcpp::Duration::from_seconds(hold_s);
  } else {
    fx_.handle = backend_->play(file, false, gain);
    fx_.virtual_active = false;
    fx_.virtual_until = rclcpp::Time(0, 0, RCL_ROS_TIME);
  }
  fx_.event_id = event_id;
  fx_.file = file;
  fx_.layer = layer;
  fx_.priority = priority;
  fx_.loop = false;
  if (mode_ == "debug") {
    RCLCPP_INFO(
      get_logger(),
      "fx play: layer=%s event=%s file=%s prio=%u gain=%.3f backend=%s",
      layer.c_str(),
      event_id.c_str(),
      short_file(file).c_str(),
      priority,
      gain,
      backend_->backend_name().c_str());
  }
  return true;
}

void AudioServerNode::activate_ducking_()
{
  if (is_mute_mode_()) {
    return;
  }

  if (!config_.ducking.enabled) {
    return;
  }

  duck_until_ = now() + rclcpp::Duration::from_seconds(static_cast<double>(config_.ducking.hold_ms) / 1000.0);

  if (!music_.handle || !backend_->is_running(music_.handle)) {
    return;
  }

  const double full_music_gain = config_.master_gain * config_.layers["music"].gain;
  const double duck_gain = full_music_gain * config_.ducking.music_gain;

  if (backend_->set_gain(music_.handle, duck_gain)) {
    music_ducked_by_gain_ = true;
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "ducking applied by gain: music_event=%s gain=%.3f->%.3f hold_ms=%d",
        music_.event_id.c_str(),
        full_music_gain,
        duck_gain,
        config_.ducking.hold_ms);
    }
    return;
  }

  return;
}

void AudioServerNode::maybe_release_ducking_()
{
  if (is_mute_mode_()) {
    return;
  }

  if (!config_.ducking.enabled) {
    return;
  }

  if (duck_until_.nanoseconds() == 0) {
    return;
  }

  if (now() < duck_until_) {
    return;
  }

  if (fx_.handle && backend_->is_running(fx_.handle)) {
    return;
  }

  if (music_ducked_by_gain_ && music_.handle && backend_->is_running(music_.handle)) {
    const double full_music_gain = config_.master_gain * config_.layers["music"].gain;
    (void)backend_->set_gain(music_.handle, full_music_gain);
    music_ducked_by_gain_ = false;
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "ducking released by gain restore: music_event=%s gain=%.3f",
        music_.event_id.c_str(),
        full_music_gain);
    }
  }

  if (resume_music_after_duck_ && !resume_music_file_.empty() && (!music_.handle || !backend_->is_running(music_.handle))) {
    try {
      const double gain = config_.master_gain * config_.layers["music"].gain;
      music_.handle = backend_->play(resume_music_file_, true, gain);
      music_.event_id = resume_music_event_;
      music_.file = resume_music_file_;
      music_.layer = "music";
      music_.priority = resume_music_priority_;
      music_.loop = true;
      if (mode_ == "debug") {
        RCLCPP_INFO(
          get_logger(),
          "ducking resume music: event=%s file=%s prio=%u",
          music_.event_id.c_str(),
          short_file(music_.file).c_str(),
          music_.priority);
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "Failed to resume ducked music: %s", e.what());
    }
  }

  resume_music_after_duck_ = false;
  resume_music_event_.clear();
  resume_music_file_.clear();
  resume_music_priority_ = 0;
  duck_until_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
}

void AudioServerNode::on_event_(const audio_manager_ros2::msg::AudioEvent::SharedPtr msg)
{
  if (!msg) return;

  std::lock_guard<std::mutex> lk(mutex_);

  last_event_ = msg->event_id;
  last_event_stamp_ = msg->stamp;

  if (mode_ == "debug") {
    RCLCPP_INFO(
      get_logger(),
      "event rx: id=%s layer=%s prio=%u stop=%s force=%s source=%s",
      msg->event_id.c_str(),
      msg->layer.c_str(),
      msg->priority,
      yesno(msg->stop),
      yesno(msg->force),
      msg->source.c_str());
  }

  if (!should_accept_event_(*msg)) {
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "event skip: id=%s reason=mode_policy mode=%s layer=%s prio=%u force=%s",
        msg->event_id.c_str(),
        mode_.c_str(),
        msg->layer.c_str(),
        msg->priority,
        yesno(msg->force));
    }
    return;
  }

  if (msg->stop) {
    if (mode_ == "debug") {
      RCLCPP_INFO(
        get_logger(),
        "event stop request: layer=%s force=%s",
        msg->layer.c_str(),
        yesno(msg->force));
    }
    if (msg->layer.empty()) {
      stop_all_();
    } else {
      stop_layer_(msg->layer);
    }
    return;
  }

  const auto evt_it = config_.events.find(msg->event_id);
  if (evt_it == config_.events.end()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "Unknown event_id='%s'", msg->event_id.c_str());
    return;
  }

  const EventConfig & ec = evt_it->second;
  const std::string layer = msg->layer.empty() ? ec.layer : msg->layer;
  const uint8_t priority = msg->priority > 0 ? msg->priority : clamp_priority(ec.priority);

  if (!cooldown_ok_(msg->event_id, ec.cooldown_ms, msg->force)) {
    if (mode_ == "debug") {
      double remaining_ms = static_cast<double>(ec.cooldown_ms);
      const auto it = last_play_time_.find(msg->event_id);
      if (it != last_play_time_.end()) {
        const double dt_ms = (now() - it->second).seconds() * 1000.0;
        remaining_ms = std::max(0.0, static_cast<double>(ec.cooldown_ms) - dt_ms);
      }
      RCLCPP_INFO(
        get_logger(),
        "event skip: id=%s reason=cooldown remaining_ms=%.1f force=%s",
        msg->event_id.c_str(),
        remaining_ms,
        yesno(msg->force));
    }
    return;
  }

  if (ec.variants.empty()) {
    return;
  }

  std::vector<double> weights;
  weights.reserve(ec.variants.size());
  for (const auto & v : ec.variants) {
    weights.push_back(std::max(0.0, v.weight));
  }
  std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
  static thread_local std::mt19937 rng{std::random_device{}()};
  const size_t idx = dist(rng);
  const Variant & var = ec.variants[std::min(idx, ec.variants.size() - 1)];

  if (mode_ == "debug") {
    RCLCPP_INFO(
      get_logger(),
      "event resolve: id=%s layer=%s mode=%s cfg_prio=%d msg_prio=%u final_prio=%u variant=%zu/%zu file=%s",
      msg->event_id.c_str(),
      layer.c_str(),
      ec.mode.c_str(),
      ec.priority,
      msg->priority,
      priority,
      idx + 1,
      ec.variants.size(),
      short_file(var.file).c_str());
  }

  bool played = false;
  try {
    if (layer == "music") {
      played = play_music_(msg->event_id, var.file, ec.mode == "loop", priority, msg->force);
    } else {
      played = play_fx_(msg->event_id, var.file, layer, priority, msg->force);
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "Audio play failed: %s", e.what());
  }

  if (played) {
    mark_played_(msg->event_id);
    if (mode_ != "debug") {
      RCLCPP_INFO(
        get_logger(),
        "event played: id=%s layer=%s prio=%u file=%s mode=%s%s",
        msg->event_id.c_str(),
        layer.c_str(),
        priority,
        short_file(var.file).c_str(),
        mode_.c_str(),
        is_mute_mode_() ? " (muted)" : "");
    }
  } else if (mode_ == "debug") {
    RCLCPP_INFO(
      get_logger(),
      "event skip: id=%s reason=channel_priority_or_state layer=%s prio=%u",
      msg->event_id.c_str(),
      layer.c_str(),
      priority);
  }
}

void AudioServerNode::cleanup_and_publish_()
{
  audio_manager_ros2::msg::AudioStatus status;

  {
    std::lock_guard<std::mutex> lk(mutex_);

    if (music_.handle && !backend_->is_running(music_.handle)) {
      if (!music_.loop) {
        music_ = ChannelState{};
      }
    }

    if (fx_.handle && !backend_->is_running(fx_.handle)) {
      fx_ = ChannelState{};
    }

    if (fx_.virtual_active && !fx_.loop && now() >= fx_.virtual_until) {
      fx_ = ChannelState{};
    }
    if (music_.virtual_active && !music_.loop && now() >= music_.virtual_until) {
      music_ = ChannelState{};
    }

    maybe_release_ducking_();

    status.mode = mode_;
    status.backend = backend_->backend_name();
    status.device = "";

    status.current_music_event = music_.event_id;
    status.current_music_file = music_.file;

    status.active_sfx_events.clear();
    if (channel_active_(fx_) && !fx_.event_id.empty()) {
      status.active_sfx_events.push_back(fx_.event_id);
    }

    status.last_event = last_event_;
    status.last_event_stamp = last_event_stamp_;

    const auto music_it = config_.layers.find("music");
    const auto sfx_it = config_.layers.find("sfx");
    const auto alerts_it = config_.layers.find("alerts");

    status.master_gain = static_cast<float>(config_.master_gain);
    status.music_gain = static_cast<float>((music_it != config_.layers.end()) ? music_it->second.gain : 0.0);
    status.sfx_gain = static_cast<float>((sfx_it != config_.layers.end()) ? sfx_it->second.gain : 0.0);
    status.alerts_gain = static_cast<float>((alerts_it != config_.layers.end()) ? alerts_it->second.gain : 0.0);

    if (backend_->backend_name().find("null_backend") != std::string::npos) {
      status.status_msg = "ok (null_backend: no real audio output)";
    } else {
      status.status_msg = "ok";
    }
  }

  status_pub_->publish(status);
}

}  // namespace audio_manager_ros2
