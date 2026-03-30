/**
 * @file openapi_export.hpp
 * @brief OpenAPI 3.0 / Swagger specification generator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Generates OpenAPI 3.0 specification from registered endpoints:
 * - Automatic route discovery and documentation
 * - Schema generation from C++ types
 * - Authentication scheme definitions
 * - Request/response examples
 * - Tag-based endpoint grouping
 * - Interactive Swagger UI HTML export
 * - JSON and YAML output formats
 * - Versioned API documentation
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_NET_OPENAPI_EXPORT_HPP
#define GENIE_NET_OPENAPI_EXPORT_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <optional>
#include <set>
#include <functional>

namespace genie {
namespace net {
namespace openapi {

// ============================================================================
// Enumerations
// ============================================================================

enum class HttpMethod {
    GET,
    POST,
    PUT,
    PATCH,
    DELETE_,
    HEAD,
    OPTIONS
};

enum class ParameterLocation {
    Query,
    Path,
    Header,
    Cookie
};

enum class SchemaType {
    String,
    Integer,
    Number,
    Boolean,
    Array,
    Object
};

enum class SecuritySchemeType {
    ApiKey,
    Http,
    OAuth2,
    OpenIdConnect
};

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] inline std::string http_method_string(HttpMethod m) {
    switch (m) {
        case HttpMethod::GET:     return "get";
        case HttpMethod::POST:    return "post";
        case HttpMethod::PUT:     return "put";
        case HttpMethod::PATCH:   return "patch";
        case HttpMethod::DELETE_: return "delete";
        case HttpMethod::HEAD:    return "head";
        case HttpMethod::OPTIONS: return "options";
    }
    return "get";
}

[[nodiscard]] inline std::string param_location_string(ParameterLocation l) {
    switch (l) {
        case ParameterLocation::Query:  return "query";
        case ParameterLocation::Path:   return "path";
        case ParameterLocation::Header: return "header";
        case ParameterLocation::Cookie: return "cookie";
    }
    return "query";
}

[[nodiscard]] inline std::string schema_type_string(SchemaType t) {
    switch (t) {
        case SchemaType::String:  return "string";
        case SchemaType::Integer: return "integer";
        case SchemaType::Number:  return "number";
        case SchemaType::Boolean: return "boolean";
        case SchemaType::Array:   return "array";
        case SchemaType::Object:  return "object";
    }
    return "string";
}

// JSON helper: escape string
[[nodiscard]] inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Schema property definition
 */
struct SchemaProperty {
    std::string name;
    SchemaType type{SchemaType::String};
    std::string description;
    std::string format;           // "date-time", "email", "uri", etc.
    bool required{false};
    std::string example;
    std::string ref;              // $ref to another schema
    std::string items_ref;        // For array items
    SchemaType items_type{SchemaType::String}; // For array of primitives
    std::vector<std::string> enum_values;
    double minimum{0};
    double maximum{0};
    bool has_min{false};
    bool has_max{false};
};

/**
 * @brief Schema definition (model)
 */
struct Schema {
    std::string name;
    std::string description;
    SchemaType type{SchemaType::Object};
    std::vector<SchemaProperty> properties;
    std::string example_json;
    
    [[nodiscard]] std::vector<std::string> required_properties() const {
        std::vector<std::string> req;
        for (const auto& p : properties) {
            if (p.required) req.push_back(p.name);
        }
        return req;
    }
};

/**
 * @brief API parameter
 */
struct Parameter {
    std::string name;
    ParameterLocation location{ParameterLocation::Query};
    std::string description;
    bool required{false};
    SchemaType type{SchemaType::String};
    std::string format;
    std::string example;
    std::vector<std::string> enum_values;
};

/**
 * @brief Request body definition
 */
struct RequestBody {
    std::string description;
    std::string content_type{"application/json"};
    std::string schema_ref;       // Reference to a schema
    std::string example_json;
    bool required{true};
};

/**
 * @brief Response definition
 */
struct Response {
    int status_code{200};
    std::string description;
    std::string content_type{"application/json"};
    std::string schema_ref;
    std::string example_json;
    std::map<std::string, std::string> headers;
};

/**
 * @brief Endpoint (operation) definition
 */
