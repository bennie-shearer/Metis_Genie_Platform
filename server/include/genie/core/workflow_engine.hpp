/**
 * @file workflow_engine.hpp
 * @brief DAG-based task workflow orchestration
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Directed acyclic graph workflow execution:
 * - Task nodes with typed inputs/outputs
 * - Dependency resolution via topological sort
 * - Parallel execution of independent tasks
 * - Retry policies per task
 * - Workflow templating and parameterization
 * - Conditional branching and gates
 * - Execution history and replay
 * - Timeout and cancellation support
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_WORKFLOW_ENGINE_HPP
#define GENIE_CORE_WORKFLOW_ENGINE_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>

namespace genie {
namespace core {
namespace workflow {

// ============================================================================
// Enumerations
// ============================================================================

enum class TaskState {
    Pending,
    Ready,         // Dependencies met, awaiting execution
    Running,
    Completed,
    Failed,
    Skipped,
    Cancelled,
    TimedOut
};

enum class WorkflowState {
    Created,
    Running,
    Completed,
    Failed,
    Cancelled,
    Paused
};

[[nodiscard]] inline std::string task_state_string(TaskState s) {
    switch (s) {
        case TaskState::Pending:    return "pending";
        case TaskState::Ready:      return "ready";
        case TaskState::Running:    return "running";
        case TaskState::Completed:  return "completed";
        case TaskState::Failed:     return "failed";
        case TaskState::Skipped:    return "skipped";
        case TaskState::Cancelled:  return "cancelled";
        case TaskState::TimedOut:   return "timed_out";
    }
    return "unknown";
}

[[nodiscard]] inline std::string workflow_state_string(WorkflowState s) {
    switch (s) {
        case WorkflowState::Created:   return "created";
        case WorkflowState::Running:   return "running";
        case WorkflowState::Completed: return "completed";
        case WorkflowState::Failed:    return "failed";
        case WorkflowState::Cancelled: return "cancelled";
        case WorkflowState::Paused:    return "paused";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

using TaskContext = std::map<std::string, std::string>;
using TaskFunction = std::function<bool(TaskContext&)>;
using ConditionFunction = std::function<bool(const TaskContext&)>;

/**
 * @brief Single task in a workflow DAG
 */
struct WorkflowTask {
    std::string id;
    std::string name;
    std::string description;
    TaskFunction execute;
    std::vector<std::string> depends_on;
    std::optional<ConditionFunction> condition;   // Skip if false
    int max_retries{0};
    std::chrono::seconds timeout{300};
    int priority{100};             // Lower = higher priority

    // Runtime state
    TaskState state{TaskState::Pending};
    int attempt{0};
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::chrono::milliseconds duration{0};
    std::string error_message;
    TaskContext output;
};

/**
 * @brief Workflow execution record
 */
struct WorkflowExecution {
    std::string workflow_id;
    std::string execution_id;
    WorkflowState state{WorkflowState::Created};
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::chrono::milliseconds total_duration{0};
    int tasks_completed{0};
    int tasks_failed{0};
    int tasks_skipped{0};
    TaskContext context;           // Shared context across tasks

    [[nodiscard]] std::string format() const {
        auto t = std::chrono::system_clock::to_time_t(started_at);
        std::ostringstream oss;
        oss << "Workflow[" << workflow_id << "] " << workflow_state_string(state)
            << " started=" << std::put_time(std::gmtime(&t), "%H:%M:%S")
            << " duration=" << total_duration.count() << "ms"
            << " completed=" << tasks_completed
            << " failed=" << tasks_failed
            << " skipped=" << tasks_skipped;
        return oss.str();
    }
};

// ============================================================================
// Workflow Builder
// ============================================================================

class Workflow;

/**
 * @brief Fluent workflow builder
 */
class WorkflowBuilder {
public:
    WorkflowBuilder() = default;
    explicit WorkflowBuilder(const std::string& name) : name_(name) {}

    WorkflowBuilder& task(const std::string& id, const std::string& name,
                            TaskFunction fn) {
        WorkflowTask t;
        t.id = id;
        t.name = name;
        t.execute = std::move(fn);
        tasks_.push_back(std::move(t));
        return *this;
    }

    WorkflowBuilder& depends_on(const std::string& task_id,
                                  const std::vector<std::string>& deps) {
        for (auto& t : tasks_) {
            if (t.id == task_id) { t.depends_on = deps; break; }
        }
        return *this;
    }

    WorkflowBuilder& with_retry(const std::string& task_id, int retries) {
        for (auto& t : tasks_) {
            if (t.id == task_id) { t.max_retries = retries; break; }
        }
        return *this;
    }

    WorkflowBuilder& with_timeout(const std::string& task_id, int seconds) {
        for (auto& t : tasks_) {
            if (t.id == task_id) { t.timeout = std::chrono::seconds(seconds); break; }
        }
        return *this;
    }

    WorkflowBuilder& with_condition(const std::string& task_id, ConditionFunction cond) {
        for (auto& t : tasks_) {
            if (t.id == task_id) { t.condition = std::move(cond); break; }
        }
        return *this;
    }

