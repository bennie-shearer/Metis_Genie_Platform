/**
 * @file lazy_loading.hpp
 * @brief Lazy loading and connection pooling for APIs
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Performance - Lazy loading for large data, connection pooling for APIs
 */

#ifndef GENIE_PERFORMANCE_LAZY_LOADING_HPP
#define GENIE_PERFORMANCE_LAZY_LOADING_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <optional>

// Forward declare for PooledApiClient
namespace genie::core { class HttpClient; }
#include <queue>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <future>

namespace genie {
namespace performance {

/**
 * @brief Lazy value wrapper
 */
template<typename T>
class Lazy {
public:
    using Loader = std::function<T()>;
    
    explicit Lazy(Loader loader) : loader_(std::move(loader)), loaded_(false) {}
    
    /**
     * @brief Get value (loads on first access)
     */
    const T& get() {
        std::call_once(load_flag_, [this]() {
            value_ = loader_();
            loaded_ = true;
        });
        return value_;
    }
    
    /**
     * @brief Check if loaded
     */
    bool is_loaded() const {
        return loaded_;
    }
    
    /**
     * @brief Force reload
     */
    void reload() {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = loader_();
        loaded_ = true;
    }
    
    /**
     * @brief Reset to unloaded state
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_ = false;
        load_flag_ = std::once_flag();
        value_ = T();
    }

private:
    Loader loader_;
    T value_;
    std::atomic<bool> loaded_;
    std::once_flag load_flag_;
    std::mutex mutex_;
};

/**
 * @brief Paginated data loader
 */
template<typename T>
class PaginatedLoader {
public:
    struct Page {
        std::vector<T> items;
        int page_number{0};
        int page_size{0};
        int total_items{0};
        int total_pages{0};
        bool has_next{false};
        bool has_prev{false};
    };
    
    using PageLoader = std::function<Page(int page, int page_size)>;
    
    explicit PaginatedLoader(PageLoader loader, int default_page_size = 50)
        : loader_(std::move(loader)), page_size_(default_page_size) {}
    
    /**
     * @brief Get page
     */
    Page get_page(int page) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = pages_.find(page);
        if (it != pages_.end()) {
            return it->second;
        }
        
        Page loaded = loader_(page, page_size_);
        pages_[page] = loaded;
        
        return loaded;
    }
    
    /**
     * @brief Prefetch next page
     */
    std::future<Page> prefetch_page(int page) {
        return std::async(std::launch::async, [this, page]() {
            return get_page(page);
        });
    }
    
    /**
     * @brief Get all items (loads all pages)
     */
    std::vector<T> get_all() {
        std::vector<T> all_items;
        
        Page first_page = get_page(0);
        all_items.insert(all_items.end(), first_page.items.begin(), first_page.items.end());
        
        for (int i = 1; i < first_page.total_pages; ++i) {
            Page page = get_page(i);
            all_items.insert(all_items.end(), page.items.begin(), page.items.end());
        }
        
        return all_items;
    }
    
    /**
     * @brief Clear cache
     */
    void clear_cache() {
        std::lock_guard<std::mutex> lock(mutex_);
        pages_.clear();
    }
    
    /**
     * @brief Set page size
     */
    void set_page_size(int size) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size != page_size_) {
            page_size_ = size;
            pages_.clear();
        }
    }

private:
    PageLoader loader_;
    int page_size_;
    std::map<int, Page> pages_;
    std::mutex mutex_;
};

/**
 * @brief Streaming data iterator
 */
template<typename T>
class StreamingIterator {
public:
    using Fetcher = std::function<std::vector<T>(const std::string& cursor, int batch_size)>;
    
    explicit StreamingIterator(Fetcher fetcher, int batch_size = 100)
        : fetcher_(std::move(fetcher)), batch_size_(batch_size),
          current_index_(0), has_more_(true) {}
    
    /**
     * @brief Get next item
     */
    std::optional<T> next() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Need to fetch more?
        if (current_index_ >= buffer_.size()) {
            if (!has_more_) {
                return std::nullopt;
            }
            
            fetch_batch();
            
            if (buffer_.empty()) {
                has_more_ = false;
                return std::nullopt;
            }
        }
        