struct Endpoint {
    std::string path;
    HttpMethod method{HttpMethod::GET};
    std::string summary;
    std::string description;
    std::string operation_id;
    std::vector<std::string> tags;
    std::vector<Parameter> parameters;
    std::optional<RequestBody> request_body;
    std::map<int, Response> responses;
    std::vector<std::string> security_schemes;
    bool deprecated{false};
};

/**
 * @brief Tag definition
 */
struct Tag {
    std::string name;
    std::string description;
    std::string external_docs_url;
};

/**
 * @brief Security scheme
 */
struct SecurityScheme {
    std::string name;
    SecuritySchemeType type{SecuritySchemeType::ApiKey};
    std::string description;
    std::string param_name;       // For ApiKey
    ParameterLocation param_in{ParameterLocation::Header};  // For ApiKey
    std::string scheme;           // For Http (e.g., "bearer")
    std::string bearer_format;    // For Http Bearer
};

/**
 * @brief Server definition
 */
struct Server {
    std::string url;
    std::string description;
    std::map<std::string, std::string> variables;
};

/**
 * @brief API info
 */
struct ApiInfo {
    std::string title{"Metis Genie Platform Investment Management API"};
    std::string description{"Enterprise investment management platform REST API"};
    std::string version{"5.3.1"};
    std::string terms_of_service;
    std::string contact_name{"Bennie Shearer"};
    std::string contact_email;
    std::string contact_url;
    std::string license_name{"Proprietary"};
    std::string license_url;
};

// ============================================================================
// OpenAPI Specification Generator
// ============================================================================

/**
 * @brief Generates OpenAPI 3.0 JSON specification
 */
class OpenApiGenerator {
public:
    OpenApiGenerator() {
        register_default_schemas();
        register_default_security();
    }
    
    /**
     * @brief Set API info
     */
    void set_info(ApiInfo info) { info_ = std::move(info); }
    
    /**
     * @brief Add server
     */
    void add_server(Server server) { servers_.push_back(std::move(server)); }
    
    /**
     * @brief Add tag
     */
    void add_tag(Tag tag) { tags_.push_back(std::move(tag)); }
    
    /**
     * @brief Register schema
     */
    void add_schema(Schema schema) {
        std::lock_guard<std::mutex> lock(mutex_);
        schemas_[schema.name] = std::move(schema);
    }
    
    /**
     * @brief Register security scheme
     */
    void add_security_scheme(SecurityScheme scheme) {
        std::lock_guard<std::mutex> lock(mutex_);
        security_schemes_[scheme.name] = std::move(scheme);
    }
    
    /**
     * @brief Register endpoint
     */
    void add_endpoint(Endpoint endpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        endpoints_.push_back(std::move(endpoint));
    }
    
    /**
     * @brief Generate OpenAPI 3.0 JSON specification
     */
    [[nodiscard]] std::string generate_json(int indent = 2) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        std::string ind(indent, ' ');
        std::string ind2(indent * 2, ' ');
        std::string ind3(indent * 3, ' ');
        std::string ind4(indent * 4, ' ');
        std::string ind5(indent * 5, ' ');
        
        oss << "{\n";
        oss << ind << "\"openapi\": \"3.0.3\",\n";
        
        // Info
        oss << ind << "\"info\": {\n";
        oss << ind2 << "\"title\": \"" << json_escape(info_.title) << "\",\n";
        oss << ind2 << "\"description\": \"" << json_escape(info_.description) << "\",\n";
        oss << ind2 << "\"version\": \"" << info_.version << "\"";
        if (!info_.contact_name.empty()) {
            oss << ",\n" << ind2 << "\"contact\": {\n";
            oss << ind3 << "\"name\": \"" << json_escape(info_.contact_name) << "\"";
            if (!info_.contact_email.empty())
                oss << ",\n" << ind3 << "\"email\": \"" << info_.contact_email << "\"";
            oss << "\n" << ind2 << "}";
        }
        if (!info_.license_name.empty()) {
            oss << ",\n" << ind2 << "\"license\": {\n";
            oss << ind3 << "\"name\": \"" << info_.license_name << "\"";
            if (!info_.license_url.empty())
                oss << ",\n" << ind3 << "\"url\": \"" << info_.license_url << "\"";
            oss << "\n" << ind2 << "}";
        }
        oss << "\n" << ind << "},\n";
        