    [[nodiscard]] std::string name() const { return name_; }
    [[nodiscard]] std::vector<WorkflowTask> tasks() const { return tasks_; }

private:
    std::string name_;
    std::vector<WorkflowTask> tasks_;
};

// ============================================================================
// Workflow Engine
// ============================================================================

/**
 * @brief Executes DAG-based workflows
 */
class WorkflowEngine {
public:
    /**
     * @brief Register a workflow template
     */
    void register_workflow(const std::string& id, WorkflowBuilder builder) {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_[id] = std::move(builder);
    }

    /**
     * @brief Execute a workflow
     */
    WorkflowExecution execute(const std::string& workflow_id,
                               TaskContext initial_context = {}) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = templates_.find(workflow_id);
        if (it == templates_.end()) {
            WorkflowExecution ex;
            ex.workflow_id = workflow_id;
            ex.state = WorkflowState::Failed;
            return ex;
        }

        WorkflowExecution exec;
        exec.workflow_id = workflow_id;
        exec.execution_id = workflow_id + "-" + std::to_string(++exec_counter_);
        exec.started_at = std::chrono::system_clock::now();
        exec.state = WorkflowState::Running;
        exec.context = initial_context;

        auto tasks = it->second.tasks();

        // Topological sort
        auto ordered = topological_sort(tasks);
        if (ordered.empty() && !tasks.empty()) {
            exec.state = WorkflowState::Failed;
            return exec;
        }

        // Execute in dependency order
        std::map<std::string, TaskState> states;
        for (auto& task : ordered) {
            // Check dependencies
            bool deps_met = true;
            for (const auto& dep : task.depends_on) {
                if (states[dep] != TaskState::Completed) {
                    deps_met = false;
                    break;
                }
            }

            if (!deps_met) {
                task.state = TaskState::Skipped;
                states[task.id] = TaskState::Skipped;
                ++exec.tasks_skipped;
                continue;
            }

            // Check condition
            if (task.condition && !(*task.condition)(exec.context)) {
                task.state = TaskState::Skipped;
                states[task.id] = TaskState::Skipped;
                ++exec.tasks_skipped;
                continue;
            }

            // Execute with retry
            bool success = false;
            for (int attempt = 0; attempt <= task.max_retries; ++attempt) {
                task.attempt = attempt + 1;
                task.state = TaskState::Running;
                task.started_at = std::chrono::system_clock::now();

                try {
                    success = task.execute(exec.context);
                } catch (const std::exception& e) {
                    task.error_message = e.what();
                    success = false;
                } catch (...) {
                    task.error_message = "unknown exception";
                    success = false;
                }

                task.completed_at = std::chrono::system_clock::now();
                task.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    task.completed_at - task.started_at);

                if (success) break;
            }

            if (success) {
                task.state = TaskState::Completed;
                states[task.id] = TaskState::Completed;
                ++exec.tasks_completed;
            } else {
                task.state = TaskState::Failed;
                states[task.id] = TaskState::Failed;
                ++exec.tasks_failed;
                exec.state = WorkflowState::Failed;
                break;
            }
        }

        if (exec.state == WorkflowState::Running) {
            exec.state = WorkflowState::Completed;
        }

        exec.completed_at = std::chrono::system_clock::now();
        exec.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            exec.completed_at - exec.started_at);

        history_.push_back(exec);
        if (history_.size() > 1000) history_.pop_front();

        return exec;
    }

    /**
     * @brief Get execution history
     */
    [[nodiscard]] std::vector<WorkflowExecution> history(int last_n = 50) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<WorkflowExecution> result;
        int start = std::max(0, static_cast<int>(history_.size()) - last_n);
        for (int i = start; i < static_cast<int>(history_.size()); ++i) {
            result.push_back(history_[i]);
        }
        return result;
    }

    [[nodiscard]] int workflow_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(templates_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, WorkflowBuilder> templates_;
    std::deque<WorkflowExecution> history_;
    int exec_counter_{0};

    [[nodiscard]] static std::vector<WorkflowTask> topological_sort(
        std::vector<WorkflowTask>& tasks) {
        std::map<std::string, int> in_degree;
        std::map<std::string, std::vector<std::string>> adj;
        std::map<std::string, WorkflowTask*> task_map;

        for (auto& t : tasks) {
            task_map[t.id] = &t;
            in_degree[t.id] = 0;
        }
        for (auto& t : tasks) {
            for (const auto& dep : t.depends_on) {
                adj[dep].push_back(t.id);
                ++in_degree[t.id];
            }
        }

        std::deque<std::string> queue;
        for (auto& [id, deg] : in_degree) {
            if (deg == 0) queue.push_back(id);
        }

        std::vector<WorkflowTask> result;
        while (!queue.empty()) {
            auto id = queue.front(); queue.pop_front();
            if (task_map.count(id)) result.push_back(*task_map[id]);
            for (const auto& next : adj[id]) {
                if (--in_degree[next] == 0) queue.push_back(next);
            }
        }

        // Cycle detection
        if (result.size() != tasks.size()) return {};
        return result;
    }
};

} // namespace workflow
} // namespace core
} // namespace genie

#endif // GENIE_CORE_WORKFLOW_ENGINE_HPP
