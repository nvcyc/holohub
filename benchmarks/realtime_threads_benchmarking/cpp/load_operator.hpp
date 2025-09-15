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

#ifndef LOAD_OPERATOR_HPP
#define LOAD_OPERATOR_HPP

#include <chrono>

#include <holoscan/holoscan.hpp>

namespace holoscan {
namespace ops {

class LoadOperator : public Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(LoadOperator)

  LoadOperator(double load_duration_ms = 10.0) : load_duration_ms_(load_duration_ms), iterations_(0) {}

  void compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) override;

 private:
  void consume_cpu();

  double load_duration_ms_;
  int iterations_;
};

}  // namespace ops
}  // namespace holoscan

#endif /* LOAD_OPERATOR_HPP */
