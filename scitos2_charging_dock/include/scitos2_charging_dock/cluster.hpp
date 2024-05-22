// Copyright (c) 2024 Alberto J. Tudela Roldán
// Copyright (c) 2024 Grupo Avispa, DTE, Universidad de Málaga
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SCITOS2_CHARGING_DOCK__CLUSTER_HPP_
#define SCITOS2_CHARGING_DOCK__CLUSTER_HPP_

// PCL
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/common/centroid.h>

// C++
#include <vector>

// ROS
#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"

namespace scitos2_charging_dock
{

/**
 * @class scitos2_charging_dock::Cluster
 * @brief Class to represent a cluster of points.
 */
struct Cluster
{
  // Original pointcloud of the cluster.
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud;
  // Pointcloud of the cluster after perform ICP.
  pcl::PointCloud<pcl::PointXYZ>::Ptr matched_cloud;
  // Score of the ICP.
  double icp_score;
  // Distance from the initial pose.
  double distance_from_initial_pose;
  // Pose of the dock.
  geometry_msgs::msg::PoseStamped icp_pose;

  /**
   * @brief Get the centroid of the dock.
   *
   * @return geometry_msgs::msg::Point The centroid of the dock.
   */
  geometry_msgs::msg::Point getCentroid()
  {
    Eigen::Vector4f centroid_vec_4f;
    pcl::compute3DCentroid(*cloud, centroid_vec_4f);
    geometry_msgs::msg::Point centroid;
    centroid.x = static_cast<double>(centroid_vec_4f[0]);
    centroid.y = static_cast<double>(centroid_vec_4f[1]);
    return centroid;
  }

  /**
   * @brief Get the width of the cluster.
   *
   * @return double The width of the cluster.
   */
  double width() const
  {
    if (!cloud) {return 0;}

    pcl::PointXYZ pt1 = (*cloud).front();
    pcl::PointXYZ pt2 = (*cloud).back();
    return sqrt(pow((pt1.x - pt2.x), 2) + pow((pt1.y - pt2.y), 2));
  }

  /**
   * @brief Check if the cluster is valid.
   *
   * @param ideal_size Size of the cluster.
   * @return bool If the cluster is valid.
   */
  bool valid(double ideal_size)
  {
    // If there are no points this cannot be valid.
    if (!cloud) {return false;}

    // Check overall size.
    if (width() > 1.25 * ideal_size || width() < ideal_size / 2.0) {return false;}

    // Not too far off from initial pose.
    return distance_from_initial_pose < 1.0;
  }

  friend bool operator<(const Cluster c1, const Cluster c2) {return c1.width() < c2.width();}
  friend bool operator>(const Cluster c1, const Cluster c2) {return c1.width() > c2.width();}
};
}  // namespace scitos2_charging_dock

#endif  // SCITOS2_CHARGING_DOCK__CLUSTER_HPP_
