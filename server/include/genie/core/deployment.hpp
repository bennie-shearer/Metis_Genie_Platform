/**
 * @file deployment.hpp
 * @brief Deployment abstractions for future container/orchestration support
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * This header provides abstractions for deployment environments.
 * Currently supports standalone execution, but the API is designed
 * to support future containerization (Docker) and orchestration
 * (Kubernetes) without breaking changes.
 *
 * Future Implementation Path:
 * - v3.x: Docker containerization support
 * - v4.x: Kubernetes-native deployment
 * - v4.x: Service mesh integration
 *
 * Current Features:
 * - Environment variable configuration
 * - Health check endpoints
 * - Graceful shutdown handling
 * - Signal handling
 */

#pragma once
#ifndef GENIE_CORE_DEPLOYMENT_HPP
#define GENIE_CORE_DEPLOYMENT_HPP

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

#ifdef _WIN32
#include <windows.h>
// Undefine Windows macros that conflict with C++ identifiers
#ifdef DELETE
#undef DELETE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <unistd.h>
#endif

namespace genie::deployment {

// =========================================================================
// Environment Configuration
// =========================================================================

/**
 * @brief Get environment variable with default value
 * @param name Environment variable name
 * @param default_value Value if not set
 * @return Environment variable value or default
 * 
 * Container usage:
 *   METIS_PORT=8080 docker run metis-genie-platform
 *   int port = env("METIS_PORT", 8080);
 */
inline std::string env(const std::string& name, const std::string& default_value = "") {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : default_value;
}

/**
 * @brief Get environment variable as integer
 */
inline int env_int(const std::string& name, int default_value = 0) {
    const char* value = std::getenv(name.c_str());
    if (!value) return default_value;
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

/**
 * @brief Get environment variable as boolean
 */
inline bool env_bool(const std::string& name, bool default_value = false) {
    const char* value = std::getenv(name.c_str());
    if (!value) return default_value;
    std::string v = value;
    return v == "1" || v == "true" || v == "yes" || v == "TRUE" || v == "YES";
}

// =========================================================================
// Standard Environment Variables
// =========================================================================

/**
 * @brief Standard environment variable names for Metis Genie Platform
 * 
 * These follow container best practices:
 * - PORT: Standard port variable (Heroku, Cloud Run, etc.)
 * - METIS_*: Application-specific variables
 */
struct EnvVars {
    static constexpr const char* PORT = "PORT";                    // Server port
    static constexpr const char* METIS_HOST = "METIS_HOST";        // Bind address
    static constexpr const char* METIS_DB_PATH = "METIS_DB_PATH";  // Database path
    static constexpr const char* METIS_LOG_LEVEL = "METIS_LOG_LEVEL";  // Log level
    static constexpr const char* METIS_CONFIG = "METIS_CONFIG";    // Config file path
    static constexpr const char* METIS_MODE = "METIS_MODE";        // Run mode
    
    // Future Kubernetes-specific
    static constexpr const char* KUBERNETES_SERVICE_HOST = "KUBERNETES_SERVICE_HOST";
    static constexpr const char* KUBERNETES_PORT = "KUBERNETES_PORT";
};

/**
 * @brief Check if running in container environment
 */
inline bool is_container_environment() {
    // Check for common container indicators
    return std::getenv("KUBERNETES_SERVICE_HOST") != nullptr ||
           std::getenv("DOCKER_CONTAINER") != nullptr ||
           std::getenv("container") != nullptr;
}

/**
 * @brief Check if running in Kubernetes
 */
inline bool is_kubernetes_environment() {
    return std::getenv("KUBERNETES_SERVICE_HOST") != nullptr;
}

// =========================================================================
// Health Check Support
// =========================================================================

/**
 * @brief Health check status
 */
enum class HealthStatus {
    HEALTHY,      ///< Service is healthy
    DEGRADED,     ///< Service is running but degraded
    UNHEALTHY     ///< Service is unhealthy
};

inline std::string health_status_to_string(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "healthy";
        case HealthStatus::DEGRADED: return "degraded";
        case HealthStatus::UNHEALTHY: return "unhealthy";
        default: return "unknown";
    }
}

/**
 * @brief Health check result
 */
struct HealthCheckResult {
    HealthStatus status{HealthStatus::HEALTHY};
    std::string message;
    std::map<std::string, std::string> details;
    
    /**
     * @brief Convert to JSON string
     */
    [[nodiscard]] std::string to_json() const {
        std::string json = "{";
        json += "\"status\":\"" + health_status_to_string(status) + "\"";
        if (!message.empty()) {
            json += ",\"message\":\"" + message + "\"";
        }
        if (!details.empty()) {
            json += ",\"details\":{";
            bool first = true;
            for (const auto& [key, value] : details) {
                if (!first) json += ",";
                json += "\"" + key + "\":\"" + value + "\"";
                first = false;
            }
            json += "}";
        }
        json += "}";
        return json;
    }
};

/**
 * @brief Health check callback type
 */
using HealthCheckFunc = std::function<HealthCheckResult()>;

/**
 * @brief Health check registry for multiple components
 */
class HealthChecker {
    std::map<std::string, HealthCheckFunc> checks_;
    
public:
    /**
     * @brief Register a health check
     * @param name Component name
     * @param check Health check function
     */
    void register_check(const std::string& name, HealthCheckFunc check) {
        checks_[name] = std::move(check);
    }
    
