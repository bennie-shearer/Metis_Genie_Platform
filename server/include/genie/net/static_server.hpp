/**
 * @file static_server.hpp
 * @brief Static File Server - Serve client HTML/JS/CSS directly from C++20 server
 * @version 5.3.1
 *
 * Enables the Metis Genie Platform server to serve the HTML5 client directly,
 * eliminating the need for a separate web server.
 *
 * @note Cross-platform: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (AppleClang)
 *
 * Usage:
 * @code
 *   StaticFileServer static_srv("./web");
 *   // In route setup:
 *   api.get("/app/wildcard", [&](auto& req, auto& res) {
 *       static_srv.serve(req.path, res);
 *   });
 * @endcode
 *
 * Copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <chrono>

namespace genie::net {

/**
 * @brief Serves static files from a directory with MIME type detection and caching
 */
class StaticFileServer {
public:
    struct CacheEntry {
        std::string content;
        std::string mime_type;
        std::chrono::steady_clock::time_point loaded_at;
    };

    explicit StaticFileServer(const std::string& root_dir, 
                               size_t max_cache_mb = 50,
                               int cache_ttl_sec = 300)
        : root_dir_(root_dir)
        , max_cache_bytes_(max_cache_mb * 1024 * 1024)
        , cache_ttl_sec_(cache_ttl_sec) {}

    /**
     * @brief Serve a file based on URL path
     * @param url_path Request path (e.g., "/index.html")
     * @param body Output: file content
     * @param content_type Output: MIME type
     * @return HTTP status code (200 or 404)
     */
    int serve(const std::string& url_path, std::string& body, std::string& content_type) {
        std::string safe_path = sanitize_path(url_path);
        if (safe_path.empty()) safe_path = "index.html";

        // Check cache first
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(safe_path);
            if (it != cache_.end()) {
                auto age = std::chrono::steady_clock::now() - it->second.loaded_at;
                if (std::chrono::duration_cast<std::chrono::seconds>(age).count() < cache_ttl_sec_) {
                    body = it->second.content;
                    content_type = it->second.mime_type;
                    ++cache_hits_;
                    return 200;
                }
                cache_.erase(it);
            }
        }

        // Read from disk
        std::string full_path = root_dir_ + "/" + safe_path;
        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            body = "{\"error\":\"File not found\"}";
            content_type = "application/json";
            return 404;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        body = ss.str();
        content_type = mime_type_for(safe_path);

        // Cache if within limits
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            if (cache_bytes_ + body.size() <= max_cache_bytes_) {
                cache_[safe_path] = {body, content_type, std::chrono::steady_clock::now()};
                cache_bytes_ += body.size();
            }
        }

        ++files_served_;
        return 200;
    }

    /** @brief Get cache statistics */
    struct Stats {
        size_t files_served = 0;
        size_t cache_hits = 0;
        size_t cache_entries = 0;
        size_t cache_bytes = 0;
    };

    [[nodiscard]] Stats stats() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return {files_served_, cache_hits_, cache_.size(), cache_bytes_};
    }

    /** @brief Clear the file cache */
    void clear_cache() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.clear();
        cache_bytes_ = 0;
    }

private:
    std::string root_dir_;
    size_t max_cache_bytes_;
    int cache_ttl_sec_;

    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
    size_t cache_bytes_ = 0;
    size_t files_served_ = 0;
    size_t cache_hits_ = 0;

    /** @brief Sanitize path to prevent directory traversal */
    static std::string sanitize_path(const std::string& path) {
        std::string result;
        size_t start = 0;
        // Strip leading slash
        if (!path.empty() && path[0] == '/') start = 1;
        // Strip /app/ prefix if present
        if (path.find("/app/") == 0) start = 5;

        for (size_t i = start; i < path.size(); ++i) {
            char c = path[i];
            // Block directory traversal
            if (c == '.' && i + 1 < path.size() && path[i + 1] == '.') {
                return "";  // Reject
            }
            if (c == '\\') c = '/';  // Normalize
            result += c;
        }
        return result;
    }

    /** @brief Determine MIME type from file extension */
    static std::string mime_type_for(const std::string& path) {
        static const std::unordered_map<std::string, std::string> types = {
            {".html", "text/html"},
            {".htm",  "text/html"},
            {".css",  "text/css"},
            {".js",   "application/javascript"},
            {".json", "application/json"},
            {".png",  "image/png"},
            {".jpg",  "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif",  "image/gif"},
            {".svg",  "image/svg+xml"},
            {".ico",  "image/x-icon"},
            {".woff", "font/woff"},
            {".woff2","font/woff2"},
            {".ttf",  "font/ttf"},
            {".pdf",  "application/pdf"},
            {".csv",  "text/csv"},
            {".xml",  "application/xml"},
            {".txt",  "text/plain"},
            {".md",   "text/markdown"},
        };

        auto dot = path.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = path.substr(dot);
            auto it = types.find(ext);
            if (it != types.end()) return it->second;
        }
        return "application/octet-stream";
    }
};

}  // namespace genie::net
