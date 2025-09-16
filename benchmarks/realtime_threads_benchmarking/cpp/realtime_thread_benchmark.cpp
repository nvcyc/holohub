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

using namespace holoscan;

struct BenchmarkStats {
  double avg = 0.0;
  double std_dev = 0.0;
  double min_val = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double max_val = 0.0;
  size_t sample_count = 0;
  std::vector<double> sorted_data;
};

// Calculate percentiles from sorted data
double calculate_percentile(const std::vector<double>& sorted_data, double percentile) {
  if (sorted_data.empty())
    return 0.0;

  double index = (percentile / 100.0) * (sorted_data.size() - 1);
  size_t lower = static_cast<size_t>(std::floor(index));
  size_t upper = static_cast<size_t>(std::ceil(index));

  if (lower == upper) {
    return sorted_data[lower];
  }

  double weight = index - lower;
  return sorted_data[lower] * (1.0 - weight) + sorted_data[upper] * weight;
}

// Calculate standard deviation
double calculate_std_dev(const std::vector<double>& data, double mean) {
  if (data.size() <= 1)
    return 0.0;

  double sum_sq_diff = 0.0;
  for (double value : data) {
    double diff = value - mean;
    sum_sq_diff += diff * diff;
  }

  return std::sqrt(sum_sq_diff / (data.size() - 1));
}

// Calculate benchmark statistics
BenchmarkStats calculate_benchmark_stats(
  const std::vector<double>& raw_values, bool skip_negative_values = false) {
  BenchmarkStats stats;

  // Extract values
  for (const auto& value : raw_values) {
    if (value >= 0.0 || !skip_negative_values) {
      stats.sorted_data.push_back(value);
    }
  }

  if (stats.sorted_data.empty())
    return stats;

  std::sort(stats.sorted_data.begin(), stats.sorted_data.end());
  stats.sample_count = stats.sorted_data.size();

  // Calculate basic statistics
  stats.avg =
      std::accumulate(stats.sorted_data.begin(), stats.sorted_data.end(), 0.0) / stats.sample_count;
  stats.std_dev = calculate_std_dev(stats.sorted_data, stats.avg);

  // Calculate percentiles
  stats.min_val = stats.sorted_data.front();
  stats.max_val = stats.sorted_data.back();
  stats.p50 = calculate_percentile(stats.sorted_data, 50.0);
  stats.p95 = calculate_percentile(stats.sorted_data, 95.0);
  stats.p99 = calculate_percentile(stats.sorted_data, 99.0);

  return stats;
}

// Calculate empirical CDF at a given value
double calculate_cdf(const std::vector<double>& data, double x) {
  if (data.empty())
    return 0.0;

  int count = 0;
  for (double value : data) {
    if (value <= x)
      count++;
  }

  return static_cast<double>(count) / data.size();
}

void run_dummy_cpu_workload(int workload_size = 100, int load_intensity = 100) {
  std::vector<double> data(workload_size);
  double work_result = 0.0;
  for (int i = 0; i < load_intensity; ++i) {
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = std::sin(i * 0.01) * std::cos(i * 0.02);
    }
    for (double x : data) {
      work_result += std::sqrt(std::abs(x) + 1.0);
    }
  }
}

class DummyLoadOp : public Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(DummyLoadOp)

  DummyLoadOp(int workload_size = 1000)
  : workload_size_(workload_size) {}

  void compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) override {
    run_dummy_cpu_workload(workload_size_);
  };

 private:
   int workload_size_;
 };