        // Servers
        oss << ind << "\"servers\": [\n";
        for (size_t i = 0; i < servers_.size(); ++i) {
            oss << ind2 << "{\n";
            oss << ind3 << "\"url\": \"" << servers_[i].url << "\",\n";
            oss << ind3 << "\"description\": \"" << json_escape(servers_[i].description) << "\"\n";
            oss << ind2 << "}" << (i + 1 < servers_.size() ? "," : "") << "\n";
        }
        oss << ind << "],\n";
        
        // Tags
        if (!tags_.empty()) {
            oss << ind << "\"tags\": [\n";
            for (size_t i = 0; i < tags_.size(); ++i) {
                oss << ind2 << "{\n";
                oss << ind3 << "\"name\": \"" << tags_[i].name << "\",\n";
                oss << ind3 << "\"description\": \"" << json_escape(tags_[i].description) << "\"\n";
                oss << ind2 << "}" << (i + 1 < tags_.size() ? "," : "") << "\n";
            }
            oss << ind << "],\n";
        }
        
        // Paths
        oss << ind << "\"paths\": {\n";
        // Group endpoints by path
        std::map<std::string, std::vector<const Endpoint*>> path_map;
        for (const auto& ep : endpoints_) {
            path_map[ep.path].push_back(&ep);
        }
        
        size_t path_idx = 0;
        for (const auto& [path, eps] : path_map) {
            oss << ind2 << "\"" << path << "\": {\n";
            for (size_t ei = 0; ei < eps.size(); ++ei) {
                const auto& ep = *eps[ei];
                oss << ind3 << "\"" << http_method_string(ep.method) << "\": {\n";
                oss << ind4 << "\"summary\": \"" << json_escape(ep.summary) << "\",\n";
                if (!ep.description.empty())
                    oss << ind4 << "\"description\": \"" << json_escape(ep.description) << "\",\n";
                if (!ep.operation_id.empty())
                    oss << ind4 << "\"operationId\": \"" << ep.operation_id << "\",\n";
                if (ep.deprecated)
                    oss << ind4 << "\"deprecated\": true,\n";
                
                // Tags
                if (!ep.tags.empty()) {
                    oss << ind4 << "\"tags\": [";
                    for (size_t t = 0; t < ep.tags.size(); ++t) {
                        if (t > 0) oss << ", ";
                        oss << "\"" << ep.tags[t] << "\"";
                    }
                    oss << "],\n";
                }
                
                // Parameters
                if (!ep.parameters.empty()) {
                    oss << ind4 << "\"parameters\": [\n";
                    for (size_t pi = 0; pi < ep.parameters.size(); ++pi) {
                        const auto& p = ep.parameters[pi];
                        oss << ind5 << "{\n";
                        oss << ind5 << "  \"name\": \"" << p.name << "\",\n";
                        oss << ind5 << "  \"in\": \"" << param_location_string(p.location) << "\",\n";
                        if (!p.description.empty())
                            oss << ind5 << "  \"description\": \"" << json_escape(p.description) << "\",\n";
                        oss << ind5 << "  \"required\": " << (p.required ? "true" : "false") << ",\n";
                        oss << ind5 << "  \"schema\": {\"type\": \"" << schema_type_string(p.type) << "\"";
                        if (!p.format.empty()) oss << ", \"format\": \"" << p.format << "\"";
                        oss << "}\n";
                        oss << ind5 << "}" << (pi + 1 < ep.parameters.size() ? "," : "") << "\n";
                    }
                    oss << ind4 << "],\n";
                }
                
                // Request body
                if (ep.request_body) {
                    oss << ind4 << "\"requestBody\": {\n";
                    if (!ep.request_body->description.empty())
                        oss << ind5 << "\"description\": \"" << json_escape(ep.request_body->description) << "\",\n";
                    oss << ind5 << "\"required\": " << (ep.request_body->required ? "true" : "false") << ",\n";
                    oss << ind5 << "\"content\": {\n";
                    oss << ind5 << "  \"" << ep.request_body->content_type << "\": {\n";
                    if (!ep.request_body->schema_ref.empty())
                        oss << ind5 << "    \"schema\": {\"$ref\": \"#/components/schemas/" << ep.request_body->schema_ref << "\"}\n";
                    oss << ind5 << "  }\n";
                    oss << ind5 << "}\n";
                    oss << ind4 << "},\n";
                }
                
                // Responses
                oss << ind4 << "\"responses\": {\n";
                size_t ri = 0;
                for (const auto& [code, resp] : ep.responses) {
                    oss << ind5 << "\"" << code << "\": {\n";
                    oss << ind5 << "  \"description\": \"" << json_escape(resp.description) << "\"";
                    if (!resp.schema_ref.empty()) {
                        oss << ",\n" << ind5 << "  \"content\": {\n";
                        oss << ind5 << "    \"" << resp.content_type << "\": {\n";
                        oss << ind5 << "      \"schema\": {\"$ref\": \"#/components/schemas/" << resp.schema_ref << "\"}\n";
                        oss << ind5 << "    }\n";
                        oss << ind5 << "  }";
                    }
                    oss << "\n" << ind5 << "}" << (++ri < ep.responses.size() ? "," : "") << "\n";
                }
                oss << ind4 << "}\n";
                
                oss << ind3 << "}" << (ei + 1 < eps.size() ? "," : "") << "\n";
            }
            oss << ind2 << "}" << (++path_idx < path_map.size() ? "," : "") << "\n";
        }
        oss << ind << "},\n";
        
