// Copyright (c) 2023 Boston Dynamics AI Institute LLC. All rights reserved.

#include <bosdyn/api/geometry.pb.h>
#include <bosdyn/api/robot_state.pb.h>
#include <bosdyn/math/frame_helpers.h>
#include <bosdyn/math/proto_math.h>
#include <builtin_interfaces/msg/duration.hpp>
#include <spot_driver_cpp/api/default_robot_state_client.hpp>
#include <spot_driver_cpp/api/time_sync_api.hpp>
#include <spot_driver_cpp/conversions/geometry.hpp>

#include <string_view>

namespace {
static const std::map<const std::string, const std::string> kFriendlyJointNames = {
    {"fl.hx", "front_left_hip_x"},  {"fl.hy", "front_left_hip_y"},  {"fl.kn", "front_left_knee"},
    {"fr.hx", "front_right_hip_x"}, {"fr.hy", "front_right_hip_y"}, {"fr.kn", "front_right_knee"},
    {"hl.hx", "rear_left_hip_x"},   {"hl.hy", "rear_left_hip_y"},   {"hl.kn", "rear_left_knee"},
    {"hr.hx", "rear_right_hip_x"},  {"hr.hy", "rear_right_hip_y"},  {"hr.kn", "rear_right_knee"},
    {"arm0.sh0", "arm_sh0"},        {"arm0.sh1", "arm_sh1"},        {"arm0.hr0", "arm_hr0"},
    {"arm0.el0", "arm_el0"},        {"arm0.el1", "arm_el1"},        {"arm0.wr0", "arm_wr0"},
    {"arm0.wr1", "arm_wr1"},        {"arm0.f1x", "arm_f1x"},
};

spot_msgs::msg::BatteryStateArray GetBatteryStates(const ::bosdyn::api::RobotState& robot_state,
                                                   const google::protobuf::Duration& clock_skew) {
  spot_msgs::msg::BatteryStateArray battery_states;

  for (const auto& battery : robot_state.battery_states()) {
    spot_msgs::msg::BatteryState battery_state;

    battery_state.header.stamp = spot_ros2::applyClockSkew(battery.timestamp(), clock_skew);
    battery_state.identifier = battery.identifier();
    battery_state.charge_percentage = battery.charge_percentage().value();
    battery_state.estimated_runtime = builtin_interfaces::build<builtin_interfaces::msg::Duration>()
                                          .sec(battery.estimated_runtime().seconds())
                                          .nanosec(battery.estimated_runtime().nanos());
    battery_state.current = battery.current().value();
    battery_state.voltage = battery.voltage().value();
    for (const auto& temp : battery.temperatures()) {
      battery_state.temperatures.push_back(temp);
    }
    battery_state.status = battery.status();
    battery_states.battery_states.push_back(battery_state);
  }

  return battery_states;
}

spot_msgs::msg::WiFiState GetWifiState(const ::bosdyn::api::RobotState& robot_state) {
  spot_msgs::msg::WiFiState wifi_state;

  for (const auto& comm_state : robot_state.comms_states()) {
    if (comm_state.has_wifi_state()) {
      wifi_state.current_mode = comm_state.wifi_state().current_mode();
      wifi_state.essid = comm_state.wifi_state().essid();
    }
  }

  return wifi_state;
}

spot_msgs::msg::FootStateArray GetFootState(const ::bosdyn::api::RobotState& robot_state) {
  spot_msgs::msg::FootStateArray foot_states;

  for (const auto& foot : robot_state.foot_state()) {
    spot_msgs::msg::FootState foot_state;
    foot_state.foot_position_rt_body.x = foot.foot_position_rt_body().x();
    foot_state.foot_position_rt_body.y = foot.foot_position_rt_body().y();
    foot_state.foot_position_rt_body.z = foot.foot_position_rt_body().z();
    foot_state.contact = foot.contact();
    foot_states.states.push_back(foot_state);
  }

  return foot_states;
}

spot_msgs::msg::EStopStateArray GetEstopStates(const ::bosdyn::api::RobotState& robot_state,
                                               const google::protobuf::Duration& clock_skew) {
  spot_msgs::msg::EStopStateArray estop_states;

  for (const auto& estop : robot_state.estop_states()) {
    spot_msgs::msg::EStopState estop_state;
    estop_state.header.stamp = spot_ros2::applyClockSkew(estop.timestamp(), clock_skew);
    estop_state.name = estop.name();
    estop_state.type = estop.type();
    estop_state.state = estop.state();
    estop_state.state_description = estop.state_description();
    estop_states.estop_states.push_back(estop_state);
  }

  return estop_states;
}

std::optional<sensor_msgs::msg::JointState> GetJointStates(const ::bosdyn::api::RobotState& robot_state,
                                                           const google::protobuf::Duration& clock_skew,
                                                           const std::string& prefix) {
  if (robot_state.has_kinematic_state()) {
    sensor_msgs::msg::JointState joint_states;
    joint_states.header.stamp =
        spot_ros2::applyClockSkew(robot_state.kinematic_state().acquisition_timestamp(), clock_skew);

    for (const auto& joint : robot_state.kinematic_state().joint_states()) {
      const auto joint_name = prefix + kFriendlyJointNames.at(joint.name());
      joint_states.name.push_back(joint_name);
      joint_states.position.push_back(joint.position().value());
      joint_states.velocity.push_back(joint.velocity().value());
      joint_states.effort.push_back(joint.load().value());
    }

    return joint_states;
  }
  return {};
}

std::optional<tf2_msgs::msg::TFMessage> GetTf(const ::bosdyn::api::RobotState& robot_state,
                                              const google::protobuf::Duration& clock_skew, const std::string& prefix,
                                              const std::string& inverse_target_frame_id) {
  if (robot_state.has_kinematic_state()) {
    tf2_msgs::msg::TFMessage tf_msg;

    for (const auto& [child_frame_id, transform] :
         robot_state.kinematic_state().transforms_snapshot().child_to_parent_edge_map()) {
      const auto local_time =
          spot_ros2::applyClockSkew(robot_state.kinematic_state().acquisition_timestamp(), clock_skew);
      if (inverse_target_frame_id == std::string(prefix + child_frame_id)) {
        const auto inversed_tf = ~(transform.parent_tform_child());
        tf_msg.transforms.push_back(spot_ros2::conversions::toTransformStamped(
            inversed_tf, std::string(prefix + child_frame_id), std::string(prefix + transform.parent_frame_name()),
            local_time));
      } else {
        tf_msg.transforms.push_back(spot_ros2::conversions::toTransformStamped(
            transform.parent_tform_child(), std::string(prefix + transform.parent_frame_name()),
            std::string(prefix + child_frame_id), local_time));
      }
    }
    return tf_msg;
  }

  return {};
}

std::optional<geometry_msgs::msg::TwistWithCovarianceStamped> GetOdomTwist(
    const ::bosdyn::api::RobotState& robot_state, const google::protobuf::Duration& clock_skew) {
  if (robot_state.has_kinematic_state()) {
    geometry_msgs::msg::TwistWithCovarianceStamped odom_twist_msg;

    odom_twist_msg.header.stamp =
        spot_ros2::applyClockSkew(robot_state.kinematic_state().acquisition_timestamp(), clock_skew);
    odom_twist_msg.twist.twist.linear.x = robot_state.kinematic_state().velocity_of_body_in_odom().linear().x();
    odom_twist_msg.twist.twist.linear.y = robot_state.kinematic_state().velocity_of_body_in_odom().linear().y();
    odom_twist_msg.twist.twist.linear.z = robot_state.kinematic_state().velocity_of_body_in_odom().linear().z();
    odom_twist_msg.twist.twist.angular.x = robot_state.kinematic_state().velocity_of_body_in_odom().linear().x();
    odom_twist_msg.twist.twist.angular.y = robot_state.kinematic_state().velocity_of_body_in_odom().angular().y();
    odom_twist_msg.twist.twist.angular.z = robot_state.kinematic_state().velocity_of_body_in_odom().angular().z();

    return odom_twist_msg;
  }
  return {};
}

std::optional<nav_msgs::msg::Odometry> GetOdom(const ::bosdyn::api::RobotState& robot_state,
                                               const google::protobuf::Duration& clock_skew, const std::string& prefix,
                                               bool is_using_vision) {
  if (robot_state.has_kinematic_state()) {
    nav_msgs::msg::Odometry odom_msg;
    ::bosdyn::api::SE3Pose tf_body_pose;
    geometry_msgs::msg::PoseWithCovariance pose_odom_msg;

    odom_msg.header.stamp =
        spot_ros2::applyClockSkew(robot_state.kinematic_state().acquisition_timestamp(), clock_skew);
    if (is_using_vision) {
      odom_msg.header.frame_id = prefix + "vision";
      ::bosdyn::api::GetWorldTformBody(robot_state.kinematic_state().transforms_snapshot(), &tf_body_pose);
    } else {
      odom_msg.header.frame_id = prefix + "odom";
      ::bosdyn::api::GetOdomTformBody(robot_state.kinematic_state().transforms_snapshot(), &tf_body_pose);
    }
    odom_msg.child_frame_id = prefix + "body";

    pose_odom_msg.pose.position.x = tf_body_pose.position().x();
    pose_odom_msg.pose.position.y = tf_body_pose.position().y();
    pose_odom_msg.pose.position.z = tf_body_pose.position().z();
    pose_odom_msg.pose.orientation.x = tf_body_pose.rotation().x();
    pose_odom_msg.pose.orientation.y = tf_body_pose.rotation().y();
    pose_odom_msg.pose.orientation.z = tf_body_pose.rotation().z();
    pose_odom_msg.pose.orientation.w = tf_body_pose.rotation().w();

    odom_msg.pose = pose_odom_msg;
    odom_msg.twist = GetOdomTwist(robot_state, clock_skew).value().twist;
    return odom_msg;
  }
  return {};
}

std::optional<spot_msgs::msg::PowerState> GetPowerState(const ::bosdyn::api::RobotState& robot_state,
                                                        const google::protobuf::Duration& clock_skew) {
  if (robot_state.has_power_state()) {
    spot_msgs::msg::PowerState power_state;

    power_state.header.stamp = spot_ros2::applyClockSkew(robot_state.power_state().timestamp(), clock_skew);
    power_state.motor_power_state = robot_state.power_state().motor_power_state();
    power_state.shore_power_state = robot_state.power_state().shore_power_state();
    power_state.locomotion_charge_percentage = robot_state.power_state().locomotion_charge_percentage().value();
    power_state.locomotion_estimated_runtime =
        builtin_interfaces::build<builtin_interfaces::msg::Duration>()
            .sec(robot_state.power_state().locomotion_estimated_runtime().seconds())
            .nanosec(robot_state.power_state().locomotion_estimated_runtime().nanos());

    return power_state;
  }

  return {};
}

std::optional<spot_msgs::msg::SystemFaultState> GetSystemFaultState(const ::bosdyn::api::RobotState& robot_state,
                                                                    const google::protobuf::Duration& clock_skew) {
  if (robot_state.has_system_fault_state()) {
    spot_msgs::msg::SystemFaultState system_fault_state;
    const auto create_fault_message = [&clock_skew](const ::bosdyn::api::SystemFault& fault) {
      spot_msgs::msg::SystemFault fault_msg;
      fault_msg.name = fault.name();
      fault_msg.header.stamp = spot_ros2::applyClockSkew(fault.onset_timestamp(), clock_skew);
      fault_msg.duration = builtin_interfaces::build<builtin_interfaces::msg::Duration>()
                               .sec(fault.duration().seconds())
                               .nanosec(fault.duration().nanos());
      fault_msg.code = fault.code();
      fault_msg.uid = fault.uid();
      fault_msg.error_message = fault.error_message();
      for (const auto& attr : fault.attributes()) {
        fault_msg.attributes.push_back(attr);
      }
      fault_msg.severity = fault.severity();
      return fault_msg;
    };
    for (const auto& fault : robot_state.system_fault_state().faults()) {
      system_fault_state.faults.push_back(create_fault_message(fault));
    }
    for (const auto& fault : robot_state.system_fault_state().historical_faults()) {
      system_fault_state.historical_faults.push_back(create_fault_message(fault));
    }
    return system_fault_state;
  }
  return {};
}
std::optional<bosdyn_msgs::msg::ManipulatorState> GetManipulatorState(const ::bosdyn::api::RobotState& robot_state) {
  if (robot_state.has_manipulator_state()) {
    bosdyn_msgs::msg::ManipulatorState manipulator_state;

    manipulator_state.gripper_open_percentage = robot_state.manipulator_state().gripper_open_percentage();
    manipulator_state.is_gripper_holding_item = robot_state.manipulator_state().is_gripper_holding_item();

    manipulator_state.estimated_end_effector_force_in_hand.x =
        robot_state.manipulator_state().estimated_end_effector_force_in_hand().x();
    manipulator_state.estimated_end_effector_force_in_hand.y =
        robot_state.manipulator_state().estimated_end_effector_force_in_hand().y();
    manipulator_state.estimated_end_effector_force_in_hand.z =
        robot_state.manipulator_state().estimated_end_effector_force_in_hand().z();
    manipulator_state.estimated_end_effector_force_in_hand_is_set =
        robot_state.manipulator_state().has_estimated_end_effector_force_in_hand();

    manipulator_state.stow_state.value = robot_state.manipulator_state().stow_state();

    manipulator_state.velocity_of_hand_in_vision.linear.x =
        robot_state.manipulator_state().velocity_of_hand_in_vision().linear().x();
    manipulator_state.velocity_of_hand_in_vision.linear.y =
        robot_state.manipulator_state().velocity_of_hand_in_vision().linear().y();
    manipulator_state.velocity_of_hand_in_vision.linear.z =
        robot_state.manipulator_state().velocity_of_hand_in_vision().linear().z();
    manipulator_state.velocity_of_hand_in_vision.angular.x =
        robot_state.manipulator_state().velocity_of_hand_in_vision().angular().x();
    manipulator_state.velocity_of_hand_in_vision.angular.y =
        robot_state.manipulator_state().velocity_of_hand_in_vision().angular().y();
    manipulator_state.velocity_of_hand_in_vision.angular.z =
        robot_state.manipulator_state().velocity_of_hand_in_vision().angular().z();
    manipulator_state.velocity_of_hand_in_vision_is_set =
        robot_state.manipulator_state().has_velocity_of_hand_in_vision();

    manipulator_state.velocity_of_hand_in_odom.linear.x =
        robot_state.manipulator_state().velocity_of_hand_in_odom().linear().x();
    manipulator_state.velocity_of_hand_in_odom.linear.y =
        robot_state.manipulator_state().velocity_of_hand_in_odom().linear().y();
    manipulator_state.velocity_of_hand_in_odom.linear.z =
        robot_state.manipulator_state().velocity_of_hand_in_odom().linear().z();
    manipulator_state.velocity_of_hand_in_odom.angular.x =
        robot_state.manipulator_state().velocity_of_hand_in_odom().angular().x();
    manipulator_state.velocity_of_hand_in_odom.angular.y =
        robot_state.manipulator_state().velocity_of_hand_in_odom().angular().y();
    manipulator_state.velocity_of_hand_in_odom.angular.z =
        robot_state.manipulator_state().velocity_of_hand_in_odom().angular().z();
    manipulator_state.velocity_of_hand_in_odom_is_set = robot_state.manipulator_state().has_velocity_of_hand_in_odom();

    manipulator_state.carry_state.value = robot_state.manipulator_state().carry_state();
    return manipulator_state;
  }
  return {};
}
std::optional<geometry_msgs::msg::Vector3Stamped> GetEndEffectorForce(const ::bosdyn::api::RobotState& robot_state,
                                                                      const google::protobuf::Duration& clock_skew,
                                                                      const std::string& prefix) {
  if (robot_state.has_manipulator_state()) {
    geometry_msgs::msg::Vector3Stamped force;
    force.header.stamp = spot_ros2::applyClockSkew(robot_state.kinematic_state().acquisition_timestamp(), clock_skew);
    force.header.frame_id = prefix + "hand";
    force.vector.x = robot_state.manipulator_state().estimated_end_effector_force_in_hand().x();
    force.vector.y = robot_state.manipulator_state().estimated_end_effector_force_in_hand().y();
    force.vector.z = robot_state.manipulator_state().estimated_end_effector_force_in_hand().z();

    return force;
  }
  return {};
}
std::optional<spot_msgs::msg::BehaviorFaultState> GetBehaviorFaultState(const ::bosdyn::api::RobotState& robot_state,
                                                                        const google::protobuf::Duration& clock_skew) {
  if (robot_state.has_behavior_fault_state()) {
    spot_msgs::msg::BehaviorFaultState behavior_fault_msgs;

    for (const auto& fault : robot_state.behavior_fault_state().faults()) {
      spot_msgs::msg::BehaviorFault fault_msg;
      fault_msg.behavior_fault_id = fault.behavior_fault_id();
      fault_msg.header.stamp = spot_ros2::applyClockSkew(fault.onset_timestamp(), clock_skew);
      fault_msg.cause = fault.cause();
      fault_msg.status = fault.status();
      behavior_fault_msgs.faults.push_back(fault_msg);
    }
    return behavior_fault_msgs;
  }
  return {};
}

}  // namespace

