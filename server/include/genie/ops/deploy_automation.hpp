/**
 * @file deployment.hpp
 * @brief Deployment automation scripts and configuration
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Operations - Add deployment automation scripts
 */

#ifndef GENIE_OPS_DEPLOYMENT_HPP
#define GENIE_OPS_DEPLOYMENT_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <mutex>
#include <memory>

namespace genie {
namespace ops {

/**
 * @brief Deployment environment
 */
enum class Environment {
    DEVELOPMENT,
    STAGING,
    PRODUCTION
};

/**
 * @brief Deployment status
 */
enum class DeploymentStatus {
    PENDING,
    PREPARING,
    DEPLOYING,
    VERIFYING,
    COMPLETED,
    FAILED,
    ROLLED_BACK
};

/**
 * @brief Deployment configuration
 */
struct DeploymentConfig {
    std::string app_name{"metis-genie-platform"};
    std::string version;
    Environment environment{Environment::DEVELOPMENT};
    
    // Server configuration
    std::string host{"localhost"};
    int port{8080};
    int worker_threads{4};
    
    // Database
    std::string db_host{"localhost"};
    int db_port{5432};
    std::string db_name{"genie"};
    std::string db_user;
    std::string db_password;
    
    // API Keys (loaded from environment)
    std::map<std::string, std::string> api_keys;
    
    // Feature flags
    std::map<std::string, bool> features;
    
    // Resource limits
    size_t max_memory_mb{2048};
    size_t max_connections{1000};
    
    std::string environment_string() const {
        switch (environment) {
            case Environment::DEVELOPMENT: return "development";
            case Environment::STAGING: return "staging";
            case Environment::PRODUCTION: return "production";
            default: return "unknown";
        }
    }
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{\n";
        json << "  \"app_name\": \"" << app_name << "\",\n";
        json << "  \"version\": \"" << version << "\",\n";
        json << "  \"environment\": \"" << environment_string() << "\",\n";
        json << "  \"server\": {\n";
        json << "    \"host\": \"" << host << "\",\n";
        json << "    \"port\": " << port << ",\n";
        json << "    \"worker_threads\": " << worker_threads << "\n";
        json << "  },\n";
        json << "  \"database\": {\n";
        json << "    \"host\": \"" << db_host << "\",\n";
        json << "    \"port\": " << db_port << ",\n";
        json << "    \"name\": \"" << db_name << "\"\n";
        json << "  },\n";
        json << "  \"limits\": {\n";
        json << "    \"max_memory_mb\": " << max_memory_mb << ",\n";
        json << "    \"max_connections\": " << max_connections << "\n";
        json << "  },\n";
        json << "  \"features\": {\n";
        bool first = true;
        for (const auto& [k, v] : features) {
            if (!first) json << ",\n";
            json << "    \"" << k << "\": " << (v ? "true" : "false");
            first = false;
        }
        json << "\n  }\n";
        json << "}";
        return json.str();
    }
};

/**
 * @brief Deployment record
 */
struct DeploymentRecord {
    std::string id;
    std::string version;
    Environment environment;
    DeploymentStatus status{DeploymentStatus::PENDING};
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::string deployed_by;
    std::string commit_hash;
    std::string previous_version;
    std::vector<std::string> steps_completed;
    std::string error_message;
    std::map<std::string, std::string> metadata;
    
    std::string status_string() const {
        switch (status) {
            case DeploymentStatus::PENDING: return "pending";
            case DeploymentStatus::PREPARING: return "preparing";
            case DeploymentStatus::DEPLOYING: return "deploying";
            case DeploymentStatus::VERIFYING: return "verifying";
            case DeploymentStatus::COMPLETED: return "completed";
            case DeploymentStatus::FAILED: return "failed";
            case DeploymentStatus::ROLLED_BACK: return "rolled_back";
            default: return "unknown";
        }
    }
};

/**
 * @brief Deployment step
 */
struct DeploymentStep {
    std::string name;
    std::string description;
    std::function<bool()> execute;
    std::function<bool()> rollback;
    bool required{true};
    std::chrono::seconds timeout{300};
};

/**
 * @brief Deployment manager
 */
class DeploymentManager {
public:
    using StepCallback = std::function<void(const std::string&, bool)>;
    
    explicit DeploymentManager(const DeploymentConfig& config)
        : config_(config) {
        setup_default_steps();
    }
    
    /**
     * @brief Add custom deployment step
     */
    void add_step(const DeploymentStep& step) {
        std::lock_guard<std::mutex> lock(mutex_);
        steps_.push_back(step);
    }
    
