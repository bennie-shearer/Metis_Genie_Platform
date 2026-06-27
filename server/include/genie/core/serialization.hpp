/**
 * @file serialization.hpp
 * @brief Serialization and state management for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_SERIALIZATION_HPP
#define GENIE_SERIALIZATION_HPP

#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <functional>

namespace genie {
namespace serialization {

// JSON-like serialization builder
class Serializer {
    std::ostringstream ss_;
    int indent_{0};
    std::vector<bool> first_stack_;
    
    void write_indent() { for (int i = 0; i < indent_; ++i) ss_ << "  "; }
    void maybe_comma() { if (!first_stack_.empty() && !first_stack_.back()) ss_ << ","; first_stack_.back() = false; }
    
public:
    Serializer& begin_object(const std::string& name = "") {
        if (!name.empty()) { maybe_comma(); ss_ << "\n"; write_indent(); ss_ << "\"" << name << "\": "; }
        ss_ << "{"; ++indent_; first_stack_.push_back(true);
        return *this;
    }
    
    Serializer& end_object() {
        --indent_; first_stack_.pop_back();
        ss_ << "\n"; write_indent(); ss_ << "}";
        return *this;
    }
    
    Serializer& begin_array(const std::string& name) {
        maybe_comma(); ss_ << "\n"; write_indent();
        ss_ << "\"" << name << "\": ["; ++indent_; first_stack_.push_back(true);
        return *this;
    }
    
    Serializer& end_array() {
        --indent_; first_stack_.pop_back();
        ss_ << "\n"; write_indent(); ss_ << "]";
        return *this;
    }
    
    Serializer& field(const std::string& name, const std::string& value) {
        maybe_comma(); ss_ << "\n"; write_indent();
        ss_ << "\"" << name << "\": \"" << value << "\"";
        return *this;
    }
    
    Serializer& field(const std::string& name, int value) {
        maybe_comma(); ss_ << "\n"; write_indent();
        ss_ << "\"" << name << "\": " << value;
        return *this;
    }
    
    Serializer& field(const std::string& name, double value) {
        maybe_comma(); ss_ << "\n"; write_indent();
        ss_ << "\"" << name << "\": " << std::fixed << std::setprecision(6) << value;
        return *this;
    }
    
    Serializer& field(const std::string& name, bool value) {
        maybe_comma(); ss_ << "\n"; write_indent();
        ss_ << "\"" << name << "\": " << (value ? "true" : "false");
        return *this;
    }
    
    std::string str() const { return ss_.str(); }
    
    void save(const std::string& filename) const {
        std::ofstream f(filename);
        f << ss_.str();
    }
};

// System State Snapshot
struct SystemSnapshot {
    std::string timestamp;
    std::string version;
    std::map<std::string, std::string> config;
    std::vector<std::map<std::string, std::string>> portfolios;
    std::vector<std::map<std::string, std::string>> securities;
    std::vector<std::map<std::string, std::string>> orders;
    
    std::string to_json() const {
        Serializer s;
        s.begin_object()
            .field("timestamp", timestamp)
            .field("version", version)
            .begin_object("config");
        for (const auto& [k, v] : config) s.field(k, v);
        s.end_object()
            .begin_array("portfolios");
        for (const auto& p : portfolios) {
            s.begin_object();
            for (const auto& [k, v] : p) s.field(k, v);
            s.end_object();
        }
        s.end_array()
            .begin_array("securities");
        for (const auto& sec : securities) {
            s.begin_object();
            for (const auto& [k, v] : sec) s.field(k, v);
            s.end_object();
        }
        s.end_array()
            .begin_array("orders");
        for (const auto& o : orders) {
            s.begin_object();
            for (const auto& [k, v] : o) s.field(k, v);
            s.end_object();
        }
        s.end_array()
            .end_object();
        return s.str();
    }
    
    void save(const std::string& filename) const {
        std::ofstream f(filename);
        f << to_json();
    }
    
    static std::string current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%dT%H:%M:%S");
        return ss.str();
    }
};

// State Manager
class StateManager {
    std::string snapshot_dir_{"./snapshots"};
    std::vector<SystemSnapshot> history_;
    
public:
    void set_snapshot_directory(const std::string& dir) { snapshot_dir_ = dir; }
    
    SystemSnapshot create_snapshot(const std::string& version) {
        SystemSnapshot snap;
        snap.timestamp = SystemSnapshot::current_timestamp();
        snap.version = version;
        history_.push_back(snap);
        return snap;
    }
    
    void save_snapshot(const SystemSnapshot& snap, const std::string& name = "") {
        std::string filename = snapshot_dir_ + "/" + 
            (name.empty() ? "snapshot_" + snap.timestamp : name) + ".json";
        // Replace colons in filename for Windows compatibility
        for (char& c : filename) if (c == ':') c = '-';
        snap.save(filename);
    }
    
    const std::vector<SystemSnapshot>& history() const { return history_; }
    
    void clear_history() { history_.clear(); }
};

// Checkpoint system for recovery
class CheckpointManager {
    std::string checkpoint_file_{"checkpoint.json"};
    std::function<SystemSnapshot()> snapshot_creator_;
    int auto_save_interval_{0};  // 0 = disabled
    int operations_since_save_{0};
    
public:
    void set_checkpoint_file(const std::string& file) { checkpoint_file_ = file; }
    void set_auto_save_interval(int ops) { auto_save_interval_ = ops; }
    void set_snapshot_creator(std::function<SystemSnapshot()> creator) { snapshot_creator_ = creator; }
    
    void record_operation() {
        ++operations_since_save_;
        if (auto_save_interval_ > 0 && operations_since_save_ >= auto_save_interval_) {
            save_checkpoint();
            operations_since_save_ = 0;
        }
    }
    
    void save_checkpoint() {
        if (snapshot_creator_) {
            auto snap = snapshot_creator_();
            snap.save(checkpoint_file_);
        }
    }
    
    bool checkpoint_exists() const {
        std::ifstream f(checkpoint_file_);
        return f.good();
    }
};

} // namespace serialization
} // namespace genie
#endif // GENIE_SERIALIZATION_HPP