        // Components
        oss << ind << "\"components\": {\n";
        
        // Schemas
        oss << ind2 << "\"schemas\": {\n";
        size_t si = 0;
        for (const auto& [name, schema] : schemas_) {
            oss << ind3 << "\"" << name << "\": {\n";
            oss << ind4 << "\"type\": \"" << schema_type_string(schema.type) << "\"";
            if (!schema.description.empty())
                oss << ",\n" << ind4 << "\"description\": \"" << json_escape(schema.description) << "\"";
            
            auto req = schema.required_properties();
            if (!req.empty()) {
                oss << ",\n" << ind4 << "\"required\": [";
                for (size_t r = 0; r < req.size(); ++r) {
                    if (r > 0) oss << ", ";
                    oss << "\"" << req[r] << "\"";
                }
                oss << "]";
            }
            
            if (!schema.properties.empty()) {
                oss << ",\n" << ind4 << "\"properties\": {\n";
                for (size_t pi = 0; pi < schema.properties.size(); ++pi) {
                    const auto& p = schema.properties[pi];
                    oss << ind5 << "\"" << p.name << "\": {";
                    if (!p.ref.empty()) {
                        oss << "\"$ref\": \"#/components/schemas/" << p.ref << "\"";
                    } else {
                        oss << "\"type\": \"" << schema_type_string(p.type) << "\"";
                        if (!p.format.empty()) oss << ", \"format\": \"" << p.format << "\"";
                        if (!p.description.empty()) oss << ", \"description\": \"" << json_escape(p.description) << "\"";
                        if (!p.example.empty()) oss << ", \"example\": \"" << json_escape(p.example) << "\"";
                        if (p.type == SchemaType::Array && !p.items_ref.empty())
                            oss << ", \"items\": {\"$ref\": \"#/components/schemas/" << p.items_ref << "\"}";
                        if (!p.enum_values.empty()) {
                            oss << ", \"enum\": [";
                            for (size_t e = 0; e < p.enum_values.size(); ++e) {
                                if (e > 0) oss << ", ";
                                oss << "\"" << p.enum_values[e] << "\"";
                            }
                            oss << "]";
                        }
                    }
                    oss << "}" << (pi + 1 < schema.properties.size() ? "," : "") << "\n";
                }
                oss << ind4 << "}";
            }
            
            oss << "\n" << ind3 << "}" << (++si < schemas_.size() ? "," : "") << "\n";
        }
        oss << ind2 << "},\n";
        
        // Security Schemes
        oss << ind2 << "\"securitySchemes\": {\n";
        size_t ssi = 0;
        for (const auto& [name, scheme] : security_schemes_) {
            oss << ind3 << "\"" << name << "\": {\n";
            switch (scheme.type) {
                case SecuritySchemeType::ApiKey:
                    oss << ind4 << "\"type\": \"apiKey\",\n";
                    oss << ind4 << "\"name\": \"" << scheme.param_name << "\",\n";
                    oss << ind4 << "\"in\": \"" << param_location_string(scheme.param_in) << "\"";
                    break;
                case SecuritySchemeType::Http:
                    oss << ind4 << "\"type\": \"http\",\n";
                    oss << ind4 << "\"scheme\": \"" << scheme.scheme << "\"";
                    if (!scheme.bearer_format.empty())
                        oss << ",\n" << ind4 << "\"bearerFormat\": \"" << scheme.bearer_format << "\"";
                    break;
                default:
                    oss << ind4 << "\"type\": \"apiKey\"";
            }
            if (!scheme.description.empty())
                oss << ",\n" << ind4 << "\"description\": \"" << json_escape(scheme.description) << "\"";
            oss << "\n" << ind3 << "}" << (++ssi < security_schemes_.size() ? "," : "") << "\n";
        }
        oss << ind2 << "}\n";
        