class BenchmarkOp : public Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(BenchmarkOp)

  BenchmarkOp(int target_fps = 60,
              int load_intensity = 100,
              int workload_size = 100)
            : target_fps_(target_fps),
              target_period_ns_(static_cast<int64_t>(1e9 / target_fps)),
              load_intensity_(load_intensity),
              workload_size_(workload_size) {}

  void compute(InputContext& op_input, OutputContext& op_output, ExecutionContext& context) override {
    auto start_time = std::chrono::steady_clock::now();

    if (last_start_time_ == std::chrono::steady_clock::time_point()) {
      last_start_time_ = start_time;
    } else {
      auto period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        start_time - last_start_time_).count();
      periods_ns_.push_back(period_ns);
      last_start_time_ = start_time;
    }

    run_dummy_cpu_workload(workload_size_, load_intensity_);
    auto end_time = std::chrono::steady_clock::now();

    auto execution_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - start_time).count();
    execution_times_ns_.push_back(execution_time_ns);
  };

  BenchmarkStats get_execution_time_benchmark_stats() const {
    BenchmarkStats benchmark_stats;

    if (execution_times_ns_.empty()) {
      return benchmark_stats;
    }

    // Convert nanoseconds to milliseconds for statistics
    std::vector<double> execution_times_ms;
    for (double ns : execution_times_ns_) {
      execution_times_ms.push_back(ns / 1e6);
    }

    benchmark_stats = calculate_benchmark_stats(execution_times_ms, false);

    return benchmark_stats;
  }

  BenchmarkStats get_period_benchmark_stats() const {
    BenchmarkStats benchmark_stats;
    if (periods_ns_.empty()) {
      return benchmark_stats;
    }
    
    // Convert nanoseconds to milliseconds for statistics
    std::vector<double> periods_ms;
    for (double ns : periods_ns_) {
      periods_ms.push_back(ns / 1e6);
    }

    benchmark_stats = calculate_benchmark_stats(periods_ms, false);
    return benchmark_stats;
  }

private:
  int target_fps_;
  int64_t target_period_ns_;
  int load_intensity_;
  int workload_size_;
  std::vector<double> periods_ns_;
  std::vector<double> execution_times_ns_;
  std::chrono::steady_clock::time_point last_start_time_{};
  mutable std::mutex lock_;
  double work_result_;
};

class RealtimeThreadBenchmarkApp : public Application {
 public:
  RealtimeThreadBenchmarkApp(int target_fps = 60,
                         bool use_realtime = false,
                         SchedulingPolicy scheduling_policy = SchedulingPolicy::kDeadline,
                         int background_load_intensity = 1000,
                         int background_workload_size = 1000)
      : target_fps_(target_fps),
        use_realtime_(use_realtime),
        scheduling_policy_(scheduling_policy),
        background_load_intensity_(background_load_intensity),
        background_workload_size_(background_workload_size) {}

  void compose() override {
    benchmark_op_ = make_operator<BenchmarkOp>("benchmark_op", target_fps_);
    add_operator(benchmark_op_);

    auto periodic_condition = make_condition<PeriodicCondition>("periodic_condition", 1000000000 / target_fps_);

    if (use_realtime_) {
      // Create a thread pool for hosting the real-time thread
      auto realtime_pool = make_thread_pool("realtime_thread_pool");

      if (scheduling_policy_ == SchedulingPolicy::kDeadline) {
        int64_t period_ns = static_cast<int64_t>(1e9 / target_fps_);
        int64_t deadline_ns = period_ns;
        int64_t runtime_ns = static_cast<int64_t>(period_ns * 0.10);  // 10% of period

        realtime_pool->add_realtime(benchmark_op_, scheduling_policy_, true, {0}, 0,
                                   runtime_ns, deadline_ns, period_ns);
      } else if (scheduling_policy_ == SchedulingPolicy::kFirstInFirstOut ||
                 scheduling_policy_ == SchedulingPolicy::kRoundRobin) {
        realtime_pool->add_realtime(benchmark_op_, scheduling_policy_, true, {0}, 99);
        benchmark_op_->add_arg(periodic_condition);
      }
    } else {
      benchmark_op_->add_arg(periodic_condition);
    }

    auto dummy_load_op_1 = make_operator<DummyLoadOp>("dummy_load_op_1", background_workload_size_);
    auto dummy_load_op_2 = make_operator<DummyLoadOp>("dummy_load_op_2", background_workload_size_);
    add_operator(dummy_load_op_1);
    add_operator(dummy_load_op_2);

  }

  BenchmarkStats get_execution_time_benchmark_stats() {
    return benchmark_op_->get_execution_time_benchmark_stats();
  }

  BenchmarkStats get_period_benchmark_stats() {
    return benchmark_op_->get_period_benchmark_stats();
  }

 private:
  int target_fps_;
  bool use_realtime_;
  SchedulingPolicy scheduling_policy_;
  int background_load_intensity_;
  int background_workload_size_;
  std::shared_ptr<BenchmarkOp> benchmark_op_;
};

