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

#include "load_operator.hpp"

#include <map>

namespace holoscan {
namespace ops {


void LoadOperator::compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) {
  // Perform CPU-intensive work
  consume_cpu();

  iterations_++;
  std::map<std::string, int> data{{"iteration", iterations_}};
  op_output.emit(data, "load_data");
}

void LoadOperator::consume_cpu() {
  // Convert milliseconds to seconds
  double work_duration = load_duration_ms_ / 1000.0;
  auto end_time = std::chrono::steady_clock::now() +
                  std::chrono::duration<double>(work_duration);

  // Busy loop to consume CPU
  int dummy = 0;
  while (std::chrono::steady_clock::now() < end_time) {
    dummy++;
    if (dummy > 1000000) {  // Prevent overflow
      dummy = 0;
    }
  }
}

}  // namespace ops
}  // namespace holoscan