namespace spot_ros2 {

DefaultRobotStateClient::DefaultRobotStateClient(::bosdyn::client::RobotStateClient* client,
                                                 std::shared_ptr<TimeSyncApi> time_sync_api,
                                                 const std::string& robot_name)
    : client_{client}, time_sync_api_{time_sync_api}, frame_prefix_{robot_name.empty() ? "" : robot_name + "/"} {}

tl::expected<RobotState, std::string> DefaultRobotStateClient::getRobotState() {
  std::shared_future<::bosdyn::client::RobotStateResultType> get_robot_state_result_future =
      client_->GetRobotStateAsync();

  ::bosdyn::client::RobotStateResultType get_robot_state_result = get_robot_state_result_future.get();
  if (!get_robot_state_result.status || !get_robot_state_result.response.has_robot_state()) {
    return tl::make_unexpected("Failed to get robot state: " + get_robot_state_result.status.DebugString());
  }

  const auto clock_skew_result = time_sync_api_->getClockSkew();
  if (!clock_skew_result) {
    return tl::make_unexpected("Failed to get latest clock skew: " + clock_skew_result.error());
  }

  const auto robot_state = get_robot_state_result.response.robot_state();

  const auto out =
      RobotState{GetBatteryStates(robot_state, clock_skew_result.value()),
                 GetWifiState(robot_state),
                 GetFootState(robot_state),
                 GetEstopStates(robot_state, clock_skew_result.value()),
                 GetJointStates(robot_state, clock_skew_result.value(),
                                "Opal/"),  // TODO(abaker-bdai): This should be a parameter passed into the client
                 GetTf(robot_state, clock_skew_result.value(), "Opal/",
                       "body"),  // TODO(abaker-bdai): This should be a parameter passed into the client
                 GetOdomTwist(robot_state, clock_skew_result.value()),
                 GetOdom(robot_state, clock_skew_result.value(), "Opal/",
                         true),  // TODO(abaker-bdai): This should be a parameter passed into the client
                 GetPowerState(robot_state, clock_skew_result.value()),
                 GetSystemFaultState(robot_state, clock_skew_result.value()),
                 GetManipulatorState(robot_state),
                 GetEndEffectorForce(robot_state, clock_skew_result.value(), "Opal/"),
                 GetBehaviorFaultState(robot_state, clock_skew_result.value())};

  return out;
}

}  // namespace spot_ros2