void print_title(const std::string& title) {
  std::cout << std::string(80, '=') << std::endl;
  std::cout << title << std::endl;
  std::cout << std::string(80, '=') << std::endl;
}

void print_benchmark_config(int target_fps, int duration_seconds, const std::string& scheduling_policy_str, 
                           int background_load_intensity, int background_workload_size, bool use_realtime, int worker_thread_number) {
  std::cout << "  Target FPS: " << target_fps << std::endl;
  std::cout << "  Duration: " << duration_seconds << "s" << std::endl;
  std::cout << "  Realtime: " << (use_realtime ? "true" : "false") << std::endl;
  if (use_realtime) {
    std::cout << "  Scheduling Policy: " << scheduling_policy_str << std::endl;
  }
  std::cout << "  Background Workload Intensity: " << background_load_intensity << std::endl;
  std::cout << "  Background Workload Size: " << background_workload_size << std::endl;
  std::cout << "  Worker Thread Number: " << worker_thread_number << std::endl;
}

void print_benchmark_results(
  const BenchmarkStats& period_stats,
  const BenchmarkStats& execution_time_stats,
  int target_fps,
  const std::string& context_type) {
  std::cout << "=== " << context_type << " ===" << std::endl;
  std::cout << std::fixed << std::setprecision(3) << std::dec;
  
  if (period_stats.sample_count > 0) {
    double target_period_ms = 1000.0 / target_fps;
    std::cout << "Period Statistics:" << std::endl;
    std::cout << "  Average: " << period_stats.avg << " ms (Target: " 
              << std::fixed << std::setprecision(3) << target_period_ms << " ms)" << std::endl;
    std::cout << "  Std Dev: " << period_stats.std_dev << " ms" << std::endl;
    std::cout << "  Min:     " << period_stats.min_val << " ms" << std::endl;
    std::cout << "  P50:     " << period_stats.p50 << " ms" << std::endl;
    std::cout << "  P95:     " << period_stats.p95 << " ms" << std::endl;
    std::cout << "  P99:     " << period_stats.p99 << " ms" << std::endl;
    std::cout << "  Max:     " << period_stats.max_val << " ms" << std::endl;
    std::cout << "  Samples: " << period_stats.sample_count << std::endl << std::endl;
  }
}