        return buffer_[current_index_++];
    }
    
    /**
     * @brief Check if more items available
     */
    bool has_next() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_index_ < buffer_.size() || has_more_;
    }
    
    /**
     * @brief Get next batch
     */
    std::vector<T> next_batch(int count) {
        std::vector<T> result;
        result.reserve(count);
        
        for (int i = 0; i < count; ++i) {
            auto item = next();
            if (!item) break;
            result.push_back(*item);
        }
        
        return result;
    }
    
    /**
     * @brief Reset iterator
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
        cursor_.clear();
        current_index_ = 0;
        has_more_ = true;
    }

private:
    Fetcher fetcher_;
    int batch_size_;
    std::vector<T> buffer_;
    std::string cursor_;
    size_t current_index_;
    bool has_more_;
    mutable std::mutex mutex_;
    
    void fetch_batch() {
        buffer_ = fetcher_(cursor_, batch_size_);
        current_index_ = 0;
        
        if (buffer_.size() < static_cast<size_t>(batch_size_)) {
            has_more_ = false;
        }
        
        // Update cursor for next fetch
        if (!buffer_.empty()) {
            // Cursor handling depends on implementation
        }
    }
};

/**
 * @brief HTTP connection pool
 */
class HttpConnectionPool {
public:
    struct Config {
        std::string base_url;
        int max_connections{10};
        int min_connections{2};
        std::chrono::seconds connection_timeout{30};
        std::chrono::seconds idle_timeout{120};
        std::chrono::seconds request_timeout{60};
        bool keep_alive{true};
        int max_requests_per_connection{1000};
    };
    
    struct Connection {
        std::string id;
        std::string host;
        int port{80};
        bool in_use{false};
        int request_count{0};
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_used;
        bool healthy{true};
    };
    
    explicit HttpConnectionPool(const Config& config)
        : config_(config), running_(true) {
        // Parse base URL
        parse_url(config.base_url);
        
        // Create initial connections
        for (int i = 0; i < config.min_connections; ++i) {
            create_connection();
        }
        
        // Start maintenance thread
        maintenance_thread_ = std::thread([this]() { maintenance_loop(); });
    }
    
    ~HttpConnectionPool() {
        running_ = false;
        cv_.notify_all();
        if (maintenance_thread_.joinable()) {
            maintenance_thread_.join();
        }
    }
    
    /**
     * @brief Get connection from pool
     */
    std::shared_ptr<Connection> acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait for available connection
        if (!cv_.wait_for(lock, config_.connection_timeout, [this]() {
            return !running_ || has_available_connection();
        })) {
            throw std::runtime_error("Connection pool timeout");
        }
        
        if (!running_) {
            throw std::runtime_error("Pool is shutting down");
        }
        
        // Find available connection
        for (auto& conn : connections_) {
            if (!conn->in_use && conn->healthy) {
                conn->in_use = true;
                conn->last_used = std::chrono::system_clock::now();
                active_count_++;
                return conn;
            }
        }
        
        // Create new connection if under limit
        if (static_cast<int>(connections_.size()) < config_.max_connections) {
            auto conn = create_connection();
            conn->in_use = true;
            active_count_++;
            return conn;
        }
        
        throw std::runtime_error("No connections available");
    }
    
    /**
     * @brief Release connection back to pool
     */
    void release(std::shared_ptr<Connection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        conn->in_use = false;
        conn->request_count++;
        conn->last_used = std::chrono::system_clock::now();
        active_count_--;
        
        // Check if connection should be retired
        if (conn->request_count >= config_.max_requests_per_connection) {
            conn->healthy = false;
        }
        
        cv_.notify_one();
    }
    
    /**
     * @brief Get pool statistics
     */
    std::map<std::string, int> get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int healthy = 0, unhealthy = 0;
        for (const auto& conn : connections_) {
            if (conn->healthy) healthy++;
            else unhealthy++;
        }
        
        return {
            {"total", static_cast<int>(connections_.size())},
            {"active", active_count_},
            {"available", healthy - active_count_},
            {"healthy", healthy},
            {"unhealthy", unhealthy}
        };
    }
    
    /**
     * @brief Health check
     */
    bool is_healthy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int healthy = 0;
        for (const auto& conn : connections_) {
            if (conn->healthy) healthy++;
        }
        
        return healthy >= config_.min_connections;
    }

