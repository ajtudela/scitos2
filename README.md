# scitos2

![ROS2](https://img.shields.io/badge/ros2-humble-blue?logo=ros&logoColor=white)
![License](https://img.shields.io/github/license/grupo-avispa/scitos2)

## Overview

`scitos2` is a ROS 2 stack designed for Metralabs robots that utilize the MIRA framework, including models such as SCITOS, TORY, MORPHIA, etc. This stack comprises several packages, each serving a unique purpose:

 * [scitos2_behavior_tree]: This package contains behavior tree nodes that extend your robot's functionalities, such as emergency stop, reset motor stop, etc.
 * [scitos2_common]: This package provides common functionalities for the scitos2 stack.
 * [scitos2_core]: This package provides the abstract interface (virtual base classes) for the Scitos Modules.
 * [scitos2_mira]: This is the main node that interfaces with the MIRA framework.
 * [scitos2_modules]: This package implements the MIRA authorities as modules.
 * [scitos2_msgs]: This package contains messages and services related to the Metralabs Scitos robot base.

**Keywords:** ROS, ROS2, metralabs, mira, scitos

**Author: Alberto Tudela<br />**

The `scitos2` package has been tested under [ROS2] Humble on [Ubuntu] 22.04. Please note that this is research code and is subject to frequent changes. Its suitability for any specific purpose is not guaranteed.

## Installation

### Building from Source

#### Dependencies

- [Robot Operating System (ROS) 2](https://docs.ros.org/en/humble/) (middleware for robotics)
- [MIRA](https://www.mira-project.org/) (Metralabs middleware for robotic applications)

#### Building

To build from source, clone the latest version from this repository into your colcon workspace and compile the package using the following command:
```bash
cd colcon_workspace/src
git clone https://github.com/grupo-avispa/scitos2.git
cd ../
rosdep install -i --from-path src --rosdistro humble -y
colcon build --symlink-install
```

[Ubuntu]: https://ubuntu.com/
[ROS2]: https://docs.ros.org/en/humble/
[scitos2_behavior_tree]: /scitos2_behavior_tree
[scitos2_common]: /scitos2_common
[scitos2_core]: /scitos2_core
[scitos2_mira]: /scitos2_mira
[scitos2_modules]: /scitos2_modules
[scitos2_msgs]: /scitos2_msgs
