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

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include <holoscan/holoscan.hpp>

#include "target_operator.hpp"
#include "load_operator.hpp"
#include "data_sink_operator.hpp"

using namespace holoscan;

class RealtimeThreadBenchmark : public Application {
 public:
  RealtimeThreadBenchmark(int target_fps = 60,
                         int duration_seconds = 30,
                         bool use_realtime = false,
                         SchedulingPolicy scheduling_policy = SchedulingPolicy::kDeadline,
                         double load_duration_ms = 10.0)
      : target_fps_(target_fps),
        duration_seconds_(duration_seconds),
        use_realtime_(use_realtime),
        scheduling_policy_(scheduling_policy),
        load_duration_ms_(load_duration_ms) {}

  void compose() override {
    // Create operators
    auto target_op = make_operator<ops::TargetOperator>("target_op", target_fps_);
    auto load_op1 = make_operator<ops::LoadOperator>("load_op1", load_duration_ms_);
    auto load_op2 = make_operator<ops::LoadOperator>("load_op2", load_duration_ms_);
    auto sink_op = make_operator<ops::DataSinkOperator>("sink_op");

    // Store reference to target operator for statistics
    target_operator_ = target_op;

    // Create thread pools with limited worker threads to create contention
    if (use_realtime_) {
      // Realtime pool for target operator
      auto realtime_pool = make_thread_pool("realtime_pool", 1);

      if (scheduling_policy_ == SchedulingPolicy::kDeadline) {
        int64_t period_ns = static_cast<int64_t>(1e9 / target_fps_);
        int64_t deadline_ns = static_cast<int64_t>(period_ns * 0.95);  // 95% of period
        int64_t runtime_ns = static_cast<int64_t>(period_ns * 0.10);   // 10% of period

        realtime_pool->add_realtime(target_op, scheduling_policy_, true, {0},
                                   runtime_ns, deadline_ns, period_ns);
      } else if (scheduling_policy_ == SchedulingPolicy::kFirstInFirstOut ||
                 scheduling_policy_ == SchedulingPolicy::kRoundRobin) {
        realtime_pool->add_realtime(target_op, scheduling_policy_, true, {0}, 99);
      }

      // Regular pool for load operators (competing for resources)
      auto load_pool = make_thread_pool("load_pool", 1);
      load_pool->add(load_op1, true, {1});
      load_pool->add(load_op2, true, {1});
    } else {
      // All operators share the same pool (no realtime scheduling)
      auto shared_pool = make_thread_pool("shared_pool", 2);
      shared_pool->add(target_op, true, {0, 1});
      shared_pool->add(load_op1, true, {0, 1});
      shared_pool->add(load_op2, true, {0, 1});
    }

    // Connect operators
    add_flow(target_op, sink_op, {{"frame", "data"}});
    add_flow(load_op1, sink_op, {{"load_data", "data"}});
    add_flow(load_op2, sink_op, {{"load_data", "data"}});
  }

  ops::TargetOperator::Statistics get_benchmark_results() {
    return target_operator_->get_statistics();
  }

 private:
  int target_fps_;
  int duration_seconds_;
  bool use_realtime_;
  SchedulingPolicy scheduling_policy_;
  double load_duration_ms_;
  std::shared_ptr<ops::TargetOperator> target_operator_;
};