private:
    Config config_;
    std::string host_;
    int port_{80};
    bool use_ssl_{false};
    
    std::vector<std::shared_ptr<Connection>> connections_;
    int active_count_{0};
    std::atomic<bool> running_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread maintenance_thread_;
    
    void parse_url(const std::string& url) {
        // Simple URL parsing
        size_t protocol_end = url.find("://");
        size_t host_start = (protocol_end != std::string::npos) ? protocol_end + 3 : 0;
        
        if (url.find("https") != std::string::npos) {
            use_ssl_ = true;
            port_ = 443;
        }
        
        size_t port_start = url.find(':', host_start);
        size_t path_start = url.find('/', host_start);
        
        if (port_start != std::string::npos && port_start < path_start) {
            host_ = url.substr(host_start, port_start - host_start);
            size_t port_end = (path_start != std::string::npos) ? path_start : url.size();
            port_ = std::stoi(url.substr(port_start + 1, port_end - port_start - 1));
        } else {
            size_t host_end = (path_start != std::string::npos) ? path_start : url.size();
            host_ = url.substr(host_start, host_end - host_start);
        }
    }
    
    std::shared_ptr<Connection> create_connection() {
        auto conn = std::make_shared<Connection>();
        conn->id = generate_id();
        conn->host = host_;
        conn->port = port_;
        conn->created_at = std::chrono::system_clock::now();
        conn->last_used = conn->created_at;
        conn->healthy = true;
        
        connections_.push_back(conn);
        return conn;
    }
    
    bool has_available_connection() const {
        for (const auto& conn : connections_) {
            if (!conn->in_use && conn->healthy) {
                return true;
            }
        }
        return static_cast<int>(connections_.size()) < config_.max_connections;
    }
    
    void maintenance_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            if (!running_) break;
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            auto now = std::chrono::system_clock::now();
            
            // Remove unhealthy/idle connections
            connections_.erase(
                std::remove_if(connections_.begin(), connections_.end(),
                    [this, now](const std::shared_ptr<Connection>& conn) {
                        if (conn->in_use) return false;
                        if (!conn->healthy) return true;
                        if (now - conn->last_used > config_.idle_timeout &&
                            static_cast<int>(connections_.size()) > config_.min_connections) {
                            return true;
                        }
                        return false;
                    }),
                connections_.end());
            
            // Ensure minimum connections
            while (static_cast<int>(connections_.size()) < config_.min_connections) {
                create_connection();
            }
        }
    }
    
    static std::string generate_id() {
        static std::atomic<int> counter{0};
        return "conn-" + std::to_string(counter++);
    }
};

/**
 * @brief API client with connection pooling
 */
class PooledApiClient {
public:
    struct Config {
        std::string base_url;
        std::string api_key;
        int max_connections{5};
        std::chrono::seconds timeout{30};
        int max_retries{3};
        std::chrono::milliseconds retry_delay{1000};
    };
    
    explicit PooledApiClient(const Config& config) : config_(config) {
        HttpConnectionPool::Config pool_config;
        pool_config.base_url = config.base_url;
        pool_config.max_connections = config.max_connections;
        pool_config.request_timeout = config.timeout;
        
        pool_ = std::make_unique<HttpConnectionPool>(pool_config);
    }
    
    /**
     * @brief GET request
     */
    std::string get(const std::string& endpoint,
                    const std::map<std::string, std::string>& params = {}) {
        auto conn = pool_->acquire();
        
        try {
            // Build URL with params
            std::string url = config_.base_url + endpoint;
            if (!params.empty()) {
                url += "?";
                bool first = true;
                for (const auto& [k, v] : params) {
                    if (!first) url += "&";
                    url += k + "=" + v;
                    first = false;
                }
            }
            
            // Execute request via HttpClient
            std::string response = execute_request("GET", url, "");
            
            pool_->release(conn);
            return response;
        } catch (...) {
            pool_->release(conn);
            throw;
        }
    }
    
