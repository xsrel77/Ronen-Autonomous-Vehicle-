# RBV2 ver29 - ROS2 Humble slam_toolbox mapping service

This service adds map building only.
It does not start Nav2, does not perform path planning, and does not enable autonomous navigation.

## Runtime controls

- `R1` in joystick mode: start/stop ROS2 `slam_toolbox` mapping.
- `R2` in joystick mode: load the latest saved ROS2 map metadata from `maps/ros2_slam/latest_map.json`.
- `Q` keyboard LiDAR debug is blocked while R1 mapping is active.
- `R1` is blocked while the internal MiniLidarSDL LiDAR debug is active.
- `X` devFarm LiDAR map recording is also blocked while R1 mapping is active.

## Main idea

The main robot program remains a normal C++ application.
It does not become a ROS2 node.

During R1 mapping:

1. ROS2 node `rbv2_lidar_scan_node` opens `/dev/ttyUSB0` and publishes `/scan`.
2. The main robot program sends estimated odom over local UDP to `127.0.0.1:29129`.
3. ROS2 node `rbv2_odom_udp_bridge_node` publishes `/odom` and TF `odom -> base_link`.
4. Static TF publishes `base_link -> laser`.
5. `slam_toolbox` publishes/builds the map.
6. On stop, `nav2_map_server map_saver_cli` saves `map.yaml` and `map.pgm`.

## Important odom note

The robot does not have wheel encoders.
The `/odom` topic is estimated from drive commands, Ackermann approximation, and IMU yaw rate when available.
This is suitable for first greenhouse-row map experiments, but map quality depends heavily on slow, smooth manual driving and correct LiDAR mounting measurements.

## Folder outputs

Maps are saved under:

```text
maps/ros2_slam/session_YYYYMMDD_HHMMSS/
├── map.yaml
├── map.pgm
├── session_manifest.json
└── ros2_mapping.log
```

The latest map pointer is:

```text
maps/ros2_slam/latest_map.json
```
