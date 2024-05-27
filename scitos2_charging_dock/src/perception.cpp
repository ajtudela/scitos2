/*
 * DOCKING PERCEPTION CLASS
 *
 * Copyright (c) 2020-2023 Alberto José Tudela Roldán <ajtudela@gmail.com>
 *
 * This file is part of auto_docking.
 *
 * All rights reserved.
 *
 */

// PCL
#include <pcl/common/eigen.h>
#include <pcl/io/pcd_io.h>
#include <pcl/registration/icp.h>

// TF
#include "tf2/transform_datatypes.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

// ROS
#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "nav2_util/node_utils.hpp"
#include "pcl_conversions/pcl_conversions.h"
#include "pcl_ros/transforms.hpp"

#include "scitos2_charging_dock/perception.hpp"

namespace scitos2_charging_dock
{

Perception::Perception(
  const rclcpp_lifecycle::LifecycleNode::SharedPtr & node,
  const std::string & name, std::shared_ptr<tf2_ros::Buffer> tf)
{
  name_ = name;
  logger_ = node->get_logger();
  clock_ = node->get_clock();
  tf_buffer_ = tf;

  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.icp_min_score", rclcpp::ParameterValue(0.01));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.icp_max_iter", rclcpp::ParameterValue(300));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.icp_max_corr_dis", rclcpp::ParameterValue(0.25));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.icp_max_trans_eps", rclcpp::ParameterValue(0.000000001));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.icp_max_eucl_fit_eps", rclcpp::ParameterValue(0.000000001));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.enable_debug", rclcpp::ParameterValue(false));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".perception.dock_template_path", rclcpp::ParameterValue(""));

  node->get_parameter(name_ + ".perception.icp_min_score", icp_min_score_);
  node->get_parameter(name_ + ".perception.icp_max_iter", icp_max_iter_);
  node->get_parameter(name_ + ".perception.icp_max_corr_dis", icp_max_corr_dis_);
  node->get_parameter(name_ + ".perception.icp_max_trans_eps", icp_max_trans_eps_);
  node->get_parameter(name_ + ".perception.icp_max_eucl_fit_eps", icp_max_eucl_fit_eps_);
  node->get_parameter(name_ + ".perception.enable_debug", debug_);

  // Load the dock template
  std::string dock_template_path;
  dock_template_ = Pcloud::Ptr(new Pcloud);
  node->get_parameter(name_ + ".perception.dock_template_path", dock_template_path);
  loadDockPointcloud(dock_template_path, *dock_template_);

  // Publishers
  if (debug_) {
    target_cloud_pub_ = node->create_publisher<sensor_msgs::msg::PointCloud2>("dock/target", 1);
    dock_cloud_pub_ = node->create_publisher<sensor_msgs::msg::PointCloud2>("dock/cloud", 1);
    dock_template_pub_ = node->create_publisher<sensor_msgs::msg::PointCloud2>(
      "dock/template", 1);
  }

  dyn_params_handler_ = node->add_on_set_parameters_callback(
    std::bind(&Perception::dynamicParametersCallback, this, std::placeholders::_1));

  // Setup the segmentation object
  segmentation_ = std::make_unique<Segmentation>(node, name);

  // Set the verbosity level of the PCL library to shutout the console output
  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);
}

Perception::~Perception()
{
  segmentation_.reset();
  tf_buffer_.reset();
}

geometry_msgs::msg::PoseStamped Perception::getDockPose(const sensor_msgs::msg::LaserScan & scan)
{
  // Extract clusters from the scan
  auto clusters = extractClustersFromScan(scan);

  // Set the header of the template to the scan header
  dock_template_->header.frame_id = scan.header.frame_id;

  // Refine the pose of each cluster to get the dock pose
  geometry_msgs::msg::PoseStamped dock_pose;
  if (refineAllClustersPoses(clusters, dock_template_, dock_pose)) {
    detected_dock_pose_ = dock_pose;
  }
  return detected_dock_pose_;
}

void Perception::setInitialEstimate(
  const geometry_msgs::msg::Pose & pose, const std::string & frame)
{
  initial_estimate_pose_.pose = pose;
  initial_estimate_pose_.header.frame_id = frame;
  initial_estimate_pose_.header.stamp = clock_->now();
}

bool Perception::loadDockPointcloud(std::string filepath, Pcloud & dock)
{
  if (filepath.empty()) {
    RCLCPP_ERROR(logger_, "Couldn't load the dock from an empty file path");
  } else {
    if (pcl::io::loadPCDFile<pcl::PointXYZ>(filepath, dock) < 0) {
      RCLCPP_ERROR(logger_, "Failed to load the dock from PCD file");
    } else {
      RCLCPP_INFO(logger_, "Dock loaded from PCD file");
      return true;
    }
  }
  return false;
}

