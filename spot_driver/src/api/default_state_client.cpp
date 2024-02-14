// Copyright (c) 2024 Boston Dynamics AI Institute LLC. All rights reserved.

#include <bosdyn/api/robot_state.pb.h>
#include <spot_driver/api/default_state_client.hpp>
#include <spot_driver/api/time_sync_api.hpp>
#include <spot_driver/conversions/geometry.hpp>
#include <spot_driver/conversions/robot_state.hpp>

namespace spot_ros2 {

DefaultStateClient::DefaultStateClient(::bosdyn::client::RobotStateClient* client) : client_{client} {}

tl::expected<bosdyn::api::RobotState, std::string> DefaultStateClient::getRobotState() {
  std::shared_future<::bosdyn::client::RobotStateResultType> get_robot_state_result_future =
      client_->GetRobotStateAsync();

  const ::bosdyn::client::RobotStateResultType& get_robot_state_result = get_robot_state_result_future.get();
  if (!get_robot_state_result.status || !get_robot_state_result.response.has_robot_state()) {
    return tl::make_unexpected("Failed to get robot state: " + get_robot_state_result.status.DebugString());
  }

  return get_robot_state_result.response.robot_state();
}

}  // namespace spot_ros2
