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

#include "target_operator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace holoscan {
namespace ops {


void TargetOperator::compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) {
  auto current_time = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(lock_);
    if (start_time_ == std::chrono::steady_clock::time_point{}) {
      // First frame - just initialize, don't calculate interval
      start_time_ = current_time;
      last_frame_time_ = current_time;
    } else {
      // Calculate frame period for all frames after the first
      auto frame_period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          current_time - last_frame_time_).count();
      frame_periods_.push_back(frame_period_ns);
      last_frame_time_ = current_time;
    }

    frame_count_++;
  }

  do_real_work();
  auto execution_end = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(lock_);
    auto execution_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        execution_end - current_time).count();
    execution_times_.push_back(execution_time_ns);
  }
}

TargetOperator::Statistics TargetOperator::get_statistics() const {
  std::lock_guard<std::mutex> lock(lock_);
  
  Statistics stats;
  stats.target_fps = target_fps_;
  stats.frame_count = frame_count_;
  
  if (frame_periods_.empty()) {
    stats.total_duration_s = 0.0;
    return stats;
  }

  // Calculate basic timing
  auto total_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      last_frame_time_ - start_time_).count();
  stats.total_duration_s = total_duration_ns / 1e9;

  // Frame period statistics (convert to milliseconds)
  stats.frame_periods_ms.resize(frame_periods_.size());
  std::transform(frame_periods_.begin(), frame_periods_.end(), stats.frame_periods_ms.begin(),
                 [](double ns) { return ns / 1e6; });

  if (!stats.frame_periods_ms.empty()) {
    double sum = std::accumulate(stats.frame_periods_ms.begin(), stats.frame_periods_ms.end(), 0.0);
    double mean = sum / stats.frame_periods_ms.size();
    
    double sq_sum = std::inner_product(stats.frame_periods_ms.begin(), stats.frame_periods_ms.end(),
                                      stats.frame_periods_ms.begin(), 0.0);
    double std_dev = std::sqrt(sq_sum / stats.frame_periods_ms.size() - mean * mean);
    
    auto [min_it, max_it] = std::minmax_element(stats.frame_periods_ms.begin(), stats.frame_periods_ms.end());
    
    stats.frame_period_stats.mean_ms = mean;
    stats.frame_period_stats.std_ms = std_dev;
    stats.frame_period_stats.min_ms = *min_it;
    stats.frame_period_stats.max_ms = *max_it;
  }

  // Execution time statistics (convert to milliseconds)
  stats.execution_times_ms.resize(execution_times_.size());
  std::transform(execution_times_.begin(), execution_times_.end(), stats.execution_times_ms.begin(),
                 [](double ns) { return ns / 1e6; });

  if (!stats.execution_times_ms.empty()) {
    double sum = std::accumulate(stats.execution_times_ms.begin(), stats.execution_times_ms.end(), 0.0);
    double mean = sum / stats.execution_times_ms.size();
    
    double sq_sum = std::inner_product(stats.execution_times_ms.begin(), stats.execution_times_ms.end(),
                                      stats.execution_times_ms.begin(), 0.0);
    double std_dev = std::sqrt(sq_sum / stats.execution_times_ms.size() - mean * mean);
    
    auto [min_it, max_it] = std::minmax_element(stats.execution_times_ms.begin(), stats.execution_times_ms.end());
    
    stats.execution_time_stats.mean_ms = mean;
    stats.execution_time_stats.std_ms = std_dev;
    stats.execution_time_stats.min_ms = *min_it;
    stats.execution_time_stats.max_ms = *max_it;
  }

  return stats;
}

void TargetOperator::do_real_work() {
  // Simulate image processing or data analysis work
  // Matrix-like computations
  std::vector<double> data(1000);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = std::sin(i * 0.01) * std::cos(i * 0.02);
  }

  work_result_ = 0.0;
  for (double x : data) {
    work_result_ += std::sqrt(std::abs(x) + 1.0);
  }
}

}  // namespace ops
}  // namespace holoscan