    /**
     * @brief Execute deployment
     */
    DeploymentRecord deploy(const std::string& deployed_by = "system") {
        DeploymentRecord record;
        record.id = generate_deployment_id();
        record.version = config_.version;
        record.environment = config_.environment;
        record.deployed_by = deployed_by;
        record.started_at = std::chrono::system_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        record.status = DeploymentStatus::PREPARING;
        
        // Execute pre-deployment checks
        if (!run_pre_checks()) {
            record.status = DeploymentStatus::FAILED;
            record.error_message = "Pre-deployment checks failed";
            record.completed_at = std::chrono::system_clock::now();
            deployments_.push_back(record);
            return record;
        }
        
        record.status = DeploymentStatus::DEPLOYING;
        
        // Execute deployment steps
        std::vector<size_t> completed_steps;
        for (size_t i = 0; i < steps_.size(); ++i) {
            auto& step = steps_[i];
            
            try {
                if (step.execute()) {
                    record.steps_completed.push_back(step.name);
                    completed_steps.push_back(i);
                    
                    if (step_callback_) {
                        step_callback_(step.name, true);
                    }
                } else if (step.required) {
                    throw std::runtime_error("Step failed: " + step.name);
                }
            } catch (const std::exception& e) {
                record.status = DeploymentStatus::FAILED;
                record.error_message = e.what();
                
                // Rollback completed steps
                for (auto it = completed_steps.rbegin(); it != completed_steps.rend(); ++it) {
                    if (steps_[*it].rollback) {
                        steps_[*it].rollback();
                    }
                }
                
                record.status = DeploymentStatus::ROLLED_BACK;
                record.completed_at = std::chrono::system_clock::now();
                deployments_.push_back(record);
                return record;
            }
        }
        
        record.status = DeploymentStatus::VERIFYING;
        
        // Run post-deployment verification
        if (!run_post_checks()) {
            record.status = DeploymentStatus::FAILED;
            record.error_message = "Post-deployment verification failed";
            
            // Rollback
            for (auto it = completed_steps.rbegin(); it != completed_steps.rend(); ++it) {
                if (steps_[*it].rollback) {
                    steps_[*it].rollback();
                }
            }
            
            record.status = DeploymentStatus::ROLLED_BACK;
        } else {
            record.status = DeploymentStatus::COMPLETED;
        }
        
        record.completed_at = std::chrono::system_clock::now();
        deployments_.push_back(record);
        
        return record;
    }
    
    /**
     * @brief Rollback to previous version
     */
    bool rollback(const std::string& to_version = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find version to rollback to
        std::string target_version = to_version;
        if (target_version.empty() && !deployments_.empty()) {
            // Find last successful deployment
            for (auto it = deployments_.rbegin(); it != deployments_.rend(); ++it) {
                if (it->status == DeploymentStatus::COMPLETED && 
                    it->version != config_.version) {
                    target_version = it->version;
                    break;
                }
            }
        }
        
        if (target_version.empty()) {
            return false;
        }
        
        // Execute rollback steps in reverse order
        for (auto it = steps_.rbegin(); it != steps_.rend(); ++it) {
            if (it->rollback) {
                it->rollback();
            }
        }
        
        return true;
    }
    
    /**
     * @brief Get deployment history
     */
    std::vector<DeploymentRecord> get_history(int limit = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<DeploymentRecord> result;
        int count = std::min(limit, static_cast<int>(deployments_.size()));
        
        for (auto it = deployments_.rbegin(); 
             it != deployments_.rend() && count > 0; 
             ++it, --count) {
            result.push_back(*it);
        }
        
        return result;
    }
    
    /**
     * @brief Set step callback
     */
    void on_step_complete(StepCallback callback) {
        step_callback_ = std::move(callback);
    }
    
