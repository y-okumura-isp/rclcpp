// Copyright 2019 Intel Corporation
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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rclcpp/parameter_events_subscriber.hpp"
#include "rcpputils/join.hpp"

namespace rclcpp
{

ParameterEventsSubscriber::ParameterEventsSubscriber(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics,
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging,
  const rclcpp::QoS & qos)
: node_base_(node_base),
  node_topics_(node_topics),
  node_logging_(node_logging),
  qos_(qos)
{
  event_subscription_ = rclcpp::create_subscription<rcl_interfaces::msg::ParameterEvent>(
    node_topics_, "/parameter_events", qos_,
    std::bind(&ParameterEventsSubscriber::event_callback, this, std::placeholders::_1));
}

ParameterEventCallbackHandle::SharedPtr
ParameterEventsSubscriber::add_parameter_event_callback(
  ParameterEventCallbackType callback)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto handle = std::make_shared<ParameterEventCallbackHandle>();
  handle->callback = callback;
  event_callbacks_.emplace_front(handle);

  return handle;
}

void
ParameterEventsSubscriber::remove_parameter_event_callback(
  const ParameterEventCallbackHandle * const handle)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = std::find_if(
    event_callbacks_.begin(),
    event_callbacks_.end(),
    HandleCompare<ParameterEventCallbackHandle>(handle));
  if (it != event_callbacks_.end()) {
    event_callbacks_.erase(it);
  } else {
    throw std::runtime_error("Callback doesn't exist");
  }
}

ParameterCallbackHandle::SharedPtr
ParameterEventsSubscriber::add_parameter_callback(
  const std::string & parameter_name,
  ParameterCallbackType callback,
  const std::string & node_name)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto full_node_name = resolve_path(node_name);

  auto handle = std::make_shared<ParameterCallbackHandle>();
  handle->callback = callback;
  handle->parameter_name = parameter_name;
  handle->node_name = full_node_name;
  // the last callback registered is executed first.
  parameter_callbacks_[{parameter_name, full_node_name}].emplace_front(handle);

  return handle;
}

void
ParameterEventsSubscriber::remove_parameter_callback(
  const ParameterCallbackHandle * const handle)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto & container = parameter_callbacks_[{handle->parameter_name, handle->node_name}];
  auto it = std::find_if(
    container.begin(),
    container.end(),
    HandleCompare<ParameterCallbackHandle>(handle));
  if (it != container.end()) {
    container.erase(it);
    if (container.empty()) {
      parameter_callbacks_.erase({handle->parameter_name, handle->node_name});
    }
  } else {
    throw std::runtime_error("Callback doesn't exist");
  }
}

void
ParameterEventsSubscriber::remove_parameter_callback(
  const std::string & parameter_name,
  const std::string & node_name)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto full_node_name = resolve_path(node_name);
  parameter_callbacks_.erase({parameter_name, full_node_name});
}

bool
ParameterEventsSubscriber::get_parameter_from_event(
  const rcl_interfaces::msg::ParameterEvent::SharedPtr event,
  rclcpp::Parameter & parameter,
  const std::string parameter_name,
  const std::string node_name)
{
  if (event->node == node_name) {
    rclcpp::ParameterEventsFilter filter(event, {parameter_name},
      {rclcpp::ParameterEventsFilter::EventType::NEW,
        rclcpp::ParameterEventsFilter::EventType::CHANGED});
    if (!filter.get_events().empty()) {
      const auto & events = filter.get_events();
      auto param_msg = events.back().second;
      parameter = rclcpp::Parameter::from_parameter_msg(*param_msg);
      return true;
    }
  }
  return false;
}

rclcpp::Parameter
ParameterEventsSubscriber::get_parameter_from_event(
  const rcl_interfaces::msg::ParameterEvent::SharedPtr event,
  const std::string parameter_name,
  const std::string node_name)
{
  rclcpp::Parameter p;
  if (!get_parameter_from_event(event, p, parameter_name, node_name)) {
    throw std::runtime_error(
            "Parameter '" + parameter_name + "' of node '" + node_name +
            "' is not part of parameter event");
  }
  return p;
}

void
ParameterEventsSubscriber::event_callback(
  const rcl_interfaces::msg::ParameterEvent::SharedPtr event)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const std::string & node_name = event->node;
  RCLCPP_DEBUG(node_logging_->get_logger().get_child(
      "ParameterEventsSubscriber"), "Parameter event received for node: %s", node_name.c_str());

  for (auto it = parameter_callbacks_.begin(); it != parameter_callbacks_.end(); ++it) {
    rclcpp::Parameter p;
    if (get_parameter_from_event(event, p, it->first.first, it->first.second)) {
      for (auto cb = it->second.begin(); cb != it->second.end(); ++cb) {
        auto shared_handle = cb->lock();
        if (nullptr != shared_handle) {
          shared_handle->callback(p);
        } else {
          cb = it->second.erase(cb);
        }
      }
    }
  }

  for (auto event_cb = event_callbacks_.begin(); event_cb != event_callbacks_.end(); ++event_cb) {
    auto shared_event_handle = event_cb->lock();
    if (nullptr != shared_event_handle) {
      shared_event_handle->callback(event);
    } else {
      event_cb = event_callbacks_.erase(event_cb);
    }
  }
}

std::string
ParameterEventsSubscriber::resolve_path(const std::string & path)
{
  std::string full_path;

  if (path == "") {
    full_path = node_base_->get_fully_qualified_name();
  } else {
    full_path = path;
    if (*full_path.begin() != '/') {
      const std::vector<std::string> paths{node_base_->get_namespace(), full_path};
      full_path = rcpputils::join(paths, "/");
    }
  }

  return full_path;
}

}  // namespace rclcpp
