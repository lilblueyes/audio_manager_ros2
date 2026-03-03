# audio_manager_ros2

Generic ROS 2 audio event server.

## Quickstart

```bash
colcon build --symlink-install --packages-select audio_manager_ros2
source install/setup.bash
ros2 launch audio_manager_ros2 audio_server.launch.py mode:=normal
```

## API

- Topic in: `/audio/event`
- Topic out: `/audio/status`
- Services: `/audio/stop`, `/audio/set_mode`, `/audio/reload_config`

## Modes

- `normal`
- `debug`
- `silent`
- `mute`
