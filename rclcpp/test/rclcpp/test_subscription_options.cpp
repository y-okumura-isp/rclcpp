// Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"
#include "rclcpp/subscription_options.hpp"

#include "../utils/rclcpp_gtest_macros.hpp"

using namespace std::chrono_literals;

namespace
{
constexpr const char defaultPublishTopic[] = "/statistics";
}

class TestSubscriptionOptions : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }

protected:
  void initialize(const rclcpp::NodeOptions & node_options = rclcpp::NodeOptions())
  {
    node = std::make_shared<rclcpp::Node>("test_subscription_options", node_options);
  }

  void TearDown()
  {
    node.reset();
  }

  rclcpp::Node::SharedPtr node;
};

TEST_F(TestSubscriptionOptions, topic_statistics_options_default_and_set) {
  auto options = rclcpp::SubscriptionOptions();

  EXPECT_EQ(options.topic_stats_options.state, rclcpp::TopicStatisticsState::NodeDefault);
  EXPECT_EQ(options.topic_stats_options.publish_topic, defaultPublishTopic);
  EXPECT_EQ(options.topic_stats_options.publish_period, 1s);

  options.topic_stats_options.state = rclcpp::TopicStatisticsState::Enable;
  options.topic_stats_options.publish_topic = "topic_statistics";
  options.topic_stats_options.publish_period = 5min;

  EXPECT_EQ(options.topic_stats_options.state, rclcpp::TopicStatisticsState::Enable);
  EXPECT_EQ(options.topic_stats_options.publish_topic, "topic_statistics");
  EXPECT_EQ(options.topic_stats_options.publish_period, 5min);
}

TEST_F(TestSubscriptionOptions, topic_statistics_options_node_default_mode) {
  initialize();
  auto subscription_options = rclcpp::SubscriptionOptions();

  EXPECT_EQ(
    subscription_options.topic_stats_options.state,
    rclcpp::TopicStatisticsState::NodeDefault);
  EXPECT_FALSE(
    rclcpp::detail::resolve_enable_topic_statistics(
      subscription_options,
      *(node->get_node_base_interface())));

  initialize(rclcpp::NodeOptions().enable_topic_statistics(true));
  EXPECT_TRUE(
    rclcpp::detail::resolve_enable_topic_statistics(
      subscription_options,
      *(node->get_node_base_interface())));

  subscription_options.topic_stats_options.state = rclcpp::TopicStatisticsState::Disable;
  EXPECT_FALSE(
    rclcpp::detail::resolve_enable_topic_statistics(
      subscription_options,
      *(node->get_node_base_interface())));

  subscription_options.topic_stats_options.state = static_cast<rclcpp::TopicStatisticsState>(5);
  RCLCPP_EXPECT_THROW_EQ(
    rclcpp::detail::resolve_enable_topic_statistics(
      subscription_options,
      *(node->get_node_base_interface())),
    std::runtime_error("Unrecognized EnableTopicStatistics value"));
}

TEST_F(TestSubscriptionOptions, use_default_callbacks_setters) {
  auto option = rclcpp::SubscriptionOptions().disable_use_default_callbacks();
  EXPECT_EQ(option.use_default_callbacks, false);

  option.enable_use_default_callbacks();
  EXPECT_EQ(option.use_default_callbacks, true);
}

TEST_F(TestSubscriptionOptions, ignore_local_publications_setters) {
  auto option = rclcpp::SubscriptionOptions().disable_ignore_local_publications();
  EXPECT_EQ(option.ignore_local_publications, false);

  option.enable_ignore_local_publications();
  EXPECT_EQ(option.ignore_local_publications, true);
}

TEST_F(TestSubscriptionOptions, callback_group_setters) {
  auto group = std::make_shared<rclcpp::CallbackGroup>(rclcpp::CallbackGroupType::Reentrant);
  auto option = rclcpp::SubscriptionOptions().set_callback_group(group);
  EXPECT_EQ(option.callback_group, group);
}

TEST_F(TestSubscriptionOptions, use_intra_process_com_setters) {
  auto option = rclcpp::SubscriptionOptions().enable_use_intra_process_comm();
  EXPECT_EQ(option.use_intra_process_comm, rclcpp::IntraProcessSetting::Enable);

  option.disable_use_intra_process_comm();
  EXPECT_EQ(option.use_intra_process_comm, rclcpp::IntraProcessSetting::Disable);

  option.clear_use_intra_process_comm();
  EXPECT_EQ(option.use_intra_process_comm, rclcpp::IntraProcessSetting::NodeDefault);
}
