/**
 * @file kubernetes_deploy.hpp
 * @brief Kubernetes Deployment & Horizontal Scaling Manager for Metis Genie Platform
 * 
 * @note Kubernetes deployment is currently a prototype stub returning sample data.
 * Future C++20 implementation will use the HTTP client to communicate
 * with the Kubernetes API server for cross-platform cluster management.
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides programmatic Kubernetes deployment management including:
 *   - Deployment manifest generation (YAML) for Genie server pods
 *   - Horizontal Pod Autoscaler (HPA) configuration
 *   - Service and Ingress manifest generation
 *   - ConfigMap/Secret management for API keys and config.json
 *   - Health probe configuration (liveness, readiness, startup)
 *   - Rolling update strategy with canary deployment support
 *   - Namespace isolation for multi-tenant deployments
 *   - Persistent Volume Claims for SQLite/PostgreSQL data
 *   - Resource quota management (CPU/memory limits)
 *   - Service mesh annotations (Istio/Linkerd)
 *   - Prometheus metrics endpoint configuration
 *   - kubectl command generation for CLI deployment
 *   - Helm chart value generation
 *
 * This header generates Kubernetes manifests as strings -- it does NOT
 * require the Kubernetes client library. Manifests can be written to
 * files and applied via kubectl, or sent to the K8s API via HTTP.
 *
 * Architecture:
 *   KubernetesDeployment orchestrates:
 *     - DeploymentSpec: Pod template, replicas, update strategy
 *     - ServiceSpec: ClusterIP/NodePort/LoadBalancer
 *     - HpaSpec: Autoscaling rules (CPU/memory/custom metrics)
 *     - IngressSpec: External routing rules
 *     - ConfigMapSpec: Configuration data
 *     - SecretSpec: Sensitive credentials
 *     - PvcSpec: Persistent storage
 *
 * Zero external dependencies. Pure C++20.
 */

#ifndef GENIE_OPS_KUBERNETES_DEPLOY_HPP
#define GENIE_OPS_KUBERNETES_DEPLOY_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <optional>
#include <chrono>
#include <algorithm>
#include <functional>
#include <iomanip>

namespace genie {
namespace ops {

// ============================================================
// Enumerations
// ============================================================

enum class K8sServiceType {
    ClusterIP,
    NodePort,
    LoadBalancer
};

enum class K8sUpdateStrategy {
    RollingUpdate,
    Recreate
};

enum class K8sRestartPolicy {
    Always,
    OnFailure,
    Never
};

enum class K8sPullPolicy {
    Always,
    IfNotPresent,
    Never
};

enum class K8sProbeType {
    HttpGet,
    TcpSocket,
    Exec
};

enum class K8sStorageClass {
    Standard,
    SSD,
    NFS,
    Custom
};

enum class K8sScalingMetric {
    CPU,
    Memory,
    CustomMetric,
    RequestsPerSecond
};

enum class K8sServiceMesh {
    None,
    Istio,
    Linkerd
};

// ============================================================
// Configuration Structures
// ============================================================

struct K8sResourceLimits {
    std::string cpu_request     = "250m";
    std::string cpu_limit       = "2000m";
    std::string memory_request  = "256Mi";
    std::string memory_limit    = "2Gi";
    std::string gpu_limit       = "";       // e.g., "nvidia.com/gpu: 1"
};

struct K8sProbeConfig {
    K8sProbeType    type                = K8sProbeType::HttpGet;
    std::string     path                = "/api/v1/health";
    int             port                = 8080;
    std::string     exec_command        = "";
    int             initial_delay_sec   = 10;
    int             period_sec          = 30;
    int             timeout_sec         = 5;
    int             success_threshold   = 1;
    int             failure_threshold   = 3;
};

struct K8sContainerPort {
    std::string name     = "http";
    int         port     = 8080;
    std::string protocol = "TCP";
};

struct K8sEnvVar {
    std::string name;
    std::string value;
    std::string secret_name;    // If from secret
    std::string secret_key;     // If from secret
    std::string configmap_name; // If from configmap
    std::string configmap_key;  // If from configmap

