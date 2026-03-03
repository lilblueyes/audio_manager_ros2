#include "audio_manager_ros2/config.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace audio_manager_ros2
{

namespace
{

double as_double_or(const YAML::Node & n, const char * key, double def)
{
  if (!n || !n[key]) return def;
  return n[key].as<double>();
}

int as_int_or(const YAML::Node & n, const char * key, int def)
{
  if (!n || !n[key]) return def;
  return n[key].as<int>();
}

bool as_bool_or(const YAML::Node & n, const char * key, bool def)
{
  if (!n || !n[key]) return def;
  return n[key].as<bool>();
}

std::string as_string_or(const YAML::Node & n, const char * key, const std::string & def)
{
  if (!n || !n[key]) return def;
  return n[key].as<std::string>();
}

std::string resolve_audio_path(const fs::path & yaml_path, const std::string & maybe_rel)
{
  if (maybe_rel.empty()) return maybe_rel;

  fs::path candidate(maybe_rel);
  if (candidate.is_absolute()) {
    return candidate.lexically_normal().string();
  }

  const fs::path config_dir = yaml_path.parent_path();
  const fs::path direct = (config_dir / candidate).lexically_normal();
  return direct.string();
}

}  // namespace

Config Config::LoadFromFile(const std::string & yaml_path)
{
  if (yaml_path.empty()) {
    throw std::runtime_error("Config path is empty.");
  }

  const fs::path cfg_path(yaml_path);
  if (!fs::exists(cfg_path)) {
    throw std::runtime_error("Config file not found: " + yaml_path);
  }

  YAML::Node root = YAML::LoadFile(yaml_path);

  Config out;
  if (root["audio"]) {
    out.master_gain = as_double_or(root["audio"], "master_gain", 1.0);
  }

  if (root["ducking"]) {
    out.ducking.enabled = as_bool_or(root["ducking"], "enabled", true);
    out.ducking.music_gain = as_double_or(root["ducking"], "music_gain", 0.35);
    out.ducking.hold_ms = as_int_or(root["ducking"], "hold_ms", 900);
  }

  out.layers.clear();
  if (root["layers"]) {
    for (const auto & it : root["layers"]) {
      const std::string layer_name = it.first.as<std::string>();
      const YAML::Node ln = it.second;

      LayerConfig lc;
      lc.gain = as_double_or(ln, "gain", 1.0);
      lc.max_polyphony = as_int_or(ln, "max_polyphony", layer_name == "music" ? 1 : 1);
      lc.fade_ms = as_int_or(ln, "fade_ms", 0);
      out.layers[layer_name] = lc;
    }
  }

  auto ensure_layer = [&](const std::string & name, double gain, int poly) {
    if (out.layers.find(name) == out.layers.end()) {
      LayerConfig lc;
      lc.gain = gain;
      lc.max_polyphony = poly;
      out.layers[name] = lc;
    }
  };
  ensure_layer("music", 0.7, 1);
  ensure_layer("sfx", 1.0, 1);
  ensure_layer("alerts", 1.0, 1);

  out.events.clear();
  if (root["events"]) {
    for (const auto & it : root["events"]) {
      const std::string event_id = it.first.as<std::string>();
      const YAML::Node en = it.second;

      EventConfig ec;
      ec.layer = as_string_or(en, "layer", "sfx");
      ec.mode = as_string_or(en, "mode", "oneshot");
      ec.priority = as_int_or(en, "priority", 120);
      ec.cooldown_ms = as_int_or(en, "cooldown_ms", 0);

      if (!en["variants"] || !en["variants"].IsSequence() || en["variants"].size() == 0) {
        throw std::runtime_error("Event '" + event_id + "' has no variants.");
      }

      for (const auto & v : en["variants"]) {
        Variant var;
        var.file = resolve_audio_path(cfg_path, v["file"].as<std::string>());
        var.weight = v["weight"] ? v["weight"].as<double>() : 1.0;
        var.gain = v["gain"] ? v["gain"].as<double>() : 1.0;
        ec.variants.push_back(var);
      }
      out.events[event_id] = ec;
    }
  }

  return out;
}

}  // namespace audio_manager_ros2