    /**
     * @brief Generate Docker Compose file
     */
    std::string generate_docker_compose() const {
        std::ostringstream yaml;
        
        yaml << "version: '3.8'\n\n";
        yaml << "services:\n";
        yaml << "  " << config_.app_name << ":\n";
        yaml << "    image: " << config_.app_name << ":" << config_.version << "\n";
        yaml << "    container_name: " << config_.app_name << "\n";
        yaml << "    restart: unless-stopped\n";
        yaml << "    ports:\n";
        yaml << "      - \"" << config_.port << ":" << config_.port << "\"\n";
        yaml << "    environment:\n";
        yaml << "      - APP_ENV=" << config_.environment_string() << "\n";
        yaml << "      - APP_PORT=" << config_.port << "\n";
        yaml << "      - DB_HOST=" << config_.db_host << "\n";
        yaml << "      - DB_PORT=" << config_.db_port << "\n";
        yaml << "      - DB_NAME=" << config_.db_name << "\n";
        yaml << "      - WORKER_THREADS=" << config_.worker_threads << "\n";
        
        for (const auto& [key, enabled] : config_.features) {
            yaml << "      - FEATURE_" << to_upper(key) << "=" << (enabled ? "true" : "false") << "\n";
        }
        
        yaml << "    deploy:\n";
        yaml << "      resources:\n";
        yaml << "        limits:\n";
        yaml << "          memory: " << config_.max_memory_mb << "M\n";
        yaml << "    healthcheck:\n";
        yaml << "      test: [\"CMD\", \"curl\", \"-f\", \"http://localhost:" << config_.port << "/health\"]\n";
        yaml << "      interval: 30s\n";
        yaml << "      timeout: 10s\n";
        yaml << "      retries: 3\n";
        yaml << "      start_period: 40s\n";
        yaml << "    logging:\n";
        yaml << "      driver: json-file\n";
        yaml << "      options:\n";
        yaml << "        max-size: \"10m\"\n";
        yaml << "        max-file: \"3\"\n";
        yaml << "    networks:\n";
        yaml << "      - genie-network\n";
        yaml << "    depends_on:\n";
        yaml << "      - postgres\n";
        yaml << "      - redis\n\n";
        
        yaml << "  postgres:\n";
        yaml << "    image: postgres:15-alpine\n";
        yaml << "    container_name: genie-postgres\n";
        yaml << "    restart: unless-stopped\n";
        yaml << "    environment:\n";
        yaml << "      - POSTGRES_DB=" << config_.db_name << "\n";
        yaml << "      - POSTGRES_USER=${DB_USER}\n";
        yaml << "      - POSTGRES_PASSWORD=${DB_PASSWORD}\n";
        yaml << "    volumes:\n";
        yaml << "      - postgres-data:/var/lib/postgresql/data\n";
        yaml << "    networks:\n";
        yaml << "      - genie-network\n\n";
        
        yaml << "  redis:\n";
        yaml << "    image: redis:7-alpine\n";
        yaml << "    container_name: genie-redis\n";
        yaml << "    restart: unless-stopped\n";
        yaml << "    networks:\n";
        yaml << "      - genie-network\n\n";
        
        yaml << "networks:\n";
        yaml << "  genie-network:\n";
        yaml << "    driver: bridge\n\n";
        
        yaml << "volumes:\n";
        yaml << "  postgres-data:\n";
        
        return yaml.str();
    }
    
    /**
     * @brief Generate Kubernetes deployment
     */
    std::string generate_k8s_deployment() const {
        std::ostringstream yaml;
        
        yaml << "apiVersion: apps/v1\n";
        yaml << "kind: Deployment\n";
        yaml << "metadata:\n";
        yaml << "  name: " << config_.app_name << "\n";
        yaml << "  labels:\n";
        yaml << "    app: " << config_.app_name << "\n";
        yaml << "    version: \"" << config_.version << "\"\n";
        yaml << "spec:\n";
        yaml << "  replicas: " << (config_.environment == Environment::PRODUCTION ? 3 : 1) << "\n";
        yaml << "  selector:\n";
        yaml << "    matchLabels:\n";
        yaml << "      app: " << config_.app_name << "\n";
        yaml << "  template:\n";
        yaml << "    metadata:\n";
        yaml << "      labels:\n";
        yaml << "        app: " << config_.app_name << "\n";
        yaml << "        version: \"" << config_.version << "\"\n";
        yaml << "    spec:\n";
        yaml << "      containers:\n";
        yaml << "      - name: " << config_.app_name << "\n";
        yaml << "        image: " << config_.app_name << ":" << config_.version << "\n";
        yaml << "        ports:\n";
        yaml << "        - containerPort: " << config_.port << "\n";
        yaml << "        env:\n";
        yaml << "        - name: APP_ENV\n";
        yaml << "          value: \"" << config_.environment_string() << "\"\n";
        yaml << "        - name: APP_PORT\n";
        yaml << "          value: \"" << config_.port << "\"\n";
        yaml << "        - name: DB_HOST\n";
        yaml << "          valueFrom:\n";
        yaml << "            secretKeyRef:\n";
        yaml << "              name: genie-secrets\n";
        yaml << "              key: db-host\n";
        yaml << "        resources:\n";
        yaml << "          limits:\n";
        yaml << "            memory: \"" << config_.max_memory_mb << "Mi\"\n";
        yaml << "            cpu: \"1000m\"\n";
        yaml << "          requests:\n";
        yaml << "            memory: \"256Mi\"\n";
        yaml << "            cpu: \"100m\"\n";
        yaml << "        livenessProbe:\n";
        yaml << "          httpGet:\n";
        yaml << "            path: /health/live\n";
        yaml << "            port: " << config_.port << "\n";
        yaml << "          initialDelaySeconds: 30\n";
        yaml << "          periodSeconds: 10\n";
        yaml << "        readinessProbe:\n";
        yaml << "          httpGet:\n";
        yaml << "            path: /health/ready\n";
        yaml << "            port: " << config_.port << "\n";
        yaml << "          initialDelaySeconds: 5\n";
        yaml << "          periodSeconds: 5\n";
        yaml << "---\n";
        yaml << "apiVersion: v1\n";
        yaml << "kind: Service\n";
        yaml << "metadata:\n";
        yaml << "  name: " << config_.app_name << "-service\n";
        yaml << "spec:\n";
        yaml << "  selector:\n";
        yaml << "    app: " << config_.app_name << "\n";
        yaml << "  ports:\n";
        yaml << "  - protocol: TCP\n";
        yaml << "    port: 80\n";
        yaml << "    targetPort: " << config_.port << "\n";
        yaml << "  type: ClusterIP\n";
        
        return yaml.str();
    }
    
