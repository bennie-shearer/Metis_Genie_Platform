/**
 * @file openapi_export.hpp
 * @brief OpenAPI 3.0 / Swagger Documentation Export
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Generates OpenAPI 3.0 specification from route definitions:
 * - Automatic route discovery and documentation
 * - Schema generation for request/response models
 * - Security scheme definitions (JWT, API Key, OAuth2)
 * - Tag-based grouping and organization
 * - Example request/response generation
 * - Markdown description support
 * - JSON export for Swagger UI integration
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_OPENAPI_EXPORT_HPP
#define GENIE_CORE_OPENAPI_EXPORT_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <optional>

namespace genie {
namespace core {
namespace openapi {

// ============================================================================
// Enumerations
// ============================================================================

enum class HttpMethod { Get, Post, Put, Patch, Delete, Head, Options };
enum class ParamLocation { Query, Path, Header, Cookie };
enum class SchemaType { String, Integer, Number, Boolean, Array, Object };
enum class SecurityType { ApiKey, Http, OAuth2, OpenIdConnect };

[[nodiscard]] inline std::string method_name(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get:     return "get";
        case HttpMethod::Post:    return "post";
        case HttpMethod::Put:     return "put";
        case HttpMethod::Patch:   return "patch";
        case HttpMethod::Delete:  return "delete";
        case HttpMethod::Head:    return "head";
        case HttpMethod::Options: return "options";
    }
    return "get";
}

[[nodiscard]] inline std::string param_location_name(ParamLocation l) {
    switch (l) {
        case ParamLocation::Query:  return "query";
        case ParamLocation::Path:   return "path";
        case ParamLocation::Header: return "header";
        case ParamLocation::Cookie: return "cookie";
    }
    return "query";
}

[[nodiscard]] inline std::string schema_type_name(SchemaType t) {
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

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief JSON property for schema
 */
struct SchemaProperty {
    std::string name;
    SchemaType type{SchemaType::String};
    std::string description;
    bool required{false};
    std::string format;           // "date-time", "email", "uuid", etc.
    std::string example;
    std::string ref;              // "$ref" to another schema
    std::string items_ref;        // For arrays: items schema ref
    std::string enum_values;      // Comma-separated enum values
    double minimum{0};
    double maximum{0};
    bool has_minimum{false};
    bool has_maximum{false};
};

/**
 * @brief Schema definition (model)
 */
struct Schema {
    std::string name;
    std::string description;
    SchemaType type{SchemaType::Object};
    std::vector<SchemaProperty> properties;
    std::vector<std::string> required_fields;