bool Perception::storeDockPointcloud(std::string filepath, const Pcloud & dock)
{
  bool success = false;
  if (pcl::io::savePCDFile<pcl::PointXYZ>(filepath, dock) < 0) {
    RCLCPP_ERROR(logger_, "Failed to save the dock to PCD file");
  } else {
    RCLCPP_INFO(logger_, "Dock saved to PCD file");
    success = true;
  }
  return success;
}

Clusters Perception::extractClustersFromScan(const sensor_msgs::msg::LaserScan & scan)
{
  // Perform segmentation on the scan and filter the segments
  auto segments = segmentation_->performSegmentation(scan);
  auto filtered_segments = segmentation_->filterSegments(segments);

  // Convert the segments to clusters
  return segmentsToClusters(scan.header.frame_id, filtered_segments);
}

Clusters Perception::segmentsToClusters(std::string frame, const Segments & segments)
{
  Clusters clusters;
  for (const auto & segment : segments) {
    Cluster cluster;
    cluster.matched_cloud = pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>);
    cluster.cloud = segmentToPcloud(frame, segment);
    clusters.push_back(cluster);
  }
  return clusters;
}

Pcloud::Ptr Perception::segmentToPcloud(std::string frame, const Segment & segment)
{
  Pcloud::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
  cloud->header.frame_id = frame;

  for (const auto & point : segment.points) {
    pcl::PointXYZ pcl_point;
    pcl_point.x = point.x;
    pcl_point.y = point.y;
    pcl_point.z = point.z;
    cloud->push_back(pcl_point);
  }

  return cloud;
}

bool Perception::refineClusterPose(Cluster & cluster, Pcloud::ConstPtr cloud_template)
{
  bool success = false;

  // Prepare the ICP object
  pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
  icp.setMaximumIterations(icp_max_iter_);
  icp.setMaxCorrespondenceDistance(icp_max_corr_dis_);
  icp.setTransformationEpsilon(icp_max_trans_eps_);
  icp.setEuclideanFitnessEpsilon(icp_max_eucl_fit_eps_);

  // Align the cluster to the template
  icp.setInputSource(cloud_template);
  icp.setInputTarget(cluster.cloud);
  Pcloud::Ptr matched_cloud(new pcl::PointCloud<pcl::PointXYZ>());
  icp.align(*matched_cloud);

  // If the ICP converged, store the results on the cluster
  if (icp.hasConverged()) {
    // Get the final transformation
    auto icp_refinement = eigenToTransform(icp.getFinalTransformation());
    // Transform the pose to the matching frame
    tf2::Transform tf_stage;
    tf2::fromMsg(initial_estimate_pose_.pose, tf_stage);
    tf2::Transform tf_correct = icp_refinement * tf_stage;
    // Convert to ROS msg
    geometry_msgs::msg::Pose pose_correct;
    tf2::toMsg(tf_correct, pose_correct);

    // Update the cluster
    cluster.icp_score = icp.getFitnessScore();
    cluster.icp_pose.header.frame_id = initial_estimate_pose_.header.frame_id;
    cluster.icp_pose.header.stamp = clock_->now();
    cluster.icp_pose.pose = pose_correct;
    cluster.matched_cloud = matched_cloud;

    RCLCPP_DEBUG(logger_, "ICP converged, cloud matched!");
    RCLCPP_DEBUG(logger_, "ICP fitness score: %f", cluster.icp_score);
    RCLCPP_DEBUG(
      logger_, "ICP transformation: (%f, %f)",
      cluster.icp_pose.pose.position.x, cluster.icp_pose.pose.position.y);

    success = true;
  } else {
    RCLCPP_DEBUG(logger_, "ICP did not converge");
  }

  return success;
}

