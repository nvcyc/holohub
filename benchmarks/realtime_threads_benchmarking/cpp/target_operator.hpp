/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TARGET_OPERATOR_HPP
#define TARGET_OPERATOR_HPP

#include <chrono>
#include <mutex>
#include <vector>

#include <holoscan/holoscan.hpp>

namespace holoscan {
namespace ops {

class TargetOperator : public Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(TargetOperator)

  TargetOperator(int target_fps = 60) : target_fps_(target_fps), target_period_ns_(static_cast<int64_t>(1e9 / target_fps)) {}

  void setup(OperatorSpec& spec) override;
  void compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) override;

  // Get comprehensive performance statistics and raw data
  struct Statistics {
    int target_fps;
    int frame_count;
    double total_duration_s;
    struct {
      double mean_ms;
      double std_ms;
      double min_ms;
      double max_ms;
    } frame_period_stats;
    struct {
      double mean_ms;
      double std_ms;
      double min_ms;
      double max_ms;
    } execution_time_stats;
    std::vector<double> frame_periods_ms;
    std::vector<double> execution_times_ms;
  };

  Statistics get_statistics() const;

 private:
  void do_real_work();

  int target_fps_;
  int64_t target_period_ns_;
  std::vector<double> frame_periods_;
  std::vector<double> execution_times_;
  int frame_count_;
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::steady_clock::time_point last_frame_time_;
  mutable std::mutex lock_;
  double work_result_;
};

}  // namespace ops
}  // namespace holoscan

#endif /* TARGET_OPERATOR_HPP */