    [[nodiscard]] std::string to_json(int indent = 6) const {
        std::string pad(indent, ' ');
        std::string pad2(indent + 2, ' ');
        std::string pad3(indent + 4, ' ');
        std::ostringstream oss;

        oss << pad << "\"" << name << "\": {\n";
        oss << pad2 << "\"type\": \"" << schema_type_name(type) << "\"";
        if (!description.empty()) {
            oss << ",\n" << pad2 << "\"description\": \"" << escape_json(description) << "\"";
        }

        if (!properties.empty()) {
            oss << ",\n" << pad2 << "\"properties\": {\n";
            for (size_t i = 0; i < properties.size(); ++i) {
                const auto& p = properties[i];
                oss << pad3 << "\"" << p.name << "\": {";
                if (!p.ref.empty()) {
                    oss << "\"$ref\": \"#/components/schemas/" << p.ref << "\"";
                } else {
                    oss << "\"type\": \"" << schema_type_name(p.type) << "\"";
                    if (!p.format.empty()) oss << ", \"format\": \"" << p.format << "\"";
                    if (!p.description.empty()) oss << ", \"description\": \"" << escape_json(p.description) << "\"";
                    if (!p.example.empty()) oss << ", \"example\": " << format_example(p);
                    if (!p.enum_values.empty()) oss << ", \"enum\": [" << format_enum(p.enum_values) << "]";
                    if (p.has_minimum) oss << ", \"minimum\": " << p.minimum;
                    if (p.has_maximum) oss << ", \"maximum\": " << p.maximum;
                    if (p.type == SchemaType::Array && !p.items_ref.empty()) {
                        oss << ", \"items\": {\"$ref\": \"#/components/schemas/" << p.items_ref << "\"}";
                    }
                }
                oss << "}";
                if (i < properties.size() - 1) oss << ",";
                oss << "\n";
            }
            oss << pad2 << "}";
        }

        if (!required_fields.empty()) {
            oss << ",\n" << pad2 << "\"required\": [";
            for (size_t i = 0; i < required_fields.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << required_fields[i] << "\"";
            }
            oss << "]";
        }

        oss << "\n" << pad << "}";
        return oss.str();
    }

private:
    [[nodiscard]] static std::string escape_json(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                default:   result += c;
            }
        }
        return result;
    }

    [[nodiscard]] static std::string format_example(const SchemaProperty& p) {
        if (p.type == SchemaType::String) return "\"" + p.example + "\"";
        return p.example;
    }

    [[nodiscard]] static std::string format_enum(const std::string& values) {
        std::ostringstream oss;
        std::istringstream iss(values);
        std::string val;
        bool first = true;
        while (std::getline(iss, val, ',')) {
            if (!first) oss << ", ";
            // Trim
            auto start = val.find_first_not_of(' ');
            auto end = val.find_last_not_of(' ');
            if (start != std::string::npos) {
                oss << "\"" << val.substr(start, end - start + 1) << "\"";
            }
            first = false;
        }
        return oss.str();
    }
};

/**
 * @brief API parameter
 */
struct Parameter {
    std::string name;
    ParamLocation location{ParamLocation::Query};
    std::string description;
    bool required{false};
    SchemaType type{SchemaType::String};
    std::string format;
    std::string example;
};

/**
 * @brief Response definition
 */
struct Response {
    int status_code{200};
    std::string description;
    std::string schema_ref;        // Reference to a schema
    std::string content_type{"application/json"};
    std::string example;           // JSON example
};

/**
 * @brief API endpoint (operation)
 */
struct Endpoint {
    std::string path;
    HttpMethod method{HttpMethod::Get};
    std::string summary;
    std::string description;
    std::string operation_id;
    std::vector<std::string> tags;
    std::vector<Parameter> parameters;
    std::string request_body_ref;        // Schema ref for request body
    std::string request_body_desc;
    bool request_body_required{false};
    std::vector<Response> responses;
    std::vector<std::string> security;   // Security scheme names
    bool deprecated{false};
};

/**
 * @brief Tag for grouping endpoints
 */
struct Tag {
    std::string name;
    std::string description;
    std::string external_docs_url;
};

/**
 * @brief Server info
 */
struct ServerInfo {
    std::string url;
    std::string description;
};

/**
 * @brief Security scheme
 */
struct SecurityScheme {
    std::string name;
    SecurityType type{SecurityType::Http};
    std::string description;
    std::string scheme;          // "bearer" for HTTP
    std::string bearer_format;   // "JWT"
    std::string api_key_name;    // Header/query name for ApiKey
    ParamLocation api_key_in{ParamLocation::Header};
};

// ============================================================================
// OpenAPI Spec Builder
// ============================================================================

/**
 * @brief Builds an OpenAPI 3.0 JSON specification
 */
class OpenApiBuilder {
public:
    OpenApiBuilder() = default;

    // -- Info --

    OpenApiBuilder& set_title(const std::string& title) { title_ = title; return *this; }
    OpenApiBuilder& set_description(const std::string& desc) { description_ = desc; return *this; }
    OpenApiBuilder& set_version(const std::string& ver) { version_ = ver; return *this; }
    OpenApiBuilder& set_contact(const std::string& name, const std::string& email,
                                 const std::string& url = "") {
        contact_name_ = name;
        contact_email_ = email;
        contact_url_ = url;
        return *this;
    }
    OpenApiBuilder& set_license(const std::string& name, const std::string& url = "") {
        license_name_ = name;
        license_url_ = url;
        return *this;
    }