bool Perception::refineAllClustersPoses(
  Clusters & clusters, Pcloud::ConstPtr cloud_template, geometry_msgs::msg::PoseStamped & dock_pose)
{
  long double lowest_score = icp_min_score_;
  int lowest_score_idx = -1;
  bool success = false;

  int count = 0;
  for (auto & cluster : clusters) {
    // Discard clusters not valid (i.e. clusters not similar to dock by size, ...)
    double dx = (*cloud_template).back().x - (*cloud_template).front().x;
    double dy = (*cloud_template).back().y - (*cloud_template).front().y;
    double template_width = std::hypot(dx, dy);
    if (!cluster.valid(template_width)) {
      continue;
    }

    // Prepares the template pointcloud for matching by transforming to the initial estimate pose.
    // Usually in global coordinates (map frame)
    Pcloud::Ptr cloud_template_initial(new pcl::PointCloud<pcl::PointXYZ>);
    tf2::Transform tf_stage;
    tf2::fromMsg(initial_estimate_pose_.pose, tf_stage);
    pcl_ros::transformPointCloud(*cloud_template, *cloud_template_initial, tf_stage);

    // Transforms the target point cloud from the scan frame to the staging frame (map frame)
    try {
      auto tf_stamped = tf_buffer_->lookupTransform(
        initial_estimate_pose_.header.frame_id, cluster.cloud->header.frame_id, tf2::TimePointZero);
      pcl_ros::transformPointCloud(*(cluster.cloud), *(cluster.cloud), tf_stamped);
    } catch (const tf2::TransformException & ex) {
      RCLCPP_WARN(
        logger_, "Could not transform %s to %s: %s",
        cluster.cloud->header.frame_id.c_str(),
        initial_estimate_pose_.header.frame_id.c_str(), ex.what());
      return false;
    }

    // Visualizes the target point cloud and estimated dock template pose
    if (debug_) {
      // Publish dock template
      sensor_msgs::msg::PointCloud2 template_msg;
      pcl::toROSMsg(*cloud_template, template_msg);
      template_msg.header.frame_id = initial_estimate_pose_.header.frame_id;
      template_msg.header.stamp = clock_->now();
      dock_template_pub_->publish(std::move(template_msg));

      // Publish cluster target
      sensor_msgs::msg::PointCloud2 target_msg;
      pcl::toROSMsg(*cluster.cloud, target_msg);
      target_msg.header.frame_id = initial_estimate_pose_.header.frame_id;
      target_msg.header.stamp = clock_->now();
      target_cloud_pub_->publish(std::move(target_msg));
    }

    // Refine the cluster to get the dock pose
    if (refineClusterPose(cluster, cloud_template_initial)) {
      // Check if potential dock is found
      if (cluster.icp_score < icp_min_score_) {
        RCLCPP_DEBUG(
          logger_, "Dock potentially identified at cluster %i with score %f",
          count, cluster.icp_score);
        if (lowest_score_idx == -1) {
          lowest_score_idx = count;
        }

        if (cluster.icp_score <= clusters[lowest_score_idx].icp_score) {
          lowest_score_idx = count;
          lowest_score = clusters[lowest_score_idx].icp_score;
          RCLCPP_DEBUG(
            logger_, "Dock identified at cluster %i with score %Lf", lowest_score_idx,
            lowest_score);
        }
      }
    }
    count++;
  }

  // Check if dock is found
  if (lowest_score_idx != -1) {
    dock_pose = clusters[lowest_score_idx].icp_pose;
    // Publish the dock cloud
    if (debug_) {
      sensor_msgs::msg::PointCloud2 cloud_msg;
      pcl::toROSMsg(*(clusters[lowest_score_idx].matched_cloud), cloud_msg);
      cloud_msg.header.frame_id = initial_estimate_pose_.header.frame_id;
      cloud_msg.header.stamp = clock_->now();
      dock_cloud_pub_->publish(std::move(cloud_msg));
    }
    RCLCPP_DEBUG(logger_, "Dock successfully identified at cluster %i", lowest_score_idx);
    success = true;
  } else {
    RCLCPP_DEBUG(logger_, "Unable to identify the dock");
  }

  return success;
}

tf2::Transform Perception::eigenToTransform(const Eigen::Matrix4f & T)
{
  return tf2::Transform(
    tf2::Matrix3x3(
      T(0, 0), T(0, 1), T(0, 2),
      T(1, 0), T(1, 1), T(1, 2),
      T(2, 0), T(2, 1), T(2, 2)),
    tf2::Vector3(T(0, 3), T(1, 3), T(2, 3)));
}

rcl_interfaces::msg::SetParametersResult Perception::dynamicParametersCallback(
  std::vector<rclcpp::Parameter> parameters)
{
  rcl_interfaces::msg::SetParametersResult result;
  std::lock_guard<std::mutex> lock_reinit(dynamic_params_lock_);

  for (auto parameter : parameters) {
    const auto & type = parameter.get_type();
    const auto & name = parameter.get_name();

    if (type == rclcpp::ParameterType::PARAMETER_INTEGER) {
      if (name == name_ + ".perception.icp_max_iter") {
        icp_max_iter_ = parameter.as_int();
      }
    } else if (type == rclcpp::ParameterType::PARAMETER_DOUBLE) {
      if (name == name_ + ".perception.icp_min_score") {
        icp_min_score_ = parameter.as_double();
      } else if (name == name_ + ".perception.icp_max_corr_dis") {
        icp_max_corr_dis_ = parameter.as_double();
      } else if (name == name_ + ".perception.icp_max_trans_eps") {
        icp_max_trans_eps_ = parameter.as_double();
      } else if (name == name_ + ".perception.icp_max_eucl_fit_eps") {
        icp_max_eucl_fit_eps_ = parameter.as_double();
      }
    } else if (type == rclcpp::ParameterType::PARAMETER_BOOL) {
      if (name == name_ + ".perception.enable_debug") {
        debug_ = parameter.as_bool();
      }
    }
  }

  result.successful = true;
  return result;
}

}  // namespace scitos2_charging_dock