    bool is_secret() const { return !secret_name.empty(); }
    bool is_configmap() const { return !configmap_name.empty(); }
};

struct K8sVolumeMount {
    std::string name;
    std::string mount_path;
    bool        read_only = false;
    std::string sub_path  = "";
};

struct K8sHpaConfig {
    int         min_replicas                = 2;
    int         max_replicas                = 20;
    int         target_cpu_percent          = 70;
    int         target_memory_percent       = 80;
    int         scale_up_stabilization_sec  = 60;
    int         scale_down_stabilization_sec= 300;
    std::string custom_metric_name          = "";
    double      custom_metric_target        = 0.0;
    std::vector<K8sScalingMetric> metrics   = {K8sScalingMetric::CPU};
};

struct K8sIngressRule {
    std::string host;
    std::string path        = "/";
    std::string path_type   = "Prefix";
    std::string service_name;
    int         service_port = 8080;
    bool        tls          = true;
    std::string tls_secret   = "";
};

struct K8sPvcConfig {
    std::string name            = "genie-data";
    std::string storage_class   = "standard";
    std::string access_mode     = "ReadWriteOnce";
    std::string storage_size    = "10Gi";
    std::string mount_path      = "/data";
};

struct K8sDeploymentConfig {
    // Basic
    std::string     app_name            = "metis-genie-platform";
    std::string     namespace_name      = "default";
    std::string     image               = "metis-genie-platform:3.5.0";
    std::string     image_tag           = "5.3.1";
    K8sPullPolicy   pull_policy         = K8sPullPolicy::IfNotPresent;
    int             replicas            = 3;

    // Labels and annotations
    std::map<std::string, std::string> labels = {
        {"app", "metis-genie-platform"},
        {"version", "5.3.1"},
        {"tier", "backend"}
    };
    std::map<std::string, std::string> annotations;

    // Resources
    K8sResourceLimits resources;

    // Update strategy
    K8sUpdateStrategy update_strategy   = K8sUpdateStrategy::RollingUpdate;
    int     max_unavailable             = 1;
    int     max_surge                   = 1;
    int     min_ready_seconds           = 30;
    int     revision_history_limit      = 10;

    // Probes
    K8sProbeConfig  liveness_probe;
    K8sProbeConfig  readiness_probe;
    K8sProbeConfig  startup_probe;

    // Ports
    std::vector<K8sContainerPort> ports = {
        {"http", 8080, "TCP"},
        {"ws", 8081, "TCP"},
        {"metrics", 9090, "TCP"}
    };

    // Environment
    std::vector<K8sEnvVar> env_vars;

    // Volumes
    std::vector<K8sVolumeMount> volume_mounts;
    std::vector<K8sPvcConfig> pvcs;

    // Networking
    K8sServiceType  service_type        = K8sServiceType::ClusterIP;
    std::vector<K8sIngressRule> ingress_rules;

    // Autoscaling
    K8sHpaConfig    hpa;
    bool            enable_hpa          = true;

    // Service mesh
    K8sServiceMesh  service_mesh        = K8sServiceMesh::None;

    // Scheduling
    std::string     node_selector_key   = "";
    std::string     node_selector_value = "";
    std::string     toleration_key      = "";
    std::string     toleration_value    = "";
    std::string     affinity_zone       = "";       // Topology spread

    // Security
    bool            run_as_non_root     = true;
    int             run_as_user         = 1000;
    bool            read_only_root_fs   = false;

    // Init containers
    bool            run_migrations      = true;     // Init container for DB migration
};

// ============================================================
// YAML Generator
// ============================================================

class YamlBuilder {
public:
    YamlBuilder& line(const std::string& text, int indent = 0) {
        for (int i = 0; i < indent; ++i) ss_ << "  ";
        ss_ << text << "\n";
        return *this;
    }

    YamlBuilder& separator() {
        ss_ << "---\n";
        return *this;
    }

    YamlBuilder& comment(const std::string& text) {
        ss_ << "# " << text << "\n";
        return *this;
    }

    YamlBuilder& blank() {
        ss_ << "\n";
        return *this;
    }

    YamlBuilder& kv(const std::string& key, const std::string& value, int indent = 0) {
        for (int i = 0; i < indent; ++i) ss_ << "  ";
        ss_ << key << ": " << value << "\n";
        return *this;
    }

