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

#include "data_sink_operator.hpp"

namespace holoscan {
namespace ops {

void DataSinkOperator::setup(OperatorSpec& spec) {
  spec.input<int>("data");
}

void DataSinkOperator::compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) {
  auto data = op_input.receive<int>("data");
  if (data) {
    std::cout << "Received data: " << *data << std::endl;
    received_count_++;
  }
}

}  // namespace ops
}  // namespace holoscan