        oss << ind << "}\n";
        oss << "}\n";
        
        return oss.str();
    }
    
    /**
     * @brief Generate Swagger UI HTML page
     */
    [[nodiscard]] std::string generate_swagger_html() const {
        std::ostringstream oss;
        oss << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << json_escape(info_.title) << R"( - API Documentation</title>
    <link rel="stylesheet" type="text/css" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css">
    <style>
        html { box-sizing: border-box; overflow-y: scroll; }
        *, *:before, *:after { box-sizing: inherit; }
        body { margin: 0; background: #fafafa; }
        .topbar { display: none; }
    </style>
</head>
<body>
    <div id="swagger-ui"></div>
    <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
    <script>
        const spec = )" << generate_json(0) << R"(;
        SwaggerUIBundle({
            spec: spec,
            dom_id: '#swagger-ui',
            deepLinking: true,
            presets: [
                SwaggerUIBundle.presets.apis,
                SwaggerUIBundle.SwaggerUIStandalonePreset
            ],
            layout: "BaseLayout"
        });
    </script>
</body>
</html>)";
        return oss.str();
    }
    
    /**
     * @brief Get registered endpoint count
     */
    [[nodiscard]] int endpoint_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(endpoints_.size());
    }
    
    /**
     * @brief Get registered schema count
     */
    [[nodiscard]] int schema_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(schemas_.size());
    }
    
    /**
     * @brief List all paths
     */
    [[nodiscard]] std::vector<std::string> list_paths() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> paths;
        for (const auto& ep : endpoints_) paths.insert(ep.path);
        return {paths.begin(), paths.end()};
    }

private:
    mutable std::mutex mutex_;
    ApiInfo info_;
    std::vector<Server> servers_;
    std::vector<Tag> tags_;
    std::vector<Endpoint> endpoints_;
    std::map<std::string, Schema> schemas_;
    std::map<std::string, SecurityScheme> security_schemes_;
    
    void register_default_schemas() {
        // Error response
        {
            Schema s;
            s.name = "ErrorResponse";
            s.description = "Standard error response";
            s.properties = {
                {"error", SchemaType::String, "Error message", "", true, "Not found", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"code", SchemaType::Integer, "HTTP status code", "", true, "404", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"details", SchemaType::String, "Additional error details", "", false, "", "", "", SchemaType::String, {}, 0, 0, false, false}
            };
            schemas_[s.name] = s;
        }
        
        // Pagination
        {
            Schema s;
            s.name = "PaginationMeta";
            s.description = "Pagination metadata";
            s.properties = {
                {"page", SchemaType::Integer, "Current page", "", true, "1", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"per_page", SchemaType::Integer, "Items per page", "", true, "20", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"total", SchemaType::Integer, "Total items", "", true, "100", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"total_pages", SchemaType::Integer, "Total pages", "", true, "5", "", "", SchemaType::String, {}, 0, 0, false, false}
            };
            schemas_[s.name] = s;
        }
        
        // Health check
        {
            Schema s;
            s.name = "HealthCheck";
            s.description = "System health status";
            s.properties = {
                {"status", SchemaType::String, "Health status", "", true, "healthy", "", "", SchemaType::String, {"healthy", "degraded", "unhealthy"}, 0, 0, false, false},
                {"version", SchemaType::String, "API version", "", true, "5.3.1", "", "", SchemaType::String, {}, 0, 0, false, false},
                {"uptime_seconds", SchemaType::Number, "Server uptime", "", false, "3600", "", "", SchemaType::String, {}, 0, 0, false, false}
            };
            schemas_[s.name] = s;
        }
    }
    
    void register_default_security() {
        // Bearer token
        {
            SecurityScheme ss;
            ss.name = "BearerAuth";
            ss.type = SecuritySchemeType::Http;
            ss.scheme = "bearer";
            ss.bearer_format = "JWT";
            ss.description = "JWT Bearer token authentication";
            security_schemes_[ss.name] = ss;
        }
        
        // API Key
        {
            SecurityScheme ss;
            ss.name = "ApiKeyAuth";
            ss.type = SecuritySchemeType::ApiKey;
            ss.param_name = "X-API-Key";
            ss.param_in = ParameterLocation::Header;
            ss.description = "API key authentication";
            security_schemes_[ss.name] = ss;
        }
    }
};

