#!/usr/bin/env python3
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    namespace = LaunchConfiguration("namespace")
    use_sim_time = LaunchConfiguration("use_sim_time")
    mode = LaunchConfiguration("mode")
    backend = LaunchConfiguration("backend")
    config_file = LaunchConfiguration("config_file")

    default_cfg = PathJoinSubstitution(
        [FindPackageShare("audio_manager_ros2"), "config", "default.yaml"]
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument("namespace", default_value=""),
            DeclareLaunchArgument("use_sim_time", default_value="false"),
            DeclareLaunchArgument("mode", default_value="normal", description="normal|debug|silent|mute"),
            DeclareLaunchArgument("backend", default_value="auto", description="auto|player|null"),
            DeclareLaunchArgument("config_file", default_value=default_cfg),
            Node(
                package="audio_manager_ros2",
                executable="audio_server_node",
                name="audio_server",
                namespace=namespace,
                output="screen",
                parameters=[
                    {
                        "use_sim_time": ParameterValue(use_sim_time, value_type=bool),
                        "mode": ParameterValue(mode, value_type=str),
                        "backend": ParameterValue(backend, value_type=str),
                        "config_file": ParameterValue(config_file, value_type=str),
                    }
                ],
            ),
        ]
    )