    // -- Servers --

    OpenApiBuilder& add_server(const std::string& url, const std::string& description = "") {
        servers_.push_back({url, description});
        return *this;
    }

    // -- Tags --

    OpenApiBuilder& add_tag(const std::string& name, const std::string& description = "") {
        tags_.push_back({name, description, ""});
        return *this;
    }

    // -- Security --

    OpenApiBuilder& add_bearer_auth(const std::string& name = "BearerAuth") {
        SecurityScheme ss;
        ss.name = name;
        ss.type = SecurityType::Http;
        ss.scheme = "bearer";
        ss.bearer_format = "JWT";
        ss.description = "JWT Bearer token authentication";
        security_schemes_[name] = ss;
        return *this;
    }

    OpenApiBuilder& add_api_key_auth(const std::string& name = "ApiKeyAuth",
                                      const std::string& header = "X-API-Key") {
        SecurityScheme ss;
        ss.name = name;
        ss.type = SecurityType::ApiKey;
        ss.api_key_name = header;
        ss.api_key_in = ParamLocation::Header;
        ss.description = "API key authentication via header";
        security_schemes_[name] = ss;
        return *this;
    }

    // -- Schemas --

    OpenApiBuilder& add_schema(Schema schema) {
        schemas_[schema.name] = std::move(schema);
        return *this;
    }

    // -- Endpoints --

    OpenApiBuilder& add_endpoint(Endpoint endpoint) {
        endpoints_.push_back(std::move(endpoint));
        return *this;
    }

    // -- Build --

    /**
     * @brief Generate OpenAPI 3.0 JSON specification
     */
    [[nodiscard]] std::string build() const {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"openapi\": \"3.0.3\",\n";

        // Info
        oss << "  \"info\": {\n";
        oss << "    \"title\": \"" << escape(title_) << "\",\n";
        if (!description_.empty()) {
            oss << "    \"description\": \"" << escape(description_) << "\",\n";
        }
        if (!contact_name_.empty()) {
            oss << "    \"contact\": {\n";
            oss << "      \"name\": \"" << escape(contact_name_) << "\"";
            if (!contact_email_.empty()) oss << ",\n      \"email\": \"" << contact_email_ << "\"";
            if (!contact_url_.empty()) oss << ",\n      \"url\": \"" << contact_url_ << "\"";
            oss << "\n    },\n";
        }
        if (!license_name_.empty()) {
            oss << "    \"license\": {\n";
            oss << "      \"name\": \"" << escape(license_name_) << "\"";
            if (!license_url_.empty()) oss << ",\n      \"url\": \"" << license_url_ << "\"";
            oss << "\n    },\n";
        }
        oss << "    \"version\": \"" << version_ << "\"\n";
        oss << "  }";

        // Servers
        if (!servers_.empty()) {
            oss << ",\n  \"servers\": [\n";
            for (size_t i = 0; i < servers_.size(); ++i) {
                oss << "    {\"url\": \"" << servers_[i].url << "\"";
                if (!servers_[i].description.empty()) {
                    oss << ", \"description\": \"" << escape(servers_[i].description) << "\"";
                }
                oss << "}";
                if (i < servers_.size() - 1) oss << ",";
                oss << "\n";
            }
            oss << "  ]";
        }

        // Tags
        if (!tags_.empty()) {
            oss << ",\n  \"tags\": [\n";
            for (size_t i = 0; i < tags_.size(); ++i) {
                oss << "    {\"name\": \"" << tags_[i].name << "\"";
                if (!tags_[i].description.empty()) {
                    oss << ", \"description\": \"" << escape(tags_[i].description) << "\"";
                }
                oss << "}";
                if (i < tags_.size() - 1) oss << ",";
                oss << "\n";
            }
            oss << "  ]";
        }

        // Paths
        oss << ",\n  \"paths\": {\n";
        // Group endpoints by path
        std::map<std::string, std::vector<const Endpoint*>> path_map;
        for (const auto& ep : endpoints_) {
            path_map[ep.path].push_back(&ep);
        }

        size_t path_idx = 0;
        for (const auto& [path, eps] : path_map) {
            oss << "    \"" << path << "\": {\n";
            for (size_t ei = 0; ei < eps.size(); ++ei) {
                const auto& ep = *eps[ei];
                oss << build_operation(ep, 6);
                if (ei < eps.size() - 1) oss << ",";
                oss << "\n";
            }
            oss << "    }";
            if (++path_idx < path_map.size()) oss << ",";
            oss << "\n";
        }
        oss << "  }";

        // Components
        if (!schemas_.empty() || !security_schemes_.empty()) {
            oss << ",\n  \"components\": {\n";
            bool need_comma = false;

            if (!schemas_.empty()) {
                oss << "    \"schemas\": {\n";
                size_t si = 0;
                for (const auto& [name, schema] : schemas_) {
                    oss << schema.to_json(6);
                    if (++si < schemas_.size()) oss << ",";
                    oss << "\n";
                }
                oss << "    }";
                need_comma = true;
            }

            if (!security_schemes_.empty()) {
                if (need_comma) oss << ",\n";
                oss << "    \"securitySchemes\": {\n";
                size_t si = 0;
                for (const auto& [name, ss] : security_schemes_) {
                    oss << "      \"" << name << "\": {\n";
                    if (ss.type == SecurityType::Http) {
                        oss << "        \"type\": \"http\",\n";
                        oss << "        \"scheme\": \"" << ss.scheme << "\"";
                        if (!ss.bearer_format.empty()) {
                            oss << ",\n        \"bearerFormat\": \"" << ss.bearer_format << "\"";
                        }
                    } else if (ss.type == SecurityType::ApiKey) {
                        oss << "        \"type\": \"apiKey\",\n";
                        oss << "        \"name\": \"" << ss.api_key_name << "\",\n";
                        oss << "        \"in\": \"" << param_location_name(ss.api_key_in) << "\"";
                    }
                    if (!ss.description.empty()) {
                        oss << ",\n        \"description\": \"" << escape(ss.description) << "\"";
                    }
                    oss << "\n      }";
                    if (++si < security_schemes_.size()) oss << ",";
                    oss << "\n";
                }
                oss << "    }";
            }

            oss << "\n  }";
        }

        oss << "\n}\n";
        return oss.str();
    }

