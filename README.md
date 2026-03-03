# audio_manager_ros2

Generic ROS 2 audio event server.

## Quickstart

```bash
colcon build --symlink-install --packages-select audio_manager_ros2
source install/setup.bash
ros2 run audio_manager_ros2 audio_server_node
```

## API

- Input topic: `/audio/event` (`audio_manager_ros2/msg/AudioEvent`)
- Output topic: `/audio/status` (`audio_manager_ros2/msg/AudioStatus`)
- Services:
  - `/audio/stop`
  - `/audio/set_mode`
  - `/audio/reload_config`