    /**
     * @brief Run all health checks
     * @return Combined health check result
     */
    [[nodiscard]] HealthCheckResult check_all() const {
        HealthCheckResult result;
        result.status = HealthStatus::HEALTHY;
        
        for (const auto& [name, check] : checks_) {
            auto component_result = check();
            result.details[name] = health_status_to_string(component_result.status);
            
            // Overall status is worst component status
            if (component_result.status == HealthStatus::UNHEALTHY) {
                result.status = HealthStatus::UNHEALTHY;
            } else if (component_result.status == HealthStatus::DEGRADED && 
                       result.status == HealthStatus::HEALTHY) {
                result.status = HealthStatus::DEGRADED;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Check if all components are healthy
     */
    [[nodiscard]] bool is_healthy() const {
        return check_all().status == HealthStatus::HEALTHY;
    }
};

// =========================================================================
// Graceful Shutdown Support
// =========================================================================

/**
 * @brief Global shutdown flag for graceful termination
 */
inline std::atomic<bool>& shutdown_requested() {
    static std::atomic<bool> flag{false};
    return flag;
}

/**
 * @brief Request graceful shutdown
 */
inline void request_shutdown() {
    shutdown_requested().store(true);
}

/**
 * @brief Check if shutdown has been requested
 */
inline bool is_shutdown_requested() {
    return shutdown_requested().load();
}

/**
 * @brief Shutdown callback type
 */
using ShutdownCallback = std::function<void()>;

/**
 * @brief Shutdown handler registry
 */
class ShutdownHandler {
    std::vector<ShutdownCallback> callbacks_;
    
public:
    /**
     * @brief Register a shutdown callback
     * @param callback Function to call during shutdown
     */
    void on_shutdown(ShutdownCallback callback) {
        callbacks_.push_back(std::move(callback));
    }
    
    /**
     * @brief Execute all shutdown callbacks
     */
    void execute() {
        request_shutdown();
        for (auto it = callbacks_.rbegin(); it != callbacks_.rend(); ++it) {
            try {
                (*it)();
            } catch (...) {
                // Swallow exceptions during shutdown
            }
        }
    }
};

/**
 * @brief Global shutdown handler
 */
inline ShutdownHandler& shutdown_handler() {
    static ShutdownHandler handler;
    return handler;
}

// =========================================================================
// Signal Handling
// =========================================================================

namespace detail {
    inline void signal_handler(int signal) {
        (void)signal; // Unused parameter
        shutdown_handler().execute();
    }
}

/**
 * @brief Install signal handlers for graceful shutdown
 * 
 * Handles:
 * - SIGTERM: Kubernetes termination signal
 * - SIGINT: Ctrl+C
 */
inline void install_signal_handlers() {
    std::signal(SIGTERM, detail::signal_handler);
    std::signal(SIGINT, detail::signal_handler);
#ifndef _WIN32
    std::signal(SIGHUP, SIG_IGN);  // Ignore hangup
#endif
}

// =========================================================================
// Readiness and Liveness Probes
// =========================================================================

/**
 * @brief Probe configuration for Kubernetes-style health checks
 */
struct ProbeConfig {
    int initial_delay_seconds{5};
    int period_seconds{10};
    int timeout_seconds{5};
    int success_threshold{1};
    int failure_threshold{3};
};

/**
 * @brief Probe state tracker
 */
class ProbeState {
    std::atomic<bool> ready_{false};
    std::atomic<bool> live_{true};
    std::chrono::steady_clock::time_point start_time_;
    
public:
    ProbeState() : start_time_(std::chrono::steady_clock::now()) {}
    
    /** @brief Mark service as ready */
    void set_ready(bool ready = true) { ready_.store(ready); }
    
    /** @brief Mark service as live */
    void set_live(bool live = true) { live_.store(live); }
    
    /** @brief Check if service is ready */
    [[nodiscard]] bool is_ready() const { return ready_.load(); }
    
    /** @brief Check if service is live */
    [[nodiscard]] bool is_live() const { return live_.load() && !is_shutdown_requested(); }
    
    /** @brief Get uptime in seconds */
    [[nodiscard]] int64_t uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    }
};

/**
 * @brief Global probe state
 */
inline ProbeState& probe_state() {
    static ProbeState state;
    return state;
}

// =========================================================================
// Service Discovery Helpers (Future Kubernetes Integration)
// =========================================================================

/**
 * @brief Service endpoint information
 */
struct ServiceEndpoint {
    std::string host;
    int port{0};
    std::string protocol{"http"};
    
    [[nodiscard]] std::string url() const {
        return protocol + "://" + host + ":" + std::to_string(port);
    }
};

/**
 * @brief Discover service endpoint from environment
 * @param service_name Kubernetes service name
 * @return ServiceEndpoint if found
 * 
 * Future: Will use Kubernetes DNS or environment variables
 */
inline std::optional<ServiceEndpoint> discover_service(const std::string& service_name) {
    // Kubernetes convention: SERVICE_NAME_SERVICE_HOST, SERVICE_NAME_SERVICE_PORT
    std::string host_var = service_name + "_SERVICE_HOST";
    std::string port_var = service_name + "_SERVICE_PORT";
    
    // Convert to uppercase with underscores
    for (char& c : host_var) {
        c = (c == '-') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (char& c : port_var) {
        c = (c == '-') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    
    const char* host = std::getenv(host_var.c_str());
    const char* port_str = std::getenv(port_var.c_str());
    
    if (host && port_str) {
        ServiceEndpoint ep;
        ep.host = host;
        ep.port = std::stoi(port_str);
        return ep;
    }
    
    return std::nullopt;
}

} // namespace genie::deployment

#endif // GENIE_CORE_DEPLOYMENT_HPP