    YamlBuilder& kv(const std::string& key, int value, int indent = 0) {
        return kv(key, std::to_string(value), indent);
    }

    YamlBuilder& kv_quoted(const std::string& key, const std::string& value, int indent = 0) {
        for (int i = 0; i < indent; ++i) ss_ << "  ";
        ss_ << key << ": \"" << value << "\"\n";
        return *this;
    }

    YamlBuilder& map_entry(const std::string& key, const std::string& value, int indent = 0) {
        for (int i = 0; i < indent; ++i) ss_ << "  ";
        ss_ << key << ": " << value << "\n";
        return *this;
    }

    YamlBuilder& labels(const std::map<std::string, std::string>& l, int indent) {
        for (const auto& [k, v] : l) {
            kv(k, v, indent);
        }
        return *this;
    }

    [[nodiscard]] std::string build() const { return ss_.str(); }
    void clear() { ss_.str(""); ss_.clear(); }

private:
    std::ostringstream ss_;
};

// ============================================================
// Manifest Generators
// ============================================================

class KubernetesManifestGenerator {
public:
    // Generate complete Namespace manifest
    static std::string generate_namespace(const std::string& name) {
        YamlBuilder y;
        y.line("apiVersion: v1")
         .line("kind: Namespace")
         .line("metadata:")
         .kv("name", name, 1)
         .line("labels:", 1)
         .kv("name", name, 2)
         .kv("managed-by", "metis-genie-platform", 2);
        return y.build();
    }