// ============================================================================
// Auto-Registration Helper
// ============================================================================

/**
 * @brief Fluent endpoint builder for easy registration
 */
class EndpointBuilder {
public:
    explicit EndpointBuilder(OpenApiGenerator& gen) : gen_(gen) {}
    
    EndpointBuilder& get(const std::string& path) { ep_.path = path; ep_.method = HttpMethod::GET; return *this; }
    EndpointBuilder& post(const std::string& path) { ep_.path = path; ep_.method = HttpMethod::POST; return *this; }
    EndpointBuilder& put(const std::string& path) { ep_.path = path; ep_.method = HttpMethod::PUT; return *this; }
    EndpointBuilder& patch(const std::string& path) { ep_.path = path; ep_.method = HttpMethod::PATCH; return *this; }
    EndpointBuilder& del(const std::string& path) { ep_.path = path; ep_.method = HttpMethod::DELETE_; return *this; }
    
    EndpointBuilder& summary(const std::string& s) { ep_.summary = s; return *this; }
    EndpointBuilder& description(const std::string& d) { ep_.description = d; return *this; }
    EndpointBuilder& operation_id(const std::string& id) { ep_.operation_id = id; return *this; }
    EndpointBuilder& tag(const std::string& t) { ep_.tags.push_back(t); return *this; }
    EndpointBuilder& deprecated() { ep_.deprecated = true; return *this; }
    
    EndpointBuilder& query_param(const std::string& name, SchemaType type = SchemaType::String,
                                   const std::string& desc = "", bool required = false) {
        Parameter p;
        p.name = name; p.type = type; p.description = desc; p.required = required;
        p.location = ParameterLocation::Query;
        ep_.parameters.push_back(p);
        return *this;
    }
    
    EndpointBuilder& path_param(const std::string& name, SchemaType type = SchemaType::String,
                                  const std::string& desc = "") {
        Parameter p;
        p.name = name; p.type = type; p.description = desc; p.required = true;
        p.location = ParameterLocation::Path;
        ep_.parameters.push_back(p);
        return *this;
    }
    
    EndpointBuilder& body(const std::string& schema_ref, const std::string& desc = "") {
        RequestBody rb;
        rb.schema_ref = schema_ref;
        rb.description = desc;
        ep_.request_body = rb;
        return *this;
    }
    
    EndpointBuilder& response(int code, const std::string& desc, 
                                const std::string& schema_ref = "") {
        Response r;
        r.status_code = code;
        r.description = desc;
        r.schema_ref = schema_ref;
        ep_.responses[code] = r;
        return *this;
    }
    
    EndpointBuilder& security(const std::string& scheme) {
        ep_.security_schemes.push_back(scheme);
        return *this;
    }
    
    void build() {
        gen_.add_endpoint(ep_);
        ep_ = Endpoint{};
    }

private:
    OpenApiGenerator& gen_;
    Endpoint ep_;
};

/**
 * @brief Register all Metis Genie Platform API endpoints
 */