    /**
     * @brief POST request
     */
    std::string post(const std::string& endpoint, const std::string& body) {
        auto conn = pool_->acquire();
        
        try {
            std::string url = config_.base_url + endpoint;
            std::string response = execute_request("POST", url, body);
            
            pool_->release(conn);
            return response;
        } catch (...) {
            pool_->release(conn);
            throw;
        }
    }
    
    /**
     * @brief Check health
     */
    bool is_healthy() const {
        return pool_->is_healthy();
    }
    
    /**
     * @brief Get pool stats
     */
    std::map<std::string, int> get_pool_stats() const {
        return pool_->get_stats();
    }

private:
    Config config_;
    std::unique_ptr<HttpConnectionPool> pool_;
    
    std::string execute_request(const std::string& method,
                                const std::string& url,
                                const std::string& body) {
        genie::core::HttpClient client;
        if (!config_.api_key.empty()) {
            client.set_header("Authorization", "Bearer " + config_.api_key);
        }
        client.set_timeout(static_cast<int>(config_.timeout.count() * 1000));

        genie::core::HttpRequest req;
        req.url = url;
        req.body = body;
        if (method == "POST") {
            req.method = genie::core::HttpMethod::POST;
            req.headers["Content-Type"] = "application/json";
        } else if (method == "PUT") {
            req.method = genie::core::HttpMethod::PUT;
        } else if (method == "DELETE") {
            req.method = genie::core::HttpMethod::DELETE;
        } else {
            req.method = genie::core::HttpMethod::GET;
        }

        auto response = client.execute(req);
        if (!response.ok()) {
            return "{}";
        }
        return response.body;
    }
};

/**
 * @brief Virtual scrolling data provider
 */
template<typename T>
class VirtualScrollProvider {
public:
    using ItemLoader = std::function<std::vector<T>(int start, int count)>;
    using CountLoader = std::function<int()>;
    
    explicit VirtualScrollProvider(ItemLoader loader, CountLoader count_loader,
                                    int buffer_size = 100)
        : loader_(std::move(loader)), count_loader_(std::move(count_loader)),
          buffer_size_(buffer_size), total_count_(-1) {}
    
    /**
     * @brief Get items for visible range
     */
    std::vector<T> get_visible_items(int scroll_position, int visible_count) {
        // Calculate range with buffer
        int start = std::max(0, scroll_position - buffer_size_);
        int end = scroll_position + visible_count + buffer_size_;
        
        if (total_count_ < 0) {
            total_count_ = count_loader_();
        }
        
        end = std::min(end, total_count_);
        
        // Check if we need to load more
        if (start < loaded_start_ || end > loaded_end_) {
            load_range(start, end);
        }
        
        // Extract visible items
        std::vector<T> visible;
        int actual_start = scroll_position - loaded_start_;
        int actual_end = std::min(actual_start + visible_count,
                                  static_cast<int>(loaded_items_.size()));
        
        if (actual_start >= 0 && actual_start < static_cast<int>(loaded_items_.size())) {
            visible.assign(loaded_items_.begin() + actual_start,
                          loaded_items_.begin() + actual_end);
        }
        
        return visible;
    }
    
    /**
     * @brief Get total count
     */
    int get_total_count() {
        if (total_count_ < 0) {
            total_count_ = count_loader_();
        }
        return total_count_;
    }
    
    /**
     * @brief Refresh data
     */
    void refresh() {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_items_.clear();
        loaded_start_ = 0;
        loaded_end_ = 0;
        total_count_ = -1;
    }

private:
    ItemLoader loader_;
    CountLoader count_loader_;
    int buffer_size_;
    int total_count_;
    
    std::vector<T> loaded_items_;
    int loaded_start_{0};
    int loaded_end_{0};
    std::mutex mutex_;
    
    void load_range(int start, int end) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        loaded_items_ = loader_(start, end - start);
        loaded_start_ = start;
        loaded_end_ = end;
    }
};

} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_LAZY_LOADING_HPP
