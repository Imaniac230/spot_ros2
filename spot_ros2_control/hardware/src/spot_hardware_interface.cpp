// File modified. Modifications Copyright (c) 2024 Boston Dynamics AI Institute LLC.
// All rights reserved.

// --------------------------------------------------------------
// Copyright 2020 ros2_control Development Team
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

#include "spot_ros2_control/spot_hardware_interface.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace spot_ros2_control {

void StateStreamingHandler::handle_state_streaming(::bosdyn::api::RobotStateStreamResponse& robot_state) {
  // lock so that read/write doesn't happen at the same time
  const std::lock_guard<std::mutex> lock(mutex_);
  // Get joint states from the robot and write them to the joint_states_ struct
  const auto& position_msg = robot_state.joint_states().position();
  const auto& velocity_msg = robot_state.joint_states().velocity();
  const auto& load_msg = robot_state.joint_states().load();
  current_position_.assign(position_msg.begin(), position_msg.end());
  current_velocity_.assign(velocity_msg.begin(), velocity_msg.end());
  current_load_.assign(load_msg.begin(), load_msg.end());
}

void StateStreamingHandler::get_joint_states(JointStates& joint_states) {
  // lock so that read/write doesn't happen at the same time
  const std::lock_guard<std::mutex> lock(mutex_);
  // Fill in members of the joint states stuct passed in by reference.
  joint_states.position.assign(current_position_.begin(), current_position_.end());
  joint_states.velocity.assign(current_velocity_.begin(), current_velocity_.end());
  joint_states.load.assign(current_load_.begin(), current_load_.end());
}