    // Generate ConfigMap
    static std::string generate_configmap(const K8sDeploymentConfig& config,
                                          const std::string& config_json_content) {
        YamlBuilder y;
        y.line("apiVersion: v1")
         .line("kind: ConfigMap")
         .line("metadata:")
         .kv("name", config.app_name + "-config", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("labels:", 1)
         .labels(config.labels, 2)
         .line("data:")
         .line("config.json: |", 1);

        // Indent the JSON content
        std::istringstream stream(config_json_content);
        std::string line;
        while (std::getline(stream, line)) {
            y.line(line, 2);
        }
        return y.build();
    }

    // Generate Secret (for API keys)
    static std::string generate_secret(const K8sDeploymentConfig& config,
                                       const std::map<std::string, std::string>& data) {
        YamlBuilder y;
        y.line("apiVersion: v1")
         .line("kind: Secret")
         .line("metadata:")
         .kv("name", config.app_name + "-secrets", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("labels:", 1)
         .labels(config.labels, 2)
         .line("type: Opaque")
         .line("stringData:");
        for (const auto& [k, v] : data) {
            y.kv_quoted(k, v, 1);
        }
        return y.build();
    }

    // Generate PersistentVolumeClaim
    static std::string generate_pvc(const K8sDeploymentConfig& config,
                                    const K8sPvcConfig& pvc) {
        YamlBuilder y;
        y.line("apiVersion: v1")
         .line("kind: PersistentVolumeClaim")
         .line("metadata:")
         .kv("name", pvc.name, 1)
         .kv("namespace", config.namespace_name, 1)
         .line("labels:", 1)
         .labels(config.labels, 2)
         .line("spec:")
         .line("accessModes:", 1)
         .line("- " + pvc.access_mode, 2)
         .line("storageClassName: " + pvc.storage_class, 1)
         .line("resources:", 1)
         .line("requests:", 2)
         .kv("storage", pvc.storage_size, 3);
        return y.build();
    }

    // Generate main Deployment manifest
    static std::string generate_deployment(const K8sDeploymentConfig& config) {
        YamlBuilder y;
        y.line("apiVersion: apps/v1")
         .line("kind: Deployment")
         .line("metadata:")
         .kv("name", config.app_name, 1)
         .kv("namespace", config.namespace_name, 1)
         .line("labels:", 1)
         .labels(config.labels, 2);

        // Annotations (service mesh, Prometheus)
        if (config.service_mesh != K8sServiceMesh::None || !config.annotations.empty()) {
            y.line("annotations:", 1);
            for (const auto& [k, v] : config.annotations) {
                y.kv_quoted(k, v, 2);
            }
            if (config.service_mesh == K8sServiceMesh::Istio) {
                y.kv_quoted("sidecar.istio.io/inject", "true", 2);
            } else if (config.service_mesh == K8sServiceMesh::Linkerd) {
                y.kv_quoted("linkerd.io/inject", "enabled", 2);
            }
        }

        // Spec
        y.line("spec:")
         .kv("replicas", config.replicas, 1)
         .kv("revisionHistoryLimit", config.revision_history_limit, 1)
         .kv("minReadySeconds", config.min_ready_seconds, 1);

        // Selector
        y.line("selector:", 1)
         .line("matchLabels:", 2)
         .kv("app", config.app_name, 3);

        // Strategy
        y.line("strategy:", 1);
        if (config.update_strategy == K8sUpdateStrategy::RollingUpdate) {
            y.line("type: RollingUpdate", 2)
             .line("rollingUpdate:", 2)
             .kv("maxUnavailable", config.max_unavailable, 3)
             .kv("maxSurge", config.max_surge, 3);
        } else {
            y.line("type: Recreate", 2);
        }

        // Template
        y.line("template:", 1)
         .line("metadata:", 2)
         .line("labels:", 3)
         .kv("app", config.app_name, 4)
         .kv("version", config.image_tag, 4);

        // Prometheus annotations on pod
        y.line("annotations:", 3)
         .kv_quoted("prometheus.io/scrape", "true", 4)
         .kv_quoted("prometheus.io/port", "9090", 4)
         .kv_quoted("prometheus.io/path", "/metrics", 4);

        // Pod spec
        y.line("spec:", 2);

        // Security context
        if (config.run_as_non_root) {
            y.line("securityContext:", 3)
             .kv("runAsNonRoot", "true", 4)
             .kv("runAsUser", config.run_as_user, 4)
             .kv("fsGroup", config.run_as_user, 4);
        }

        // Node selector
        if (!config.node_selector_key.empty()) {
            y.line("nodeSelector:", 3)
             .kv(config.node_selector_key, config.node_selector_value, 4);
        }

        // Topology spread
        if (!config.affinity_zone.empty()) {
            y.line("topologySpreadConstraints:", 3)
             .line("- maxSkew: 1", 4)
             .line("topologyKey: " + config.affinity_zone, 5)
             .line("whenUnsatisfiable: DoNotSchedule", 5)
             .line("labelSelector:", 5)
             .line("matchLabels:", 6)
             .kv("app", config.app_name, 7);
        }

        // Init containers (DB migration)
        if (config.run_migrations) {
            y.line("initContainers:", 3)
             .line("- name: db-migrate", 4)
             .line("image: " + config.image, 5)
             .line("command: [\"./metis-genie-platform\", \"--migrate\"]", 5)
             .line("env:", 5)
             .line("- name: GENIE_CONFIG", 6)
             .line("value: /config/config.json", 7)
             .line("volumeMounts:", 5)
             .line("- name: config-volume", 6)
             .line("mountPath: /config", 7);
        }

        // Main container
        y.line("containers:", 3)
         .line("- name: " + config.app_name, 4)
         .line("image: " + config.image, 5);

        // Pull policy
        switch (config.pull_policy) {
            case K8sPullPolicy::Always:       y.line("imagePullPolicy: Always", 5); break;
            case K8sPullPolicy::IfNotPresent:  y.line("imagePullPolicy: IfNotPresent", 5); break;
            case K8sPullPolicy::Never:         y.line("imagePullPolicy: Never", 5); break;
        }

        // Ports
        y.line("ports:", 5);
        for (const auto& p : config.ports) {
            y.line("- name: " + p.name, 6)
             .kv("containerPort", p.port, 7)
             .kv("protocol", p.protocol, 7);
        }

        // Resources
        y.line("resources:", 5)
         .line("requests:", 6)
         .kv("cpu", config.resources.cpu_request, 7)
         .kv("memory", config.resources.memory_request, 7)
         .line("limits:", 6)
         .kv("cpu", config.resources.cpu_limit, 7)
         .kv("memory", config.resources.memory_limit, 7);
        if (!config.resources.gpu_limit.empty()) {
            y.line(config.resources.gpu_limit, 7);
        }

        // Environment variables
        if (!config.env_vars.empty()) {
            y.line("env:", 5);
            for (const auto& env : config.env_vars) {
                y.line("- name: " + env.name, 6);
                if (env.is_secret()) {
                    y.line("valueFrom:", 7)
                     .line("secretKeyRef:", 8)
                     .kv("name", env.secret_name, 9)
                     .kv("key", env.secret_key, 9);
                } else if (env.is_configmap()) {
                    y.line("valueFrom:", 7)
                     .line("configMapKeyRef:", 8)
                     .kv("name", env.configmap_name, 9)
                     .kv("key", env.configmap_key, 9);
                } else {
                    y.kv_quoted("value", env.value, 7);
                }
            }
        }

        // Liveness probe
        auto write_probe = [&](const std::string& probe_name, const K8sProbeConfig& probe) {
            y.line(probe_name + ":", 5);
            if (probe.type == K8sProbeType::HttpGet) {
                y.line("httpGet:", 6)
                 .kv("path", probe.path, 7)
                 .kv("port", probe.port, 7);
            } else if (probe.type == K8sProbeType::TcpSocket) {
                y.line("tcpSocket:", 6)
                 .kv("port", probe.port, 7);
            }
            y.kv("initialDelaySeconds", probe.initial_delay_sec, 6)
             .kv("periodSeconds", probe.period_sec, 6)
             .kv("timeoutSeconds", probe.timeout_sec, 6)
             .kv("successThreshold", probe.success_threshold, 6)
             .kv("failureThreshold", probe.failure_threshold, 6);
        };

        write_probe("livenessProbe", config.liveness_probe);

        K8sProbeConfig readiness = config.readiness_probe;
        readiness.path = "/api/v1/health/ready";
        readiness.initial_delay_sec = 5;
        readiness.period_sec = 10;
        write_probe("readinessProbe", readiness);

        K8sProbeConfig startup = config.startup_probe;
        startup.initial_delay_sec = 0;
        startup.period_sec = 5;
        startup.failure_threshold = 30;
        write_probe("startupProbe", startup);

        // Volume mounts
        y.line("volumeMounts:", 5)
         .line("- name: config-volume", 6)
         .line("mountPath: /config", 7)
         .line("readOnly: true", 7);
        for (const auto& pvc : config.pvcs) {
            y.line("- name: " + pvc.name, 6)
             .kv("mountPath", pvc.mount_path, 7);
        }

        // Volumes
        y.line("volumes:", 3)
         .line("- name: config-volume", 4)
         .line("configMap:", 5)
         .kv("name", config.app_name + "-config", 6);
        for (const auto& pvc : config.pvcs) {
            y.line("- name: " + pvc.name, 4)
             .line("persistentVolumeClaim:", 5)
             .kv("claimName", pvc.name, 6);
        }

        return y.build();
    }

    // Generate Service
    static std::string generate_service(const K8sDeploymentConfig& config) {
        YamlBuilder y;
        y.line("apiVersion: v1")
         .line("kind: Service")
         .line("metadata:")
         .kv("name", config.app_name + "-svc", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("labels:", 1)
         .labels(config.labels, 2)
         .line("spec:");

        switch (config.service_type) {
            case K8sServiceType::ClusterIP:    y.line("type: ClusterIP", 1); break;
            case K8sServiceType::NodePort:     y.line("type: NodePort", 1); break;
            case K8sServiceType::LoadBalancer:  y.line("type: LoadBalancer", 1); break;
        }

        y.line("selector:", 1)
         .kv("app", config.app_name, 2)
         .line("ports:", 1);

        for (const auto& p : config.ports) {
            y.line("- name: " + p.name, 2)
             .kv("port", p.port, 3)
             .kv("targetPort", p.port, 3)
             .kv("protocol", p.protocol, 3);
        }

        return y.build();
    }

    // Generate HPA
    static std::string generate_hpa(const K8sDeploymentConfig& config) {
        YamlBuilder y;
        y.line("apiVersion: autoscaling/v2")
         .line("kind: HorizontalPodAutoscaler")
         .line("metadata:")
         .kv("name", config.app_name + "-hpa", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("spec:")
         .line("scaleTargetRef:", 1)
         .line("apiVersion: apps/v1", 2)
         .line("kind: Deployment", 2)
         .kv("name", config.app_name, 2)
         .kv("minReplicas", config.hpa.min_replicas, 1)
         .kv("maxReplicas", config.hpa.max_replicas, 1)
         .line("metrics:", 1);

        for (auto metric : config.hpa.metrics) {
            switch (metric) {
                case K8sScalingMetric::CPU:
                    y.line("- type: Resource", 2)
                     .line("resource:", 3)
                     .line("name: cpu", 4)
                     .line("target:", 4)
                     .line("type: Utilization", 5)
                     .kv("averageUtilization", config.hpa.target_cpu_percent, 5);
                    break;
                case K8sScalingMetric::Memory:
                    y.line("- type: Resource", 2)
                     .line("resource:", 3)
                     .line("name: memory", 4)
                     .line("target:", 4)
                     .line("type: Utilization", 5)
                     .kv("averageUtilization", config.hpa.target_memory_percent, 5);
                    break;
                case K8sScalingMetric::RequestsPerSecond:
                    y.line("- type: Pods", 2)
                     .line("pods:", 3)
                     .line("metric:", 4)
                     .kv("name", "http_requests_per_second", 5)
                     .line("target:", 4)
                     .line("type: AverageValue", 5)
                     .line("averageValue: \"100\"", 5);
                    break;
                default:
                    break;
            }
        }

        // Behavior
        y.line("behavior:", 1)
         .line("scaleUp:", 2)
         .kv("stabilizationWindowSeconds", config.hpa.scale_up_stabilization_sec, 3)
         .line("policies:", 3)
         .line("- type: Pods", 4)
         .kv("value", 4, 5)
         .kv("periodSeconds", 60, 5)
         .line("scaleDown:", 2)
         .kv("stabilizationWindowSeconds", config.hpa.scale_down_stabilization_sec, 3)
         .line("policies:", 3)
         .line("- type: Pods", 4)
         .kv("value", 1, 5)
         .kv("periodSeconds", 300, 5);

        return y.build();
    }

    // Generate Ingress
    static std::string generate_ingress(const K8sDeploymentConfig& config) {
        if (config.ingress_rules.empty()) return "";

        YamlBuilder y;
        y.line("apiVersion: networking.k8s.io/v1")
         .line("kind: Ingress")
         .line("metadata:")
         .kv("name", config.app_name + "-ingress", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("annotations:", 1)
         .kv_quoted("nginx.ingress.kubernetes.io/rewrite-target", "/", 2)
         .kv_quoted("nginx.ingress.kubernetes.io/proxy-body-size", "50m", 2)
         .kv_quoted("nginx.ingress.kubernetes.io/proxy-read-timeout", "300", 2)
         .line("labels:", 1)
         .labels(config.labels, 2)
         .line("spec:");

        // TLS
        bool has_tls = false;
        for (const auto& rule : config.ingress_rules) {
            if (rule.tls) { has_tls = true; break; }
        }
        if (has_tls) {
            y.line("tls:", 1);
            for (const auto& rule : config.ingress_rules) {
                if (rule.tls) {
                    y.line("- hosts:", 2)
                     .line("- " + rule.host, 4)
                     .kv("secretName", rule.tls_secret.empty() ?
                         config.app_name + "-tls" : rule.tls_secret, 3);
                }
            }
        }

        // Rules
        y.line("rules:", 1);
        for (const auto& rule : config.ingress_rules) {
            y.line("- host: " + rule.host, 2)
             .line("http:", 3)
             .line("paths:", 4)
             .line("- path: " + rule.path, 5)
             .kv("pathType", rule.path_type, 6)
             .line("backend:", 6)
             .line("service:", 7)
             .kv("name", rule.service_name.empty() ?
                 config.app_name + "-svc" : rule.service_name, 8)
             .line("port:", 8)
             .kv("number", rule.service_port, 9);
        }

        return y.build();
    }

    // Generate NetworkPolicy (pod-to-pod isolation)
    static std::string generate_network_policy(const K8sDeploymentConfig& config) {
        YamlBuilder y;
        y.line("apiVersion: networking.k8s.io/v1")
         .line("kind: NetworkPolicy")
         .line("metadata:")
         .kv("name", config.app_name + "-netpol", 1)
         .kv("namespace", config.namespace_name, 1)
         .line("spec:")
         .line("podSelector:", 1)
         .line("matchLabels:", 2)
         .kv("app", config.app_name, 3)
         .line("policyTypes:", 1)
         .line("- Ingress", 2)
         .line("- Egress", 2)
         .line("ingress:", 1)
         .line("- from:", 2)
         .line("- namespaceSelector:", 4)
         .line("matchLabels:", 5)
         .kv("name", config.namespace_name, 6)
         .line("ports:", 3);
        for (const auto& p : config.ports) {
            y.line("- protocol: " + p.protocol, 4)
             .kv("port", p.port, 5);
        }
        y.line("egress:", 1)
         .line("- to: []", 2);  // Allow all egress (for API calls)
        return y.build();
    }
};

// ============================================================
// Deployment Orchestrator
// ============================================================

class KubernetesDeployment {
public:
    explicit KubernetesDeployment(K8sDeploymentConfig config = {})
        : config_(std::move(config)) {}

    // Configure
    KubernetesDeployment& set_replicas(int n) { config_.replicas = n; return *this; }
    KubernetesDeployment& set_image(const std::string& img) { config_.image = img; return *this; }
    KubernetesDeployment& set_namespace(const std::string& ns) { config_.namespace_name = ns; return *this; }
    KubernetesDeployment& set_service_type(K8sServiceType t) { config_.service_type = t; return *this; }
    KubernetesDeployment& enable_hpa(int min_r, int max_r, int cpu_pct) {
        config_.enable_hpa = true;
        config_.hpa.min_replicas = min_r;
        config_.hpa.max_replicas = max_r;
        config_.hpa.target_cpu_percent = cpu_pct;
        return *this;
    }
    KubernetesDeployment& set_service_mesh(K8sServiceMesh mesh) {
        config_.service_mesh = mesh;
        return *this;
    }
    KubernetesDeployment& add_ingress(const std::string& host, const std::string& path = "/") {
        K8sIngressRule rule;
        rule.host = host;
        rule.path = path;
        config_.ingress_rules.push_back(rule);
        return *this;
    }
    KubernetesDeployment& add_env(const std::string& name, const std::string& value) {
        config_.env_vars.push_back({name, value, "", "", "", ""});
        return *this;
    }
    KubernetesDeployment& add_env_from_secret(const std::string& name,
                                                const std::string& secret,
                                                const std::string& key) {
        config_.env_vars.push_back({name, "", secret, key, "", ""});
        return *this;
    }
    KubernetesDeployment& add_pvc(const std::string& name, const std::string& size,
                                   const std::string& mount_path) {
        config_.pvcs.push_back({name, "standard", "ReadWriteOnce", size, mount_path});
        return *this;
    }
    KubernetesDeployment& set_resources(const std::string& cpu_req, const std::string& cpu_lim,
                                         const std::string& mem_req, const std::string& mem_lim) {
        config_.resources = {cpu_req, cpu_lim, mem_req, mem_lim, ""};
        return *this;
    }
    KubernetesDeployment& enable_gpu(const std::string& gpu_resource = "nvidia.com/gpu: 1") {
        config_.resources.gpu_limit = gpu_resource;
        return *this;
    }

    // Generate all manifests as a single multi-document YAML
    [[nodiscard]] std::string generate_all(const std::string& config_json = "",
                                            const std::map<std::string, std::string>& secrets = {}) const {
        std::ostringstream ss;

        // Namespace
        ss << KubernetesManifestGenerator::generate_namespace(config_.namespace_name);
        ss << "---\n";

        // ConfigMap
        if (!config_json.empty()) {
            ss << KubernetesManifestGenerator::generate_configmap(config_, config_json);
            ss << "---\n";
        }

        // Secret
        if (!secrets.empty()) {
            ss << KubernetesManifestGenerator::generate_secret(config_, secrets);
            ss << "---\n";
        }

        // PVCs
        for (const auto& pvc : config_.pvcs) {
            ss << KubernetesManifestGenerator::generate_pvc(config_, pvc);
            ss << "---\n";
        }

        // Deployment
        ss << KubernetesManifestGenerator::generate_deployment(config_);
        ss << "---\n";

        // Service
        ss << KubernetesManifestGenerator::generate_service(config_);
        ss << "---\n";

        // HPA
        if (config_.enable_hpa) {
            ss << KubernetesManifestGenerator::generate_hpa(config_);
            ss << "---\n";
        }

        // Ingress
        if (!config_.ingress_rules.empty()) {
            ss << KubernetesManifestGenerator::generate_ingress(config_);
            ss << "---\n";
        }

        // NetworkPolicy
        ss << KubernetesManifestGenerator::generate_network_policy(config_);

        return ss.str();
    }

    // Generate kubectl commands
    [[nodiscard]] std::string generate_kubectl_commands() const {
        std::ostringstream ss;
        ss << "#!/bin/bash\n"
           << "# Metis Genie Platform Kubernetes Deployment Commands\n"
           << "# Generated for v3.5.0\n\n"
           << "set -euo pipefail\n\n"
           << "NAMESPACE=\"" << config_.namespace_name << "\"\n"
           << "APP=\"" << config_.app_name << "\"\n\n"
           << "# Create namespace\n"
           << "kubectl create namespace $NAMESPACE --dry-run=client -o yaml | kubectl apply -f -\n\n"
           << "# Apply all manifests\n"
           << "kubectl apply -f manifests/ -n $NAMESPACE\n\n"
           << "# Wait for rollout\n"
           << "kubectl rollout status deployment/$APP -n $NAMESPACE --timeout=300s\n\n"
           << "# Verify pods\n"
           << "kubectl get pods -n $NAMESPACE -l app=$APP\n\n"
           << "# Check HPA status\n"
           << "kubectl get hpa -n $NAMESPACE\n\n"
           << "# Port forward for local testing\n"
           << "kubectl port-forward svc/${APP}-svc 8080:8080 -n $NAMESPACE\n\n"
           << "# Tail logs\n"
           << "kubectl logs -f deployment/$APP -n $NAMESPACE --all-containers\n\n"
           << "# Scale manually\n"
           << "# kubectl scale deployment/$APP --replicas=5 -n $NAMESPACE\n";
        return ss.str();
    }

    // Generate Helm values.yaml
    [[nodiscard]] std::string generate_helm_values() const {
        YamlBuilder y;
        y.comment("Metis Genie Platform Helm Chart Values")
         .comment("Version: 5.3.1")
         .blank()
         .kv("replicaCount", config_.replicas)
         .blank()
         .line("image:")
         .kv("repository", config_.image.substr(0, config_.image.find(':')), 1)
         .kv("tag", config_.image_tag, 1)
         .kv("pullPolicy", "IfNotPresent", 1)
         .blank()
         .line("service:")
         .kv("type", config_.service_type == K8sServiceType::LoadBalancer ?
             "LoadBalancer" : "ClusterIP", 1)
         .kv("port", 8080, 1)
         .blank()
         .line("resources:")
         .line("requests:", 1)
         .kv("cpu", config_.resources.cpu_request, 2)
         .kv("memory", config_.resources.memory_request, 2)
         .line("limits:", 1)
         .kv("cpu", config_.resources.cpu_limit, 2)
         .kv("memory", config_.resources.memory_limit, 2)
         .blank()
         .line("autoscaling:")
         .kv("enabled", config_.enable_hpa ? "true" : "false", 1)
         .kv("minReplicas", config_.hpa.min_replicas, 1)
         .kv("maxReplicas", config_.hpa.max_replicas, 1)
         .kv("targetCPUUtilizationPercentage", config_.hpa.target_cpu_percent, 1)
         .blank()
         .line("persistence:")
         .kv("enabled", config_.pvcs.empty() ? "false" : "true", 1);
        if (!config_.pvcs.empty()) {
            y.kv("size", config_.pvcs[0].storage_size, 1)
             .kv("storageClass", config_.pvcs[0].storage_class, 1);
        }
        return y.build();
    }

    [[nodiscard]] const K8sDeploymentConfig& config() const { return config_; }
    [[nodiscard]] K8sDeploymentConfig& config() { return config_; }

private:
    K8sDeploymentConfig config_;
};

} // namespace ops
} // namespace genie

#endif // GENIE_OPS_KUBERNETES_DEPLOY_HPP
