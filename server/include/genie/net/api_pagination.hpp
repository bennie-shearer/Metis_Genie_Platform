/**
 * @file api_pagination.hpp
 * @brief API pagination and filtering support
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements REST API pagination and filtering:
 * - Page-based pagination with metadata
 * - Flexible filtering with operators
 * - Search functionality
 * - Date range filtering
 */

#pragma once
#ifndef GENIE_NET_API_PAGINATION_HPP
#define GENIE_NET_API_PAGINATION_HPP

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace genie::net {

/**
 * @brief Pagination parameters from request
 */
struct PaginationParams {
    int page{1};
    int limit{50};
    std::string sort_by;
    std::string order{"desc"};  // "asc" or "desc"
    
    [[nodiscard]] int offset() const {
        return (page - 1) * limit;
    }
    
    [[nodiscard]] bool is_ascending() const {
        return order == "asc";
    }
};

/**
 * @brief Pagination metadata for response
 */
struct PaginationMeta {
    int page{1};
    int limit{50};
    int total{0};
    int total_pages{1};
    bool has_next{false};
    bool has_prev{false};
    
    PaginationMeta() = default;
    
    PaginationMeta(int p, int l, int t)
        : page(p), limit(l), total(t) {
        total_pages = (total + limit - 1) / limit;
        if (total_pages < 1) total_pages = 1;
        has_next = page < total_pages;
        has_prev = page > 1;
    }
    
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"page\":" << page << ",";
        oss << "\"limit\":" << limit << ",";
        oss << "\"total\":" << total << ",";
        oss << "\"total_pages\":" << total_pages << ",";
        oss << "\"has_next\":" << (has_next ? "true" : "false") << ",";
        oss << "\"has_prev\":" << (has_prev ? "true" : "false");
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief Filter parameters from request
 */
struct FilterParams {
    std::map<std::string, std::string> equals;       // field=value
    std::map<std::string, std::string> contains;     // field__contains=value
    std::map<std::string, double> greater_than;      // field__gt=value
    std::map<std::string, double> less_than;         // field__lt=value
    std::map<std::string, double> greater_equal;     // field__gte=value
    std::map<std::string, double> less_equal;        // field__lte=value
    std::optional<std::string> search;               // q=search_term
    std::optional<std::string> from_date;            // from=2026-01-01
    std::optional<std::string> to_date;              // to=2026-12-31
    std::vector<std::string> in_list;                // field__in=a,b,c
};

/**
 * @brief Paginated response wrapper
 */
template<typename T>
struct PaginatedResponse {
    std::vector<T> data;
    PaginationMeta pagination;
    
    /**
     * @brief Convert to JSON string
     * @param serializer Function to serialize each item
     */
    [[nodiscard]] std::string to_json(
        std::function<std::string(const T&)> serializer
    ) const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"data\":[";
        
        for (size_t i = 0; i < data.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializer(data[i]);
        }
        
        oss << "],";
        oss << "\"pagination\":" << pagination.to_json();
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief Parse pagination parameters from query string
 */
inline PaginationParams parse_pagination(
    const std::map<std::string, std::string>& query_params
) {
    PaginationParams params;
    
    auto it = query_params.find("page");
    if (it != query_params.end()) {
        try {
            params.page = std::stoi(it->second);
            if (params.page < 1) params.page = 1;
        } catch (...) {
            params.page = 1;
        }
    }
    
    it = query_params.find("limit");
    if (it != query_params.end()) {
        try {
            params.limit = std::stoi(it->second);
            if (params.limit < 1) params.limit = 1;
            if (params.limit > 1000) params.limit = 1000;  // Max limit
        } catch (...) {
            params.limit = 50;
        }
    }
    
    it = query_params.find("sort");
    if (it != query_params.end()) {
        params.sort_by = it->second;
    }
    
    it = query_params.find("order");
    if (it != query_params.end()) {
        params.order = (it->second == "asc") ? "asc" : "desc";
    }
    
    return params;
}

/**
 * @brief Parse filter parameters from query string
 */
inline FilterParams parse_filters(
    const std::map<std::string, std::string>& query_params
) {
    FilterParams filters;
    
    for (const auto& [key, value] : query_params) {
        // Skip pagination params
        if (key == "page" || key == "limit" || key == "sort" || key == "order") {
            continue;
        }
        
        // Search query
        if (key == "q" || key == "search") {
            filters.search = value;
            continue;
        }
        
        // Date range
        if (key == "from" || key == "from_date" || key == "after") {
            filters.from_date = value;
            continue;
        }
        if (key == "to" || key == "to_date" || key == "before") {
            filters.to_date = value;
            continue;
        }
        
        // Check for operators
        size_t pos = key.find("__");
        if (pos != std::string::npos) {
            std::string field = key.substr(0, pos);
            std::string op = key.substr(pos + 2);
            
            if (op == "contains" || op == "like") {
                filters.contains[field] = value;
            } else if (op == "gt") {
                try {
                    filters.greater_than[field] = std::stod(value);
                } catch (...) {}
            } else if (op == "lt") {
                try {
                    filters.less_than[field] = std::stod(value);
                } catch (...) {}
            } else if (op == "gte") {
                try {
                    filters.greater_equal[field] = std::stod(value);
                } catch (...) {}
            } else if (op == "lte") {
                try {
                    filters.less_equal[field] = std::stod(value);
                } catch (...) {}
            } else if (op == "in") {
                // Parse comma-separated list
                std::istringstream iss(value);
                std::string item;
                while (std::getline(iss, item, ',')) {
                    filters.in_list.push_back(item);
                }
            }
        } else {
            // Simple equality
            filters.equals[key] = value;
        }
    }
    
    return filters;
}

/**
 * @brief Apply pagination to a vector of items
 */
template<typename T>
PaginatedResponse<T> paginate(
    std::vector<T> items,
    const PaginationParams& params
) {
    PaginatedResponse<T> response;
    
    int total = static_cast<int>(items.size());
    response.pagination = PaginationMeta(params.page, params.limit, total);
    
    // Apply offset and limit
    int offset = params.offset();
    if (offset >= total) {
        return response;  // Empty page
    }
    
    auto begin = items.begin() + offset;
    auto end = (offset + params.limit >= total) ? items.end() : begin + params.limit;
    
    response.data.assign(begin, end);
    
    return response;
}

/**
 * @brief Apply sorting to a vector of items
 */
template<typename T, typename KeyExtractor>
void apply_sort(
    std::vector<T>& items,
    KeyExtractor key_fn,
    bool ascending = false
) {
    if (ascending) {
        std::sort(items.begin(), items.end(),
            [&](const T& a, const T& b) { return key_fn(a) < key_fn(b); });
    } else {
        std::sort(items.begin(), items.end(),
            [&](const T& a, const T& b) { return key_fn(a) > key_fn(b); });
    }
}

/**
 * @brief Apply filtering to a vector of items
 */
template<typename T, typename Predicate>
void apply_filter(
    std::vector<T>& items,
    Predicate pred
) {
    items.erase(
        std::remove_if(items.begin(), items.end(),
            [&](const T& item) { return !pred(item); }),
        items.end()
    );
}

/**
 * @brief URL decode a string
 */
inline std::string url_decode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.size());
    
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            int hex = 0;
            std::istringstream iss(encoded.substr(i + 1, 2));
            if (iss >> std::hex >> hex) {
                result += static_cast<char>(hex);
                i += 2;
            } else {
                result += encoded[i];
            }
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    
    return result;
}

/**
 * @brief Parse query string into map
 */
inline std::map<std::string, std::string> parse_query_string(
    const std::string& query
) {
    std::map<std::string, std::string> params;
    
    std::string q = query;
    // Remove leading '?'
    if (!q.empty() && q[0] == '?') {
        q = q.substr(1);
    }
    
    std::istringstream iss(q);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq));
            std::string value = url_decode(pair.substr(eq + 1));
            params[key] = value;
        } else {
            params[url_decode(pair)] = "";
        }
    }
    
    return params;
}

} // namespace genie::net

#endif // GENIE_NET_API_PAGINATION_HPP