hardware_interface::CallbackReturn SpotHardware::on_init(const hardware_interface::HardwareInfo& info) {
  if (hardware_interface::SystemInterface::on_init(info) != hardware_interface::CallbackReturn::SUCCESS) {
    return hardware_interface::CallbackReturn::ERROR;
  }
  // Get and save parameters
  hostname_ = info_.hardware_parameters["hostname"];
  username_ = info_.hardware_parameters["username"];
  password_ = info_.hardware_parameters["password"];

  hw_states_.resize(info_.joints.size() * interfaces_per_joint_, std::numeric_limits<double>::quiet_NaN());
  hw_commands_.resize(info_.joints.size() * interfaces_per_joint_, std::numeric_limits<double>::quiet_NaN());

  njoints_ = hw_states_.size() / interfaces_per_joint_;

  for (const hardware_interface::ComponentInfo& joint : info_.joints) {
    // Assumes three state and three command interfaces for each joint (position, velocity, and effort).
    if (joint.command_interfaces.size() != interfaces_per_joint_) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' has %zu command interfaces found. 3 expected.",
                   joint.name.c_str(), joint.command_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.command_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s command interfaces found. '%s' expected.",
                   joint.name.c_str(), joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.command_interfaces[1].name != hardware_interface::HW_IF_VELOCITY) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s command interfaces found. '%s' expected.",
                   joint.name.c_str(), joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.command_interfaces[2].name != hardware_interface::HW_IF_EFFORT) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s command interfaces found. '%s' expected.",
                   joint.name.c_str(), joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_EFFORT);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces.size() != interfaces_per_joint_) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' has %zu state interface. 3 expected.",
                   joint.name.c_str(), joint.state_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s state interface. '%s' expected.",
                   joint.name.c_str(), joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s command interfaces found. '%s' expected.",
                   joint.name.c_str(), joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[2].name != hardware_interface::HW_IF_EFFORT) {
      RCLCPP_FATAL(rclcpp::get_logger("SpotHardware"), "Joint '%s' have %s command interfaces found. '%s' expected.",
                   joint.name.c_str(), joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_EFFORT);
      return hardware_interface::CallbackReturn::ERROR;
    }
  }
  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SpotHardware::on_configure(const rclcpp_lifecycle::State& /*previous_state*/) {
  // reset values always when configuring hardware
  hw_states_.assign(hw_states_.size(), 0);
  hw_commands_.assign(hw_commands_.size(), 0);
  // allocate the sizes of these vectors ahead of time
  joint_states_.position.assign(njoints_, 0);
  joint_states_.velocity.assign(njoints_, 0);
  joint_states_.load.assign(njoints_, 0);

  // Set up the robot using the BD SDK and start command streaming.
  if (!authenticate_robot(hostname_, username_, password_)) {
    return hardware_interface::CallbackReturn::ERROR;
  }
  if (!start_time_sync()) {
    return hardware_interface::CallbackReturn::ERROR;
  }
  if (!start_state_stream(std::bind(&StateStreamingHandler::handle_state_streaming, &state_streaming_handler_,
                                    std::placeholders::_1))) {
    return hardware_interface::CallbackReturn::ERROR;
  }

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SpotHardware::on_activate(const rclcpp_lifecycle::State& /*previous_state*/) {
  // This can be added when we start command streaming.

  // if (!check_estop()) {
  //   return hardware_interface::CallbackReturn::ERROR;
  // }
  // if (!get_lease()) {
  //   return hardware_interface::CallbackReturn::ERROR;
  // }
  // if (!power_on()) {
  //   release_lease();
  //   return hardware_interface::CallbackReturn::ERROR;
  // }

  // Once command streaming is implemented, this initialization should go here.

  return hardware_interface::CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface> SpotHardware::export_state_interfaces() {
  std::vector<hardware_interface::StateInterface> state_interfaces;
  for (size_t i = 0; i < info_.joints.size(); i++) {
    const auto& joint = info_.joints.at(i);
    state_interfaces.emplace_back(hardware_interface::StateInterface(joint.name, hardware_interface::HW_IF_POSITION,
                                                                     &hw_states_[interfaces_per_joint_ * i]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(joint.name, hardware_interface::HW_IF_VELOCITY,
                                                                     &hw_states_[interfaces_per_joint_ * i + 1]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(joint.name, hardware_interface::HW_IF_EFFORT,
                                                                     &hw_states_[interfaces_per_joint_ * i + 2]));
  }
  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> SpotHardware::export_command_interfaces() {
  std::vector<hardware_interface::CommandInterface> command_interfaces;
  for (uint i = 0; i < info_.joints.size(); i++) {
    const auto& joint = info_.joints.at(i);
    command_interfaces.emplace_back(hardware_interface::CommandInterface(joint.name, hardware_interface::HW_IF_POSITION,
                                                                         &hw_commands_[interfaces_per_joint_ * i]));
    command_interfaces.emplace_back(hardware_interface::CommandInterface(joint.name, hardware_interface::HW_IF_VELOCITY,
                                                                         &hw_commands_[interfaces_per_joint_ * i + 1]));
    command_interfaces.emplace_back(hardware_interface::CommandInterface(joint.name, hardware_interface::HW_IF_EFFORT,
                                                                         &hw_commands_[interfaces_per_joint_ * i + 2]));
  }
  return command_interfaces;
}

hardware_interface::CallbackReturn SpotHardware::on_deactivate(const rclcpp_lifecycle::State& /*previous_state*/) {
  // Once command streaming is enabled, this should release the lease and stop command streaming
  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SpotHardware::on_shutdown(const rclcpp_lifecycle::State& /*previous_state*/) {
  stop_state_stream();
  // Once command streaming is enabled, this should also release the lease and stop command streaming
  // (if not already deactivated)
  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SpotHardware::on_cleanup(const rclcpp_lifecycle::State& /*previous_state*/) {
  stop_state_stream();
  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type SpotHardware::read(const rclcpp::Time& /*time*/, const rclcpp::Duration& /*period*/) {
  state_streaming_handler_.get_joint_states(joint_states_);
  const auto& joint_pos = joint_states_.position;
  const auto& joint_vel = joint_states_.velocity;
  const auto& joint_load = joint_states_.load;
  // wait for them to be initialized
  if (joint_pos.empty() || joint_vel.empty() || joint_load.empty()) {
    return hardware_interface::return_type::OK;
  }
  // Ensure that the states received from the Spot SDK will fit into the hw_states_ vector
  const auto states_size = hw_states_.size();
  if (interfaces_per_joint_ * joint_pos.size() != states_size ||
      interfaces_per_joint_ * joint_vel.size() != states_size ||
      interfaces_per_joint_ * joint_load.size() != states_size) {
    RCLCPP_FATAL(
        rclcpp::get_logger("SpotHardware"),
        "The number of joints and interfaces does not match with the outputted joint states from the Spot SDK!");
    return hardware_interface::return_type::ERROR;
  }
  // Read values into joint states
  for (size_t i = 0; i < joint_pos.size(); ++i) {
    hw_states_.at(i * interfaces_per_joint_) = joint_pos.at(i);
    hw_states_.at(i * interfaces_per_joint_ + 1) = joint_vel.at(i);
    hw_states_.at(i * interfaces_per_joint_ + 2) = joint_load.at(i);
  }
  return hardware_interface::return_type::OK;
}

hardware_interface::return_type SpotHardware::write(const rclcpp::Time& /*time*/, const rclcpp::Duration& /*period*/) {
  // This function will be responsible for sending commands to the robot via the BD SDK -- currently unimplemented.
  return hardware_interface::return_type::OK;
}

bool SpotHardware::authenticate_robot(const std::string& hostname, const std::string& username,
                                      const std::string& password) {
  if (robot_authenticated_) {
    RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Robot already authenticated!");
    return true;
  }
  // Create a Client SDK object.
  const auto client_sdk = ::bosdyn::client::CreateStandardSDK("SpotHardware");
  auto robot_result = client_sdk->CreateRobot(hostname);
  if (!robot_result) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not create robot");
    return false;
  }
  robot_ = robot_result.move();
  const ::bosdyn::common::Status status = robot_->Authenticate(username, password);
  if (!status) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"),
                 "Could not authenticate robot with hostname: %s username: %s password: %s", hostname.c_str(),
                 username.c_str(), password.c_str());
    return false;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Robot successfully authenticated!");
  robot_authenticated_ = true;
  return true;
}

bool SpotHardware::start_time_sync() {
  auto start_response = robot_->StartTimeSync();
  auto time_sync_thread_resp = robot_->GetTimeSyncThread();
  if (!time_sync_thread_resp.status) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not get time sync thread from robot");
    return false;
  }
  auto time_sync_thread = time_sync_thread_resp.response;
  if (!time_sync_thread->WaitForSync(std::chrono::seconds(5))) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Failed to establish time sync before timing out");
    return false;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Time sync complete");
  return true;
}

bool SpotHardware::check_estop() {
  // Verify the robot is not estopped and that an external application has registered and holds
  // an estop endpoint.
  const auto estop_status = robot_->IsEstopped();
  if (!estop_status) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not check estop status");
    return false;
  }
  if (estop_status.response) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Robot is e-stopped, cannot continue.");
    return false;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Estop check complete!");
  return true;
}

bool SpotHardware::get_lease() {
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Getting Lease");
  // First create a lease client.
  ::bosdyn::client::Result<::bosdyn::client::LeaseClient*> lease_client_resp =
      robot_->EnsureServiceClient<::bosdyn::client::LeaseClient>();
  if (!lease_client_resp) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not create lease client");
    return false;
  }
  lease_client_ = lease_client_resp.response;
  // Then acquire the lease for the body.
  const auto lease_res = lease_client_->AcquireLease("body");
  if (!lease_res) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not acquire body lease");
    return false;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Lease acquired!!");
  return true;
}

bool SpotHardware::power_on() {
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Powering on...");
  const auto power_status = robot_->PowerOnMotors(std::chrono::seconds(60), 1.0);
  if (!power_status) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not power on the robot");
    return false;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Powered on!");
  return true;
}

void state_stream_loop(std::stop_token stop_token, ::bosdyn::client::RobotStateStreamingClient* stateStreamClient,
                       StateHandler&& state_policy) {
  ::bosdyn::api::RobotStateStreamResponse latest_state_stream_response;

  while (!stop_token.stop_requested()) {
    // Get robot state stream
    auto robot_state_stream = stateStreamClient->GetRobotStateStream();
    if (!robot_state_stream) {
      RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Failed to get robot state");
      continue;
    }
    latest_state_stream_response = std::move(robot_state_stream.response);
    state_policy(latest_state_stream_response);
  }
}

bool SpotHardware::start_state_stream(StateHandler&& state_policy) {
  if (state_stream_started_) {
    RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "State stream has already been started!");
    return true;
  }
  // Start state streaming
  auto robot_state_stream_client_resp = robot_->EnsureServiceClient<::bosdyn::client::RobotStateStreamingClient>();
  if (!robot_state_stream_client_resp) {
    RCLCPP_ERROR(rclcpp::get_logger("SpotHardware"), "Could not create robot state client");
    return false;
  }
  state_client_ = robot_state_stream_client_resp.move();
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Robot State Client created");

  state_thread_ = std::jthread(&spot_ros2_control::state_stream_loop, state_client_, state_policy);
  state_stream_started_ = true;
  return true;
}

void SpotHardware::stop_state_stream() {
  if (!state_stream_started_) {
    RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "State stream already stopped");
    return;
  }
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Stopping State Stream");
  state_thread_.request_stop();
  state_thread_.join();
  state_stream_started_ = false;
}

void SpotHardware::release_lease() {
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Releasing Lease");
  bosdyn::api::ReturnLeaseRequest msg;
  auto lease_result = robot_->GetWallet()->GetOwnedLeaseProto("body");
  msg.mutable_lease()->CopyFrom(lease_result.response);
  auto resp = lease_client_->ReturnLease(msg);
  RCLCPP_INFO(rclcpp::get_logger("SpotHardware"), "Return lease status: %s", resp.status.DebugString().c_str());
}

}  // namespace spot_ros2_control

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(spot_ros2_control::SpotHardware, hardware_interface::SystemInterface)