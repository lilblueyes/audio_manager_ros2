#!/usr/bin/env python3
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    mode = LaunchConfiguration("mode")
    config_file = LaunchConfiguration("config_file")

    default_cfg = PathJoinSubstitution(
        [FindPackageShare("audio_manager_ros2"), "config", "default.yaml"]
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument("namespace", default_value=""),
            DeclareLaunchArgument("mode", default_value="normal"),
            DeclareLaunchArgument("config_file", default_value=default_cfg),
            Node(
                package="audio_manager_ros2",
                executable="audio_server_node",
                name="audio_server",
                output="screen",
                parameters=[
                    {
                        "mode": ParameterValue(mode, value_type=str),
                        "config_file": ParameterValue(config_file, value_type=str),
                    }
                ],
            ),
        ]
    )
