/**
 * @file feature_flags.hpp
 * @brief Feature flag system for gradual rollout and A/B testing
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides a runtime feature flag system enabling gradual feature rollouts,
 * A/B testing, and tenant-specific feature gates without redeployment.
 * Supports boolean, percentage, and user-segment targeting.
 *
 * Zero external dependencies. C++20. Cross-platform (Windows/Linux/macOS).
 */
#pragma once
#ifndef GENIE_CORE_FEATURE_FLAGS_HPP
#define GENIE_CORE_FEATURE_FLAGS_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <numeric>
#include <atomic>

namespace genie::core {

enum class FlagType { BOOLEAN, PERCENTAGE, SEGMENT, SCHEDULED };
enum class FlagStatus { ACTIVE, INACTIVE, DEPRECATED };

struct FeatureFlag {
    std::string name;
    std::string description;
    FlagType type = FlagType::BOOLEAN;
    FlagStatus status = FlagStatus::ACTIVE;
    bool default_value = false;
    double rollout_percentage = 0.0;
    std::vector<std::string> enabled_segments;
    std::vector<std::string> enabled_tenants;
    std::chrono::system_clock::time_point schedule_start{};
    std::chrono::system_clock::time_point schedule_end{};
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point updated_at{};
    std::string owner;
    std::string jira_ticket;
    uint64_t evaluation_count = 0;
    uint64_t enabled_count = 0;
};

struct FlagEvaluationContext {
    std::string user_id;
    std::string tenant_id;
    std::string segment;
    std::unordered_map<std::string, std::string> attributes;
};

class FeatureFlagManager {
public:
    void register_flag(const std::string& name, const FeatureFlag& flag) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto f = flag;
        f.name = name;
        f.created_at = std::chrono::system_clock::now();
        f.updated_at = f.created_at;
        flags_[name] = std::move(f);
    }

    bool is_enabled(const std::string& name, const FlagEvaluationContext& ctx = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(name);
        if (it == flags_.end()) return false;
        auto& flag = it->second;
        flag.evaluation_count++;
        if (flag.status != FlagStatus::ACTIVE) return false;

        bool result = false;
        switch (flag.type) {
            case FlagType::BOOLEAN:
                result = flag.default_value;
                break;
            case FlagType::PERCENTAGE:
                result = evaluate_percentage(flag, ctx);
                break;
            case FlagType::SEGMENT:
                result = evaluate_segment(flag, ctx);
                break;
            case FlagType::SCHEDULED:
                result = evaluate_schedule(flag);
                break;
        }
        if (!ctx.tenant_id.empty() && !flag.enabled_tenants.empty()) {
            auto found = std::find(flag.enabled_tenants.begin(),
                                   flag.enabled_tenants.end(), ctx.tenant_id);
            if (found == flag.enabled_tenants.end()) result = false;
        }
        if (result) flag.enabled_count++;
        return result;
    }

    void set_enabled(const std::string& name, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(name);
        if (it != flags_.end()) {
            it->second.default_value = enabled;
            it->second.updated_at = std::chrono::system_clock::now();
        }
    }

    void set_rollout(const std::string& name, double pct) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(name);
        if (it != flags_.end()) {
            it->second.rollout_percentage = std::clamp(pct, 0.0, 100.0);
            it->second.type = FlagType::PERCENTAGE;
            it->second.updated_at = std::chrono::system_clock::now();
        }
    }

    void deprecate(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(name);
        if (it != flags_.end()) {
            it->second.status = FlagStatus::DEPRECATED;
            it->second.updated_at = std::chrono::system_clock::now();
        }
    }

    std::optional<FeatureFlag> get_flag(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(name);
        if (it != flags_.end()) return it->second;
        return std::nullopt;
    }

    std::vector<FeatureFlag> list_flags() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FeatureFlag> result;
        result.reserve(flags_.size());
        for (auto& [k, v] : flags_) result.push_back(v);
        return result;
    }

    size_t flag_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return flags_.size();
    }

    struct FlagStats {
        size_t total = 0;
        size_t active = 0;
        size_t inactive = 0;
        size_t deprecated = 0;
        uint64_t total_evaluations = 0;
    };

    FlagStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        FlagStats s;
        s.total = flags_.size();
        for (auto& [k, v] : flags_) {
            switch (v.status) {
                case FlagStatus::ACTIVE: s.active++; break;
                case FlagStatus::INACTIVE: s.inactive++; break;
                case FlagStatus::DEPRECATED: s.deprecated++; break;
            }
            s.total_evaluations += v.evaluation_count;
        }
        return s;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FeatureFlag> flags_;

    bool evaluate_percentage(const FeatureFlag& flag, const FlagEvaluationContext& ctx) {
        if (ctx.user_id.empty()) return flag.rollout_percentage >= 50.0;
        uint32_t hash = 0;
        for (char c : ctx.user_id) hash = hash * 31 + static_cast<uint32_t>(c);
        for (char c : flag.name) hash = hash * 31 + static_cast<uint32_t>(c);
        double bucket = static_cast<double>(hash % 10000) / 100.0;
        return bucket < flag.rollout_percentage;
    }

    bool evaluate_segment(const FeatureFlag& flag, const FlagEvaluationContext& ctx) {
        if (ctx.segment.empty()) return false;
        return std::find(flag.enabled_segments.begin(),
                         flag.enabled_segments.end(), ctx.segment) != flag.enabled_segments.end();
    }

    bool evaluate_schedule(const FeatureFlag& flag) {
        auto now = std::chrono::system_clock::now();
        bool after_start = (flag.schedule_start == std::chrono::system_clock::time_point{}) || now >= flag.schedule_start;
        bool before_end = (flag.schedule_end == std::chrono::system_clock::time_point{}) || now <= flag.schedule_end;
        return after_start && before_end;
    }
};

} // namespace genie::core

#endif // GENIE_CORE_FEATURE_FLAGS_HPP
