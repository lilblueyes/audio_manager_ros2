# audio_manager_ros2

Generic ROS 2 audio event server.

## Quickstart

```bash
colcon build --symlink-install --packages-select audio_manager_ros2
source install/setup.bash
ros2 launch audio_manager_ros2 audio_server.launch.py backend:=null mode:=debug
```

## API

- Input topic: `/audio/event` (`audio_manager_ros2/msg/AudioEvent`)
- Output topic: `/audio/status` (`audio_manager_ros2/msg/AudioStatus`)
- Services:
  - `/audio/stop` (`audio_manager_ros2/srv/Stop`)
  - `/audio/set_mode` (`audio_manager_ros2/srv/SetMode`)
  - `/audio/reload_config` (`audio_manager_ros2/srv/ReloadConfig`)

## Usage Examples

Publish a generic event (`notification`):

```bash
ros2 topic pub --once /audio/event audio_manager_ros2/msg/AudioEvent \
"{event_id: 'notification', priority: 120, layer: 'sfx', stop: false, force: true, stamp: {sec: 0, nanosec: 0}, source: 'cli'}"
```

Inspect status:

```bash
ros2 topic echo --once /audio/status
```

Stop one layer:

```bash
ros2 service call /audio/stop audio_manager_ros2/srv/Stop "{layer: 'music'}"
```

Change mode:

```bash
ros2 service call /audio/set_mode audio_manager_ros2/srv/SetMode "{mode: 'normal'}"
```

Reload config:

```bash
ros2 service call /audio/reload_config audio_manager_ros2/srv/ReloadConfig "{}"
```

## Modes

- `normal`: plays audio normally and logs one line per played event.
- `debug`: very verbose trace (`rx/resolve/skip/preempt/play/...`).
- `silent`: only accepts forced/high-priority/alerts events.
- `mute`: keeps normal event resolution logic but never outputs audio (logs played events as muted).

## Backends

- `player_backend`: system-player backend with this priority order:
  - `gst-play-1.0`
  - `ffplay`
  - `paplay`
  - `aplay`
- `null_backend`: no audio output (useful for CI and simulation).

`player_backend` limitation:

- portable runtime gain control is not available across all players,
- ducking may fall back to stop/resume behavior for music.

## Alternatives

If you need audio transport, capture, streaming, or richer audio pipelines, consider [`audio_common`](https://github.com/mgonzs13/audio_common).