inline void register_genie_endpoints(OpenApiGenerator& gen) {
    // Add servers
    gen.add_server({"http://localhost:8080", "Local development", {}});
    gen.add_server({"https://api.genie.local", "Production", {}});
    
    // Add tags
    gen.add_tag({"Portfolio", "Portfolio management endpoints", ""});
    gen.add_tag({"Trading", "Order execution and management", ""});
    gen.add_tag({"Market Data", "Market data feeds and queries", ""});
    gen.add_tag({"Risk", "Risk analytics and reporting", ""});
    gen.add_tag({"Analytics", "Performance and factor analytics", ""});
    gen.add_tag({"Compliance", "Regulatory compliance", ""});
    gen.add_tag({"Admin", "System administration", ""});
    gen.add_tag({"Crypto", "Cryptocurrency trading", ""});
    
    EndpointBuilder eb(gen);
    
    // Health
    eb.get("/api/v1/health").summary("Health check").tag("Admin")
      .operation_id("getHealth")
      .response(200, "System is healthy", "HealthCheck")
      .build();
    
    // Portfolio endpoints
    eb.get("/api/v1/portfolios").summary("List portfolios").tag("Portfolio")
      .operation_id("listPortfolios")
      .query_param("page", SchemaType::Integer, "Page number")
      .query_param("per_page", SchemaType::Integer, "Items per page")
      .security("BearerAuth")
      .response(200, "Portfolio list")
      .response(401, "Unauthorized", "ErrorResponse")
      .build();
    
    eb.get("/api/v1/portfolios/{id}").summary("Get portfolio").tag("Portfolio")
      .operation_id("getPortfolio")
      .path_param("id", SchemaType::String, "Portfolio ID")
      .security("BearerAuth")
      .response(200, "Portfolio details")
      .response(404, "Not found", "ErrorResponse")
      .build();
    
    eb.get("/api/v1/portfolios/{id}/positions").summary("Get positions").tag("Portfolio")
      .operation_id("getPositions")
      .path_param("id", SchemaType::String, "Portfolio ID")
      .security("BearerAuth")
      .response(200, "Position list")
      .build();
    
    eb.get("/api/v1/portfolios/{id}/performance").summary("Get performance").tag("Analytics")
      .operation_id("getPerformance")
      .path_param("id", SchemaType::String, "Portfolio ID")
      .query_param("period", SchemaType::String, "Time period")
      .security("BearerAuth")
      .response(200, "Performance data")
      .build();
    
    // Trading endpoints
    eb.post("/api/v1/orders").summary("Submit order").tag("Trading")
      .operation_id("submitOrder")
      .security("BearerAuth")
      .response(201, "Order submitted")
      .response(400, "Validation error", "ErrorResponse")
      .build();
    
    eb.get("/api/v1/orders").summary("List orders").tag("Trading")
      .operation_id("listOrders")
      .query_param("status", SchemaType::String, "Filter by status")
      .security("BearerAuth")
      .response(200, "Order list")
      .build();
    
    eb.del("/api/v1/orders/{id}").summary("Cancel order").tag("Trading")
      .operation_id("cancelOrder")
      .path_param("id", SchemaType::String, "Order ID")
      .security("BearerAuth")
      .response(200, "Order cancelled")
      .response(404, "Not found", "ErrorResponse")
      .build();
    
    // Market data
    eb.get("/api/v1/market/quotes/{symbol}").summary("Get quote").tag("Market Data")
      .operation_id("getQuote")
      .path_param("symbol", SchemaType::String, "Ticker symbol")
      .response(200, "Quote data")
      .build();
    
    eb.get("/api/v1/market/history/{symbol}").summary("Get historical data").tag("Market Data")
      .operation_id("getHistory")
      .path_param("symbol", SchemaType::String, "Ticker symbol")
      .query_param("from", SchemaType::String, "Start date")
      .query_param("to", SchemaType::String, "End date")
      .response(200, "Historical data")
      .build();
    
    // Risk
    eb.get("/api/v1/risk/{portfolio_id}/var").summary("Calculate VaR").tag("Risk")
      .operation_id("calculateVaR")
      .path_param("portfolio_id", SchemaType::String)
      .query_param("confidence", SchemaType::Number, "Confidence level")
      .query_param("horizon", SchemaType::Integer, "Horizon in days")
      .security("BearerAuth")
      .response(200, "VaR result")
      .build();
    
    // Crypto
    eb.get("/api/v1/crypto/wallets").summary("List wallets").tag("Crypto")
      .operation_id("listWallets")
      .security("BearerAuth")
      .response(200, "Wallet list")
      .build();
    
    eb.post("/api/v1/crypto/orders").summary("Submit crypto order").tag("Crypto")
      .operation_id("submitCryptoOrder")
      .security("BearerAuth")
      .response(201, "Crypto order submitted")
      .build();
    
    eb.get("/api/v1/crypto/staking").summary("Get staking positions").tag("Crypto")
      .operation_id("getStaking")
      .security("BearerAuth")
      .response(200, "Staking positions")
      .build();
}

} // namespace openapi
} // namespace net
} // namespace genie

#endif // GENIE_NET_OPENAPI_EXPORT_HPP