int main(int argc, char* argv[]) {
  // Default parameters
  int target_fps = 60;
  int duration_seconds = 10;
  std::string scheduling_policy_str = "SCHED_DEADLINE";
  int background_load_intensity = 1000;
  int background_workload_size = 100;
  int worker_thread_number = 2;

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--target-fps" && i + 1 < argc) {
      target_fps = std::atoi(argv[++i]);
    } else if (arg == "--duration" && i + 1 < argc) {
      duration_seconds = std::atoi(argv[++i]);
    } else if (arg == "--scheduling-policy" && i + 1 < argc) {
      scheduling_policy_str = argv[++i];
    } else if (arg == "--load-intensity" && i + 1 < argc) {
      background_load_intensity = std::atoi(argv[++i]);
      if (background_load_intensity <= 0) {
        std::cerr << "Error: load-intensity must be positive\n";
        return 1;
      }
    } else if (arg == "--workload-size" && i + 1 < argc) {
      background_workload_size = std::atoi(argv[++i]);
      if (background_workload_size <= 0) {
        std::cerr << "Error: workload-size must be positive\n";
        return 1;
      }
    } else if (arg == "--worker-thread-number" && i + 1 < argc) {
      worker_thread_number = std::atoi(argv[++i]);
      if (worker_thread_number <= 1) {
        std::cerr << "Error: worker-thread-number must be greater than 1\n";
        return 1;
      }
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
      std::cout << "Options:" << std::endl;
      std::cout << "  --target-fps <fps>           Target FPS (30 or 60, default: 60)" << std::endl;
      std::cout << "  --duration <seconds>        Benchmark duration in seconds (default: 30)" << std::endl;
      std::cout << "  --scheduling-policy <policy> SCHED_DEADLINE, SCHED_FIFO, or SCHED_RR (default: SCHED_DEADLINE)" << std::endl;
      std::cout << "  --load-intensity <intensity> Background workload intensity (default: 1000)" << std::endl;
      std::cout << "  --workload-size <size>     Background workload size (default: 1000)" << std::endl;
      std::cout << "  --worker-thread-number <number> Worker thread number (default: 2)" << std::endl;
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

  print_title("Real-time Thread Benchmark");
  print_benchmark_config(target_fps, duration_seconds, scheduling_policy_str, background_load_intensity, background_workload_size, false, worker_thread_number);

  // Run without real-time scheduling
  print_title("Running benchmark for baseline\n(without real-time thread)");
  auto non_rt_app = std::make_unique<RealtimeThreadBenchmarkApp>(
    target_fps, false, scheduling_policy, background_load_intensity, background_workload_size);
  non_rt_app->scheduler(non_rt_app->make_scheduler<EventBasedScheduler>(
    "event-based",
    Arg("worker_thread_number", static_cast<int64_t>(worker_thread_number)),
    Arg("max_duration_ms", static_cast<int64_t>(duration_seconds * 1000))));
  non_rt_app->run();
  auto non_rt_period_stats = non_rt_app->get_period_benchmark_stats();
  auto non_rt_execution_time_stats = non_rt_app->get_execution_time_benchmark_stats();
  // auto normal_results = run_benchmark(target_fps, duration_seconds, false, scheduling_policy, background_load_intensity, background_workload_size);

  // Run with real-time scheduling
  print_title("Running benchmark for real-time\n(with real-time scheduling)");
  auto rt_app = std::make_unique<RealtimeThreadBenchmarkApp>(
    target_fps, true, scheduling_policy, background_load_intensity, background_workload_size);
  rt_app->scheduler(rt_app->make_scheduler<EventBasedScheduler>(
    "event-based",
    Arg("worker_thread_number", static_cast<int64_t>(worker_thread_number)),
    Arg("max_duration_ms", static_cast<int64_t>(duration_seconds * 1000))));
  rt_app->run();
  auto rt_period_stats = rt_app->get_period_benchmark_stats();
  auto rt_execution_time_stats = rt_app->get_execution_time_benchmark_stats();
  // auto realtime_results = run_benchmark(target_fps, duration_seconds, true, scheduling_policy, background_load_intensity, background_workload_size);

  // Display benchmark configurations
  print_title("Benchmark Configurations");
  print_benchmark_config(target_fps, duration_seconds, scheduling_policy_str, background_load_intensity, background_workload_size, false, worker_thread_number);
  std::cout << std::endl;

  // Display benchmark results
  print_title("Benchmark Results");
  print_benchmark_results(non_rt_period_stats, non_rt_execution_time_stats, target_fps, "Non-real-time Thread (Baseline)");
  print_benchmark_results(rt_period_stats, rt_execution_time_stats, target_fps, "Real-time Thread");

  // Performance Comparison
  print_title("Non-real-time and Real-time Thread Benchmark Comparison");

  // Calculate improvements in period metrics
  double avg_improvement = ((non_rt_period_stats.avg - rt_period_stats.avg) / non_rt_period_stats.avg) * 100.0;
  double p95_improvement = ((non_rt_period_stats.p95 - rt_period_stats.p95) / non_rt_period_stats.p95) * 100.0;
  double p99_improvement = ((non_rt_period_stats.p99 - rt_period_stats.p99) / non_rt_period_stats.p99) * 100.0;

  std::cout << std::fixed << std::setprecision(2) << std::dec;

  std::cout << "Period:" << std::endl;
  std::cout << "  Average Period:  " << std::setw(8) << non_rt_period_stats.avg << " ms → "
            << std::setw(8) << rt_period_stats.avg << " ms  (" << std::showpos << avg_improvement << "%)"
            << std::endl;
  std::cout << "  95th Percentile:  " << std::setw(8) << non_rt_period_stats.p95 << " ms → "
            << std::setw(8) << rt_period_stats.p95 << " ms  (" << std::showpos << p95_improvement << "%)"
            << std::endl;
  std::cout << "  99th Percentile:  " << std::setw(8) << non_rt_period_stats.p99 << " ms → "
            << std::setw(8) << rt_period_stats.p99 << " ms  (" << std::showpos << p99_improvement << "%)"
            << std::endl << std::endl;

  std::cout << std::noshowpos;

  return 0;
}