struct BenchmarkResults {
  int target_fps;
  int frame_count;
  double total_duration_s;
  double actual_duration_s;
  bool use_realtime;
  std::string scheduling_policy;
  double load_duration_ms;
  
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

BenchmarkResults run_benchmark(int target_fps,
                              int duration_seconds,
                              bool use_realtime,
                              SchedulingPolicy scheduling_policy,
                              double load_duration_ms) {
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "Running benchmark:" << std::endl;
  std::cout << "  Target FPS: " << target_fps << std::endl;
  std::cout << "  Duration: " << duration_seconds << "s" << std::endl;
  std::cout << "  Realtime: " << (use_realtime ? "true" : "false") << std::endl;
  std::cout << "  Policy: " << (use_realtime ? 
      (scheduling_policy == SchedulingPolicy::kDeadline ? "SCHED_DEADLINE" :
       scheduling_policy == SchedulingPolicy::kFirstInFirstOut ? "SCHED_FIFO" : "SCHED_RR") 
      : "Normal") << std::endl;
  std::cout << "  Load Duration: " << load_duration_ms << "ms per operator call" << std::endl;
  std::cout << "  EBS Worker Threads: 3" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  auto app = std::make_shared<RealtimeThreadBenchmark>(
      target_fps, duration_seconds, use_realtime, scheduling_policy, load_duration_ms);

  // Calculate scheduler timeout with reasonable buffer
  int64_t startup_buffer_ms = 1000;   // 1s for application startup
  int64_t shutdown_buffer_ms = 1000;  // 1s for graceful shutdown
  int64_t overhead_buffer_ms = std::min(std::max(500L, static_cast<int64_t>(duration_seconds * 20)), 5000L);

  int64_t max_duration_ms = duration_seconds * 1000 + startup_buffer_ms + shutdown_buffer_ms + overhead_buffer_ms;

  auto scheduler = app->make_scheduler<EventBasedScheduler>(
      "benchmark_scheduler",
      Arg("worker_thread_number", static_cast<int64_t>(3)),
      Arg("max_duration_ms", max_duration_ms));
  app->scheduler(scheduler);

  // Run the application
  auto start_time = std::chrono::steady_clock::now();
  app->run();
  auto end_time = std::chrono::steady_clock::now();

  // Get results and raw data
  auto results = app->get_benchmark_results();

  BenchmarkResults benchmark_results;
  benchmark_results.target_fps = results.target_fps;
  benchmark_results.frame_count = results.frame_count;
  benchmark_results.total_duration_s = results.total_duration_s;
  benchmark_results.actual_duration_s = std::chrono::duration<double>(end_time - start_time).count();
  benchmark_results.use_realtime = use_realtime;
  benchmark_results.scheduling_policy = use_realtime ? 
      (scheduling_policy == SchedulingPolicy::kDeadline ? "SCHED_DEADLINE" :
       scheduling_policy == SchedulingPolicy::kFirstInFirstOut ? "SCHED_FIFO" : "SCHED_RR") 
      : "Normal";
  benchmark_results.load_duration_ms = load_duration_ms;
  benchmark_results.frame_period_stats.mean_ms = results.frame_period_stats.mean_ms;
  benchmark_results.frame_period_stats.std_ms = results.frame_period_stats.std_ms;
  benchmark_results.frame_period_stats.min_ms = results.frame_period_stats.min_ms;
  benchmark_results.frame_period_stats.max_ms = results.frame_period_stats.max_ms;
  benchmark_results.execution_time_stats.mean_ms = results.execution_time_stats.mean_ms;
  benchmark_results.execution_time_stats.std_ms = results.execution_time_stats.std_ms;
  benchmark_results.execution_time_stats.min_ms = results.execution_time_stats.min_ms;
  benchmark_results.execution_time_stats.max_ms = results.execution_time_stats.max_ms;
  benchmark_results.frame_periods_ms = results.frame_periods_ms;
  benchmark_results.execution_times_ms = results.execution_times_ms;

  return benchmark_results;
}

void print_results(const BenchmarkResults& results) {
  std::cout << "\nBenchmark Results:" << std::endl;
  std::string rt_status = results.use_realtime ? "RT" : "Normal";
  std::cout << "  Configuration: " << results.scheduling_policy << " (" << rt_status << ")" << std::endl;
  std::cout << "  Target FPS: " << results.target_fps << std::endl;

  // KEY METRICS FIRST - Frame timing consistency
  if (results.frame_period_stats.std_ms > 0) {
    double target_period_ms = 1000.0 / results.target_fps;
    std::cout << "  ★ Frame Period Std Dev: " << std::fixed << std::setprecision(3) 
              << results.frame_period_stats.std_ms << "ms  ← KEY METRIC" << std::endl;
    std::cout << "  Frame Period Mean: " << std::fixed << std::setprecision(3) 
              << results.frame_period_stats.mean_ms << "ms (Target: " 
              << std::fixed << std::setprecision(1) << target_period_ms << "ms)" << std::endl;
  }

  // SECONDARY METRICS - Execution consistency
  if (results.execution_time_stats.std_ms > 0) {
    std::cout << "  Execution Time Std Dev: " << std::fixed << std::setprecision(3) 
              << results.execution_time_stats.std_ms << "ms" << std::endl;
    std::cout << "  Execution Time Mean: " << std::fixed << std::setprecision(3) 
              << results.execution_time_stats.mean_ms << "ms" << std::endl;
  }

  // DETAILED RANGES - Less critical but informative
  if (results.frame_period_stats.std_ms > 0 && results.execution_time_stats.std_ms > 0) {
    std::cout << "  Frame Period Min/Max: " << std::fixed << std::setprecision(1) 
              << results.frame_period_stats.min_ms << "ms / " 
              << results.frame_period_stats.max_ms << "ms" << std::endl;
    std::cout << "  Execution Range: " << std::fixed << std::setprecision(3) 
              << results.execution_time_stats.min_ms << "ms - " 
              << results.execution_time_stats.max_ms << "ms" << std::endl;
  }

  // TEST VALIDATION INFO - Basic metadata last
  std::cout << "  Frame Count: " << results.frame_count << std::endl;
  std::cout << "  Total Duration: " << std::fixed << std::setprecision(2) 
            << results.total_duration_s << "s" << std::endl;
  std::cout << "  Load Duration: " << std::fixed << std::setprecision(1) 
            << results.load_duration_ms << "ms per call" << std::endl;
}

void save_results_to_json(const BenchmarkResults& normal_results,
                         const BenchmarkResults& realtime_results,
                         const std::string& filename) {
  std::ofstream file(filename);
  file << "{\n";
  
  // Normal results
  file << "  \"normal\": {\n";
  file << "    \"target_fps\": " << normal_results.target_fps << ",\n";
  file << "    \"frame_count\": " << normal_results.frame_count << ",\n";
  file << "    \"total_duration_s\": " << std::fixed << std::setprecision(6) << normal_results.total_duration_s << ",\n";
  file << "    \"actual_duration_s\": " << std::fixed << std::setprecision(6) << normal_results.actual_duration_s << ",\n";
  file << "    \"use_realtime\": " << (normal_results.use_realtime ? "true" : "false") << ",\n";
  file << "    \"scheduling_policy\": \"" << normal_results.scheduling_policy << "\",\n";
  file << "    \"load_duration_ms\": " << std::fixed << std::setprecision(1) << normal_results.load_duration_ms;
  
  if (!normal_results.frame_periods_ms.empty()) {
    file << ",\n    \"frame_period_stats\": {\n";
    file << "      \"mean_ms\": " << std::fixed << std::setprecision(6) << normal_results.frame_period_stats.mean_ms << ",\n";
    file << "      \"std_ms\": " << std::fixed << std::setprecision(6) << normal_results.frame_period_stats.std_ms << ",\n";
    file << "      \"min_ms\": " << std::fixed << std::setprecision(6) << normal_results.frame_period_stats.min_ms << ",\n";
    file << "      \"max_ms\": " << std::fixed << std::setprecision(6) << normal_results.frame_period_stats.max_ms << "\n";
    file << "    },\n";
    file << "    \"frame_periods_ms\": [";
    for (size_t i = 0; i < normal_results.frame_periods_ms.size(); ++i) {
      if (i > 0) file << ", ";
      file << std::fixed << std::setprecision(6) << normal_results.frame_periods_ms[i];
    }
    file << "]";
  }
  
  if (!normal_results.execution_times_ms.empty()) {
    file << ",\n    \"execution_time_stats\": {\n";
    file << "      \"mean_ms\": " << std::fixed << std::setprecision(6) << normal_results.execution_time_stats.mean_ms << ",\n";
    file << "      \"std_ms\": " << std::fixed << std::setprecision(6) << normal_results.execution_time_stats.std_ms << ",\n";
    file << "      \"min_ms\": " << std::fixed << std::setprecision(6) << normal_results.execution_time_stats.min_ms << ",\n";
    file << "      \"max_ms\": " << std::fixed << std::setprecision(6) << normal_results.execution_time_stats.max_ms << "\n";
    file << "    },\n";
    file << "    \"execution_times_ms\": [";
    for (size_t i = 0; i < normal_results.execution_times_ms.size(); ++i) {
      if (i > 0) file << ", ";
      file << std::fixed << std::setprecision(6) << normal_results.execution_times_ms[i];
    }
    file << "]";
  }
  
  file << "\n  },\n";
  
  // Realtime results
  file << "  \"realtime\": {\n";
  file << "    \"target_fps\": " << realtime_results.target_fps << ",\n";
  file << "    \"frame_count\": " << realtime_results.frame_count << ",\n";
  file << "    \"total_duration_s\": " << std::fixed << std::setprecision(6) << realtime_results.total_duration_s << ",\n";
  file << "    \"actual_duration_s\": " << std::fixed << std::setprecision(6) << realtime_results.actual_duration_s << ",\n";
  file << "    \"use_realtime\": " << (realtime_results.use_realtime ? "true" : "false") << ",\n";
  file << "    \"scheduling_policy\": \"" << realtime_results.scheduling_policy << "\",\n";
  file << "    \"load_duration_ms\": " << std::fixed << std::setprecision(1) << realtime_results.load_duration_ms;
  
  if (!realtime_results.frame_periods_ms.empty()) {
    file << ",\n    \"frame_period_stats\": {\n";
    file << "      \"mean_ms\": " << std::fixed << std::setprecision(6) << realtime_results.frame_period_stats.mean_ms << ",\n";
    file << "      \"std_ms\": " << std::fixed << std::setprecision(6) << realtime_results.frame_period_stats.std_ms << ",\n";
    file << "      \"min_ms\": " << std::fixed << std::setprecision(6) << realtime_results.frame_period_stats.min_ms << ",\n";
    file << "      \"max_ms\": " << std::fixed << std::setprecision(6) << realtime_results.frame_period_stats.max_ms << "\n";
    file << "    },\n";
    file << "    \"frame_periods_ms\": [";
    for (size_t i = 0; i < realtime_results.frame_periods_ms.size(); ++i) {
      if (i > 0) file << ", ";
      file << std::fixed << std::setprecision(6) << realtime_results.frame_periods_ms[i];
    }
    file << "]";
  }
  
  if (!realtime_results.execution_times_ms.empty()) {
    file << ",\n    \"execution_time_stats\": {\n";
    file << "      \"mean_ms\": " << std::fixed << std::setprecision(6) << realtime_results.execution_time_stats.mean_ms << ",\n";
    file << "      \"std_ms\": " << std::fixed << std::setprecision(6) << realtime_results.execution_time_stats.std_ms << ",\n";
    file << "      \"min_ms\": " << std::fixed << std::setprecision(6) << realtime_results.execution_time_stats.min_ms << ",\n";
    file << "      \"max_ms\": " << std::fixed << std::setprecision(6) << realtime_results.execution_time_stats.max_ms << "\n";
    file << "    },\n";
    file << "    \"execution_times_ms\": [";
    for (size_t i = 0; i < realtime_results.execution_times_ms.size(); ++i) {
      if (i > 0) file << ", ";
      file << std::fixed << std::setprecision(6) << realtime_results.execution_times_ms[i];
    }
    file << "]";
  }
  
  file << "\n  }\n";
  file << "}\n";
  
  std::cout << "\nResults saved to: " << filename << std::endl;
}

int main(int argc, char* argv[]) {
  // Default parameters
  int target_fps = 60;
  int duration_seconds = 30;
  std::string scheduling_policy_str = "SCHED_DEADLINE";
  double load_duration_ms = 20.0;
  std::string output_file = "benchmark_results.json";

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--target-fps" && i + 1 < argc) {
      target_fps = std::stoi(argv[++i]);
    } else if (arg == "--duration" && i + 1 < argc) {
      duration_seconds = std::stoi(argv[++i]);
    } else if (arg == "--scheduling-policy" && i + 1 < argc) {
      scheduling_policy_str = argv[++i];
    } else if (arg == "--load-duration-ms" && i + 1 < argc) {
      load_duration_ms = std::stod(argv[++i]);
    } else if (arg == "--output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
      std::cout << "Options:" << std::endl;
      std::cout << "  --target-fps <fps>           Target FPS (30 or 60, default: 60)" << std::endl;
      std::cout << "  --duration <seconds>        Benchmark duration in seconds (default: 30)" << std::endl;
      std::cout << "  --scheduling-policy <policy> SCHED_DEADLINE, SCHED_FIFO, or SCHED_RR (default: SCHED_DEADLINE)" << std::endl;
      std::cout << "  --load-duration-ms <ms>     CPU work duration per load operator call (default: 20.0)" << std::endl;
      std::cout << "  --output <file>             Output JSON file (default: benchmark_results.json)" << std::endl;
      std::cout << "  --help                      Show this help message" << std::endl;
      return 0;
    }
  }

  // Map scheduling policy string to enum
  SchedulingPolicy scheduling_policy;
  if (scheduling_policy_str == "SCHED_DEADLINE") {
    scheduling_policy = SchedulingPolicy::kDeadline;
  } else if (scheduling_policy_str == "SCHED_FIFO") {
    scheduling_policy = SchedulingPolicy::kFirstInFirstOut;
  } else if (scheduling_policy_str == "SCHED_RR") {
    scheduling_policy = SchedulingPolicy::kRoundRobin;
  } else {
    std::cerr << "Invalid scheduling policy: " << scheduling_policy_str << std::endl;
    return 1;
  }

  std::cout << "Running real-time thread scheduling comparison..." << std::endl;

  // Run without real-time scheduling
  auto normal_results = run_benchmark(target_fps, duration_seconds, false, scheduling_policy, load_duration_ms);

  // Run with real-time scheduling
  auto realtime_results = run_benchmark(target_fps, duration_seconds, true, scheduling_policy, load_duration_ms);

  // Save results to JSON file
  save_results_to_json(normal_results, realtime_results, output_file);

  print_results(normal_results);
  print_results(realtime_results);

  std::cout << "\n" << std::string(65, '=') << std::endl;
  std::cout << "COMPARISON SUMMARY" << std::endl;
  std::cout << std::string(65, '=') << std::endl;
  std::cout << "                        Normal    Real-time    Improvement" << std::endl;
  std::cout << std::string(65, '-') << std::endl;

  if (normal_results.frame_period_stats.std_ms > 0 && realtime_results.frame_period_stats.std_ms > 0) {
    double normal_period_std = normal_results.frame_period_stats.std_ms;
    double realtime_period_std = realtime_results.frame_period_stats.std_ms;
    double period_std_improvement = (realtime_period_std / normal_period_std - 1) * 100;
    std::cout << "★ Frame Period Std Dev: " << std::fixed << std::setprecision(3) << normal_period_std
              << "    " << std::fixed << std::setprecision(3) << realtime_period_std
              << "    " << std::fixed << std::setprecision(1) << period_std_improvement << "% ★" << std::endl;
  }

  // Secondary metrics
  if (normal_results.execution_time_stats.std_ms > 0 && realtime_results.execution_time_stats.std_ms > 0) {
    double normal_exec_std = normal_results.execution_time_stats.std_ms;
    double realtime_exec_std = realtime_results.execution_time_stats.std_ms;
    double exec_std_improvement = (realtime_exec_std / normal_exec_std - 1) * 100;
    std::cout << "  Exec Time Std Dev:     " << std::fixed << std::setprecision(3) << normal_exec_std
              << "    " << std::fixed << std::setprecision(3) << realtime_exec_std
              << "    " << std::fixed << std::setprecision(1) << exec_std_improvement << "%" << std::endl;
  }

  return 0;
}