    /**
     * @brief Pre-populate with Metis Genie Platform platform endpoints
     */
    void populate_platform_endpoints() {
        set_title("Metis Genie Platform Investment Management Platform");
        set_description("Enterprise investment management REST API");
        set_version("5.3.1");
        set_contact("Bennie Shearer", "support@metisgenie.com");
        set_license("Proprietary");
        add_server("http://localhost:8080", "Development");
        add_server("https://api.metisgenie.com", "Production");
        add_bearer_auth();
        add_api_key_auth();

        add_tag("Authentication", "User authentication and session management");
        add_tag("Portfolios", "Portfolio management operations");
        add_tag("Positions", "Position tracking and management");
        add_tag("Orders", "Order management and execution");
        add_tag("Market Data", "Real-time and historical market data");
        add_tag("Risk", "Risk analytics and monitoring");
        add_tag("Compliance", "Regulatory compliance and reporting");
        add_tag("Crypto", "Cryptocurrency trading and wallet management");
        add_tag("Admin", "System administration");

        // Auth endpoints
        add_endpoint({"POST /api/v1/auth/login", HttpMethod::Post, "User login",
            "Authenticate user and return JWT token", "authLogin", {"Authentication"},
            {}, "LoginRequest", "Login credentials", true,
            {{200, "Successful authentication", "AuthResponse"},
             {401, "Invalid credentials"}}, {}, false});

        // Portfolio endpoints
        add_endpoint({"/api/v1/portfolios", HttpMethod::Get, "List portfolios",
            "Get all portfolios for the authenticated user", "listPortfolios", {"Portfolios"},
            {{"page", ParamLocation::Query, "Page number", false, SchemaType::Integer, "", "1"},
             {"limit", ParamLocation::Query, "Items per page", false, SchemaType::Integer, "", "20"}},
            "", "", false,
            {{200, "Portfolio list", "PortfolioList"}}, {"BearerAuth"}, false});

        add_endpoint({"/api/v1/portfolios/{id}", HttpMethod::Get, "Get portfolio",
            "Get portfolio details by ID", "getPortfolio", {"Portfolios"},
            {{"id", ParamLocation::Path, "Portfolio ID", true, SchemaType::String, "uuid"}},
            "", "", false,
            {{200, "Portfolio details", "Portfolio"}, {404, "Portfolio not found"}},
            {"BearerAuth"}, false});

        add_endpoint({"/api/v1/portfolios", HttpMethod::Post, "Create portfolio",
            "Create a new portfolio", "createPortfolio", {"Portfolios"},
            {}, "CreatePortfolioRequest", "Portfolio details", true,
            {{201, "Portfolio created", "Portfolio"}, {400, "Validation error"}},
            {"BearerAuth"}, false});

        // Orders
        add_endpoint({"/api/v1/orders", HttpMethod::Post, "Submit order",
            "Submit a new trading order", "submitOrder", {"Orders"},
            {}, "OrderRequest", "Order details", true,
            {{201, "Order submitted", "Order"}, {400, "Validation error"}},
            {"BearerAuth"}, false});

        // Risk
        add_endpoint({"/api/v1/risk/var/{portfolio_id}", HttpMethod::Get, "Calculate VaR",
            "Calculate Value-at-Risk for portfolio", "calculateVaR", {"Risk"},
            {{"portfolio_id", ParamLocation::Path, "Portfolio ID", true, SchemaType::String, "uuid"},
             {"confidence", ParamLocation::Query, "Confidence level", false, SchemaType::Number, "", "0.95"},
             {"horizon", ParamLocation::Query, "Time horizon in days", false, SchemaType::Integer, "", "1"}},
            "", "", false,
            {{200, "VaR result", "VaRResult"}}, {"BearerAuth"}, false});

        // Crypto
        add_endpoint({"/api/v1/crypto/wallets", HttpMethod::Get, "List wallets",
            "Get all cryptocurrency wallets", "listCryptoWallets", {"Crypto"},
            {}, "", "", false,
            {{200, "Wallet list", "CryptoWalletList"}}, {"BearerAuth"}, false});

        add_endpoint({"/api/v1/crypto/orders", HttpMethod::Post, "Submit crypto order",
            "Submit cryptocurrency trading order", "submitCryptoOrder", {"Crypto"},
            {}, "CryptoOrderRequest", "Crypto order details", true,
            {{201, "Order submitted", "CryptoOrder"}}, {"BearerAuth"}, false});

        // Common schemas
        Schema error;
        error.name = "ErrorResponse";
        error.description = "Standard error response";
        error.properties = {
            {"code", SchemaType::Integer, "HTTP status code", true, "", "400"},
            {"message", SchemaType::String, "Error message", true, "", "Validation failed"},
            {"details", SchemaType::Array, "Error details", false}
        };
        error.required_fields = {"code", "message"};
        add_schema(error);

        Schema pagination;
        pagination.name = "Pagination";
        pagination.description = "Pagination metadata";
        pagination.properties = {
            {"page", SchemaType::Integer, "Current page", true, "", "1"},
            {"limit", SchemaType::Integer, "Items per page", true, "", "20"},
            {"total", SchemaType::Integer, "Total items", true, "", "150"},
            {"total_pages", SchemaType::Integer, "Total pages", true, "", "8"}
        };
        add_schema(pagination);
    }

private:
    std::string title_{"API"};
    std::string description_;
    std::string version_{"1.0.0"};
    std::string contact_name_;
    std::string contact_email_;
    std::string contact_url_;
    std::string license_name_;
    std::string license_url_;