    /**
     * @brief Generate systemd service file
     */
    std::string generate_systemd_service() const {
        std::ostringstream service;
        
        service << "[Unit]\n";
        service << "Description=Metis Genie Platform Investment Platform\n";
        service << "After=network.target postgresql.service\n";
        service << "Wants=postgresql.service\n\n";
        
        service << "[Service]\n";
        service << "Type=simple\n";
        service << "User=genie\n";
        service << "Group=genie\n";
        service << "WorkingDirectory=/opt/" << config_.app_name << "\n";
        service << "ExecStart=/opt/" << config_.app_name << "/bin/" << config_.app_name << "\n";
        service << "Restart=always\n";
        service << "RestartSec=5\n";
        service << "StandardOutput=journal\n";
        service << "StandardError=journal\n";
        service << "SyslogIdentifier=" << config_.app_name << "\n\n";
        
        service << "# Environment\n";
        service << "Environment=APP_ENV=" << config_.environment_string() << "\n";
        service << "Environment=APP_PORT=" << config_.port << "\n";
        service << "Environment=WORKER_THREADS=" << config_.worker_threads << "\n";
        service << "EnvironmentFile=-/etc/" << config_.app_name << "/env\n\n";
        
        service << "# Security\n";
        service << "NoNewPrivileges=yes\n";
        service << "ProtectSystem=strict\n";
        service << "ProtectHome=yes\n";
        service << "PrivateTmp=yes\n";
        service << "ReadWritePaths=/var/lib/" << config_.app_name << " /var/log/" << config_.app_name << "\n\n";
        
        service << "# Resource limits\n";
        service << "MemoryMax=" << config_.max_memory_mb << "M\n";
        service << "TasksMax=1000\n\n";
        
        service << "[Install]\n";
        service << "WantedBy=multi-user.target\n";
        
        return service.str();
    }

private:
    DeploymentConfig config_;
    std::vector<DeploymentStep> steps_;
    std::vector<DeploymentRecord> deployments_;
    StepCallback step_callback_;
    mutable std::mutex mutex_;
    
    void setup_default_steps() {
        // Step 1: Validate configuration
        steps_.push_back({
            "validate_config",
            "Validate deployment configuration",
            [this]() { return validate_config(); },
            nullptr,
            true
        });
        
        // Step 2: Database migrations
        steps_.push_back({
            "database_migration",
            "Run database migrations",
            []() { return true; },  // Placeholder
            []() { return true; },
            true
        });
        
        // Step 3: Deploy application
        steps_.push_back({
            "deploy_app",
            "Deploy application files",
            []() { return true; },  // Placeholder
            []() { return true; },
            true
        });
        
        // Step 4: Update configuration
        steps_.push_back({
            "update_config",
            "Update runtime configuration",
            []() { return true; },  // Placeholder
            []() { return true; },
            true
        });
        
        // Step 5: Restart services
        steps_.push_back({
            "restart_services",
            "Restart application services",
            []() { return true; },  // Placeholder
            []() { return true; },
            true
        });
        
        // Step 6: Clear caches
        steps_.push_back({
            "clear_caches",
            "Clear application caches",
            []() { return true; },  // Placeholder
            nullptr,
            false
        });
    }
    
    bool validate_config() const {
        if (config_.version.empty()) return false;
        if (config_.port <= 0 || config_.port > 65535) return false;
        if (config_.worker_threads <= 0) return false;
        return true;
    }
    
    bool run_pre_checks() {
        // Check disk space, permissions, etc.
        return true;
    }
    
    bool run_post_checks() {
        // Health checks, smoke tests
        return true;
    }
    
    static std::string generate_deployment_id() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream ss;
        ss << "deploy-" << std::put_time(std::localtime(&time_t), "%Y%m%d%H%M%S");
        return ss.str();
    }
    
    static std::string to_upper(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return result;
    }
};

} // namespace ops
} // namespace genie

#endif // GENIE_OPS_DEPLOYMENT_HPP
