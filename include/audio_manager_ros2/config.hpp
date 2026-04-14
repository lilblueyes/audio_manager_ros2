#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace audio_manager_ros2
{

struct Variant
{
  std::string file;
  double weight{1.0};
  double gain{1.0};
};

struct EventConfig
{
  bool enabled{true};
  std::string layer{"sfx"};
  std::string mode{"oneshot"};
  int priority{120};
  int cooldown_ms{0};
  std::vector<Variant> variants;
};

struct LayerConfig
{
  double gain{1.0};
  int max_polyphony{1};
  int fade_ms{0};
};

struct DuckingConfig
{
  bool enabled{true};
  double music_gain{0.35};
  int hold_ms{900};
};

struct Config
{
  double master_gain{1.0};
  DuckingConfig ducking;
  std::unordered_map<std::string, LayerConfig> layers;
  std::unordered_map<std::string, EventConfig> events;

  static Config LoadFromFile(const std::string & yaml_path);
};

}  // namespace audio_manager_ros2