    std::vector<ServerInfo> servers_;
    std::vector<Tag> tags_;
    std::vector<Endpoint> endpoints_;
    std::map<std::string, Schema> schemas_;
    std::map<std::string, SecurityScheme> security_schemes_;

    [[nodiscard]] static std::string escape(const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            if (c == '"') r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else r += c;
        }
        return r;
    }

    [[nodiscard]] std::string build_operation(const Endpoint& ep, int indent) const {
        std::string pad(indent, ' ');
        std::string pad2(indent + 2, ' ');
        std::string pad3(indent + 4, ' ');
        std::ostringstream oss;

        oss << pad << "\"" << method_name(ep.method) << "\": {\n";
        oss << pad2 << "\"summary\": \"" << escape(ep.summary) << "\"";

        if (!ep.description.empty()) {
            oss << ",\n" << pad2 << "\"description\": \"" << escape(ep.description) << "\"";
        }
        if (!ep.operation_id.empty()) {
            oss << ",\n" << pad2 << "\"operationId\": \"" << ep.operation_id << "\"";
        }
        if (ep.deprecated) {
            oss << ",\n" << pad2 << "\"deprecated\": true";
        }

        // Tags
        if (!ep.tags.empty()) {
            oss << ",\n" << pad2 << "\"tags\": [";
            for (size_t i = 0; i < ep.tags.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << ep.tags[i] << "\"";
            }
            oss << "]";
        }

        // Parameters
        if (!ep.parameters.empty()) {
            oss << ",\n" << pad2 << "\"parameters\": [\n";
            for (size_t i = 0; i < ep.parameters.size(); ++i) {
                const auto& p = ep.parameters[i];
                oss << pad3 << "{";
                oss << "\"name\": \"" << p.name << "\", ";
                oss << "\"in\": \"" << param_location_name(p.location) << "\", ";
                if (!p.description.empty()) {
                    oss << "\"description\": \"" << escape(p.description) << "\", ";
                }
                oss << "\"required\": " << (p.required ? "true" : "false") << ", ";
                oss << "\"schema\": {\"type\": \"" << schema_type_name(p.type) << "\"";
                if (!p.format.empty()) oss << ", \"format\": \"" << p.format << "\"";
                oss << "}";
                oss << "}";
                if (i < ep.parameters.size() - 1) oss << ",";
                oss << "\n";
            }
            oss << pad2 << "]";
        }

        // Request body
        if (!ep.request_body_ref.empty()) {
            oss << ",\n" << pad2 << "\"requestBody\": {\n";
            if (!ep.request_body_desc.empty()) {
                oss << pad3 << "\"description\": \"" << escape(ep.request_body_desc) << "\",\n";
            }
            oss << pad3 << "\"required\": " << (ep.request_body_required ? "true" : "false") << ",\n";
            oss << pad3 << "\"content\": {\"application/json\": {\"schema\": ";
            oss << "{\"$ref\": \"#/components/schemas/" << ep.request_body_ref << "\"}";
            oss << "}}\n";
            oss << pad2 << "}";
        }

        // Responses
        oss << ",\n" << pad2 << "\"responses\": {\n";
        for (size_t i = 0; i < ep.responses.size(); ++i) {
            const auto& r = ep.responses[i];
            oss << pad3 << "\"" << r.status_code << "\": {";
            oss << "\"description\": \"" << escape(r.description) << "\"";
            if (!r.schema_ref.empty()) {
                oss << ", \"content\": {\"" << r.content_type << "\": {\"schema\": ";
                oss << "{\"$ref\": \"#/components/schemas/" << r.schema_ref << "\"}}}";
            }
            oss << "}";
            if (i < ep.responses.size() - 1) oss << ",";
            oss << "\n";
        }
        oss << pad2 << "}";

        // Security
        if (!ep.security.empty()) {
            oss << ",\n" << pad2 << "\"security\": [";
            for (size_t i = 0; i < ep.security.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "{\"" << ep.security[i] << "\": []}";
            }
            oss << "]";
        }

        oss << "\n" << pad << "}";
        return oss.str();
    }
};

} // namespace openapi
} // namespace core
} // namespace genie

#endif // GENIE_CORE_OPENAPI_EXPORT_HPP
