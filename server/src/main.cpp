/**
 * @file main.cpp
 * @brief Metis Genie Platform Investment Management Platform - Production Entry Point
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Configuration: All settings in config.pson (see config.pson.template)
 * If config.pson is missing, it is created from config.pson.template.
 * Logging: As configured in config.pson (logging.directory, logging.filename)
 *
 * Build (CMake - recommended):
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_BUILD_TYPE=Release
 *   cmake --build . --parallel
 *
 * Build (g++ direct):
 *   g++ -std=c++20 -O2 -I include -o metis-genie-platform src/main.cpp \
 *       third_party/sqlite3/sqlite3.c -pthread -lsqlite3
 *   Windows (MinGW): add -lws2_32 -lwinhttp -lcrypt32 -Wa,-mbig-obj
 *
 * Usage:
 *   ./metis-genie-platform              Use mode from config.pson
 *   ./metis-genie-platform --serve      Start HTTP server (port from config.pson)
 *   ./metis-genie-platform --serve 3000 Start HTTP server on port 3000
 *   ./metis-genie-platform --interactive Start interactive CLI mode
 *   ./metis-genie-platform --config X   Use config file X (default: config.pson)
 *   ./metis-genie-platform --version    Print version and exit
 *   ./metis-genie-platform --status     Print system status and exit
 *   ./metis-genie-platform --selftest   Run self-test and exit
 *   ./metis-genie-platform --check-config  Validate config.pson and exit
 */

#include "../include/genie/genie.hpp"
#include "../include/genie/net/http_server.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <chrono>

// Cross-platform directory utilities via <filesystem>
// Note: Requires GCC 8+ / MinGW-w64 8+ (same as http_server.hpp)
#include <filesystem>
#ifdef _WIN32
  #include <direct.h>
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace {
    inline void ensure_dir(const std::string& path) {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }
    inline bool dir_exists(const std::string& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }
}

using namespace genie;

// =========================================================================
// Signal Handling
// =========================================================================

static std::atomic<bool> g_shutdown_requested{false};
static net::HttpServer* g_server_ptr = nullptr;

static void signal_handler(int sig) {
    (void)sig;  // Signal type unused - we handle all signals the same way
    // Async-signal-safe: only set atomic flag
    g_shutdown_requested.store(true, std::memory_order_relaxed);
    if (g_server_ptr) {
        g_server_ptr->request_stop();
    }
    // Write to stderr is technically not async-signal-safe on all platforms,
    // but is widely supported and useful for debugging
    const char msg[] = "\n[SIGNAL] Shutdown requested, finishing current requests...\n";
    [[maybe_unused]] auto r = write(2, msg, sizeof(msg) - 1);
    (void)r;
}

// =========================================================================
// Startup Logger (writes to logs/ directory and console)
// =========================================================================

class StartupLogger {
    std::ofstream file_;
    bool console_enabled_ = true;
    bool file_enabled_ = false;
    bool delegate_ = false;   // once true, route through ::genie::logger()
    std::string log_path_;
    size_t max_file_bytes_{0};
    int max_files_{10};
    size_t current_size_{0};

    std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    void rotate() {
        if (max_file_bytes_ == 0 || log_path_.empty()) return;
        file_.close();
        for (int i = max_files_ - 1; i >= 1; --i) {
            std::string src = log_path_ + "." + std::to_string(i);
            std::string dst = log_path_ + "." + std::to_string(i + 1);
            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
        }
        { std::error_code ec; std::filesystem::rename(log_path_, log_path_ + ".1", ec); }
        file_.open(log_path_, std::ios::app);
        current_size_ = 0;
    }

public:
    void configure(bool enabled, bool console, bool file,
                   const std::string& dir, const std::string& filename,
                   int max_file_size_mb = 0, int max_files = 10) {
        console_enabled_ = enabled && console;
        file_enabled_ = enabled && file;
        max_file_bytes_ = (max_file_size_mb > 0)
            ? static_cast<size_t>(max_file_size_mb) * 1024UL * 1024UL : 0;
        max_files_ = std::max(1, max_files);
        if (file_enabled_) {
            if (!dir_exists(dir)) {
                ensure_dir(dir);
            }
            log_path_ = dir + "/" + filename;
            file_.open(log_path_, std::ios::app);
            if (!file_.is_open()) {
                std::cerr << "[WARN] Cannot open log file: " << log_path_ << std::endl;
                file_enabled_ = false;
            } else {
                file_.seekp(0, std::ios::end);
                current_size_ = static_cast<size_t>(file_.tellp());
            }
        }
    }

    void log(const std::string& level, const std::string& msg) {
        if (delegate_) {
            if      (level == "DEBUG") ::genie::logger().log(::genie::LogLevel::DEBUG, "Main", msg);
            else if (level == "WARN")  ::genie::logger().log(::genie::LogLevel::WARN,  "Main", msg);
            else if (level == "ERROR") ::genie::logger().log(::genie::LogLevel::ERROR, "Main", msg);
            else                       ::genie::logger().log(::genie::LogLevel::INFO,  "Main", msg);
            return;
        }
        std::string ts = timestamp();
        std::string line = "[" + ts + "] [" + level + "] " + msg;
        if (console_enabled_) {
            std::cout << line << std::endl;
        }
        if (file_enabled_ && file_.is_open()) {
            file_ << line << std::endl;
            current_size_ += line.size() + 1;
            if (max_file_bytes_ > 0 && current_size_ >= max_file_bytes_) {
                rotate();
            }
        }
    }

    void debug(const std::string& msg) { log("DEBUG", msg); }
    void info(const std::string& msg) { log("INFO", msg); }
    void warn(const std::string& msg) { log("WARN", msg); }
    void error(const std::string& msg) { log("ERROR", msg); }

    /** Switch all subsequent calls through ::genie::logger() for unified formatting */
    void delegate_to_main_logger() { delegate_ = true; }

    std::string log_path() const {
        if (log_path_.empty()) return log_path_;
        std::error_code ec;
        auto abs = std::filesystem::absolute(log_path_, ec);
        return ec ? log_path_ : abs.string();
    }
};

static StartupLogger& SLOG() {
    static StartupLogger instance;
    return instance;
}

// =========================================================================
// Configuration: All parameters are in config.pson (see config.pson.template)
// If config.pson is missing, it is created from config.pson.template.
// =========================================================================

// =========================================================================
// Application
// =========================================================================

class Application {
    GenieSystem system_;
    net::RestApi api_;
    std::string bearer_token_;
    std::string config_file_ = "config.pson";
    std::chrono::steady_clock::time_point start_time_;

    void load_config() {
        SLOG().info("Loading configuration from: " + config_file_);

        // Create config from template if it doesn't exist
        {
            std::ifstream test(config_file_);
            if (!test.good()) {
                std::ifstream tmpl("config.pson.template");
                if (tmpl.good()) {
                    std::ofstream out(config_file_);
                    out << tmpl.rdbuf();
                    out.close();
                    SLOG().info("Created config.pson from config.pson.template");
                } else {
                    SLOG().warn("config.pson not found and no config.pson.template available.");
                    SLOG().warn("Please create config.pson from config.pson.template.");
                    throw std::runtime_error("config.pson is required but not found");
                }
            }
        }

        config().load_from_file(config_file_);
        SLOG().info("Configuration loaded: " + std::to_string(config().size()) + " settings");

        // Load API keys from config.pson into credentials vault
        try {
            int providers = genie::core::load_api_keys(config_file_);
            SLOG().info("API keys loaded: " + std::to_string(providers) + " providers configured");
        } catch (const std::exception& e) {
            SLOG().warn("API key loading failed: " + std::string(e.what()));
        }

        // Configure startup logger from config
        bool log_enabled = config().require<bool>("logging.enabled");
        bool log_console = config().require<bool>("logging.console");
        bool log_file = config().require<bool>("logging.file");
        std::string log_dir = config().require<std::string>("logging.directory");
        std::string log_filename = config().require<std::string>("logging.filename");
        int log_max_size_mb = config().get<int>("logging.max_file_size_mb").value_or(50);
        int log_max_files = config().get<int>("logging.max_files").value_or(10);

        SLOG().configure(log_enabled, log_console, log_file, log_dir, log_filename,
                         log_max_size_mb, log_max_files);
        SLOG().info("Logging configured: console=" + std::string(log_console ? "yes" : "no") +
                    ", file=" + std::string(log_file ? "yes" : "no") +
                    ", rotation=" + std::to_string(log_max_size_mb) + "MB x " +
                    std::to_string(log_max_files));
        if (log_file) {
            SLOG().info("Log file: " + SLOG().log_path());
        }

        // Configure main logger
        if (config().require<std::string>("logging.level") == "DEBUG") {
            logger().set_level(LogLevel::DEBUG);
        }
        if (log_file) {
            logger().add_file(SLOG().log_path(), log_max_size_mb, log_max_files);
        }
        // All subsequent SLOG() calls now route through ::genie::logger()
        // for consistent formatting with subsystem log lines
        SLOG().delegate_to_main_logger();
    }

    void print_banner() {
        std::cout << R"(
  __  __      _   _        ____            _
 |  \/  | ___| |_(_)___   / ___| ___ _ __ (_) ___
 | |\/| |/ _ \ __| / __| | |  _ / _ \ '_ \| |/ _ \
 | |  | |  __/ |_| \__ \ | |_| |  __/ | | | |  __/
 |_|  |_|\___|\__|_|___/  \____|\____|_| |_|_|\___|

  Investment Management Platform v)" << genie::VERSION_STRING << R"(
  C++20 | Zero Dependencies | Cross-Platform
)" << std::endl;
    }

    std::string format_status() {
        auto uptime = std::chrono::steady_clock::now() - start_time_;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(uptime).count();
        auto mins = std::chrono::duration_cast<std::chrono::minutes>(uptime).count() % 60;
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(uptime).count() % 60;

        std::ostringstream ss;
        ss << "\n=== System Status ===\n";
        ss << "Version:  " << genie::VERSION_STRING << "\n";
        ss << "Config:   " << config_file_ << "\n";
        ss << "Uptime:   " << hours << "h " << mins << "m " << secs << "s\n";
        ss << "Platform: ";
#ifdef _WIN32
        ss << "Windows";
#elif __APPLE__
        ss << "macOS";
#else
        ss << "Linux";
#endif
        ss << "\nCompiler: ";
#if defined(__clang__)
        ss << "Clang " << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
        ss << "GCC " << __GNUC__ << "." << __GNUC_MINOR__;
#elif defined(_MSC_VER)
        ss << "MSVC " << _MSC_VER;
#else
        ss << "Unknown";
#endif
        ss << "\nC++:      " << __cplusplus << "\n";
        ss << "Routes:   " << api_.route_count() << "\n";
        ss << "System:   " << (system_.is_initialized() ? "Ready" : "Not initialized") << "\n";
        ss << "Log file: " << SLOG().log_path() << "\n";

        // API provider status
        int configured = 0;
        for (const auto& p : {"alpha_vantage", "alpaca", "iex_cloud", "finnhub",
                               "polygon", "fred", "tradier", "ibkr", "tda", "webull"}) {
            if (genie::core::is_provider_configured(p)) ++configured;
        }
        ss << "API keys: " << configured << " provider(s) configured\n";
        return ss.str();
    }

    void print_help() {
        std::cout << "\nCommands:\n"
                  << "  help             Show this help\n"
                  << "  version          Show version\n"
                  << "  status           System status\n"
                  << "  config           Show configuration\n"
                  << "  providers        List API provider status\n"
                  << "  routes           List all REST API routes\n"
                  << "  rest <M> <P>     REST request (e.g., rest GET /api/v1/health)\n"
                  << "  login <u> <p>    Login (e.g., login admin demo)\n"
                  << "  portfolios       List portfolios\n"
                  << "  positions        List positions\n"
                  << "  risk             Risk metrics\n"
                  << "  market           Market data\n"
                  << "  orders           List orders\n"
                  << "  analytics        Performance analytics\n"
                  << "  tax              Tax lot summary\n"
                  << "  health           Health check\n"
                  << "  logout           End session\n"
                  << "  exit             Quit\n\n";
    }

    void cmd_rest(const std::string& method, const std::string& path,
                  const std::string& body = "") {
        std::map<std::string, std::string> headers;
        if (!bearer_token_.empty()) {
            headers["Authorization"] = "Bearer " + bearer_token_;
        }
        auto res = api_.handle(method, path, body, headers);
        std::cout << "HTTP " << res.status << "\n" << res.body << "\n";
    }

    void cmd_login(const std::string& user, const std::string& pass) {
        std::string body = R"({"username":")" + user + R"(","password":")" + pass + R"("})";
        auto res = api_.handle("POST", "/api/v1/auth/login", body, {});
        std::cout << "HTTP " << res.status << "\n" << res.body << "\n";
        if (res.status == 200) {
            auto pos = res.body.find("\"token\":\"");
            if (pos != std::string::npos) {
                auto start = pos + 9;
                auto end = res.body.find("\"", start);
                bearer_token_ = res.body.substr(start, end - start);
                std::cout << "Logged in. Token stored.\n";
            }
        }
    }

    void cmd_providers() {
        std::cout << "\n=== API Provider Status ===\n";
        const char* providers[] = {
            "alpha_vantage", "yahoo_finance", "alpaca", "iex_cloud", "finnhub",
            "polygon", "fred", "sec_edgar", "ibkr", "tda", "tradier", "webull",
            "twilio", "sendgrid", "smtp", "slack"
        };
        for (const auto& p : providers) {
            bool configured = genie::core::is_provider_configured(p);
            std::cout << "  " << std::setw(16) << std::left << p
                      << (configured ? " [CONFIGURED]" : " [not set]") << "\n";
        }
        std::cout << "\nEdit " << config_file_ << " to add API keys.\n";
    }

    void interactive_loop() {
        print_banner();
        std::cout << "Type 'help' for commands, 'exit' to quit.\n\n";

        std::string line;
        while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
            std::cout << "genie> ";
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "exit" || cmd == "quit") break;
            else if (cmd == "help") print_help();
            else if (cmd == "version") std::cout << "Metis Genie Platform v" << genie::VERSION_STRING << "\n";
            else if (cmd == "status") std::cout << format_status();
            else if (cmd == "config") std::cout << config().to_pson() << "\n";
            else if (cmd == "providers") cmd_providers();
            else if (cmd == "routes") {
                std::cout << "\n=== REST API Routes (" << api_.route_count() << ") ===\n";
                cmd_rest("GET", "/api/v1/health");
            }
            else if (cmd == "rest") {
                std::string method, path, body;
                iss >> method >> path;
                std::getline(iss, body);
                cmd_rest(method, path, body);
            }
            else if (cmd == "login") {
                std::string user, pass;
                iss >> user >> pass;
                if (user.empty() || pass.empty()) {
                    std::cout << "Usage: login <username> <password>\n";
                } else {
                    cmd_login(user, pass);
                }
            }
            else if (cmd == "logout") {
                cmd_rest("POST", "/api/v1/auth/logout");
                bearer_token_.clear();
            }
            else if (cmd == "portfolios") cmd_rest("GET", "/api/v1/portfolios");
            else if (cmd == "positions") cmd_rest("GET", "/api/v1/positions");
            else if (cmd == "risk") cmd_rest("GET", "/api/v1/risk");
            else if (cmd == "market") cmd_rest("GET", "/api/v1/market");
            else if (cmd == "orders") cmd_rest("GET", "/api/v1/orders");
            else if (cmd == "analytics") cmd_rest("GET", "/api/v1/analytics");
            else if (cmd == "tax") cmd_rest("GET", "/api/v1/tax");
            else if (cmd == "health") cmd_rest("GET", "/api/v1/health");
            else std::cout << "Unknown command: " << cmd << ". Type 'help' for commands.\n";
        }
    }

public:
    int run(int argc, char* argv[]) {
        start_time_ = std::chrono::steady_clock::now();
        SLOG().info("=== Metis Genie Platform Starting ===");

        // Change working directory to executable's directory so config.pson,
        // web/, logs/, etc. are found regardless of where the exe is launched.
        {
            std::filesystem::path exe_dir;
            std::error_code ec;

#ifdef _WIN32
            // Windows: use GetModuleFileName for reliable exe path
            char buf[4096];
            DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) {
                exe_dir = std::filesystem::path(buf).parent_path();
            }
#endif
            // Fallback: try argv[0]
            if (exe_dir.empty()) {
                std::filesystem::path p(argv[0]);
                if (p.has_parent_path()) {
                    auto abs = std::filesystem::absolute(p, ec);
                    if (!ec) exe_dir = abs.parent_path();
                }
            }

            if (!exe_dir.empty()) {
                std::filesystem::current_path(exe_dir, ec);
                if (!ec) {
                    SLOG().info("Working directory: " + exe_dir.string());
                } else {
                    SLOG().warn("Could not set working directory: " + exe_dir.string());
                }
            }
        }

        // Install signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);
#endif

        // Parse arguments
        std::string mode = "";
        int port = -1;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--version" || arg == "-v") {
                std::cout << "Metis Genie Platform v" << genie::VERSION_STRING << "\n";
                return 0;
            }
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: metis-genie-platform [options]\n"
                          << "  --serve [port]     Start HTTP server (default: config or 8080)\n"
                          << "  --interactive      Start interactive CLI mode\n"
                          << "  --config <file>    Config file (default: config.pson)\n"
                          << "  --version          Show version\n"
                          << "  --status           Show system status\n"
                          << "  --selftest         Run self-test\n"
                          << "  --check-config     Validate config and exit\n"
                          << "\nDefault mode is set in config.pson (server.mode)\n";
                return 0;
            }
            if (arg == "--config" && i + 1 < argc) {
                config_file_ = argv[++i];
            }
            if (arg == "--serve") {
                mode = "server";
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    try { port = std::stoi(argv[i + 1]); ++i; }
                    catch (...) {}
                }
            }
            if (arg == "--interactive") mode = "interactive";
            if (arg == "--status") mode = "status";
            if (arg == "--selftest") mode = "selftest";
            if (arg == "--check-config") mode = "check-config";
        }

        // Load configuration
        load_config();

        // Use config defaults if not specified on command line
        if (mode.empty()) {
            mode = config().require<std::string>("server.mode");
        }
        if (port == -1) {
            port = config().require<int>("server.port");
        }

        SLOG().info("Mode: " + mode + ", Port: " + std::to_string(port));

        // Check-config mode exits early
        if (mode == "check-config") {
            return cmd_check_config();
        }

        // Initialize system
        SLOG().info("Initializing system...");
        system_.initialize();
        SLOG().info("System initialized");

        // ---- Wire config.pson values into modules ----

        // debug.startup_logging: control verbose startup output
        bool startup_logging = config().get<bool>("debug.startup_logging").value_or(true);

        // debug.sql_logging: enable SQL statement tracing on all connections
        bool sql_logging = config().get<bool>("debug.sql_logging").value_or(false);
        api_.set_sql_logging(sql_logging);
        if (sql_logging) {
            SLOG().info("SQL statement tracing enabled via debug.sql_logging");
        }

        // application.* : configure application identity in API endpoints
        std::string app_name = config().get<std::string>("application.name").value_or("Metis Genie Platform");
        std::string app_version = config().get<std::string>("application.version").value_or(std::string(genie::VERSION_STRING));
        std::string app_logo = config().get<std::string>("application.logo").value_or("img/logo.svg");
        std::string app_icon = config().get<std::string>("application.icon").value_or("img/icon.svg");
        api_.set_app_info(app_name, app_version, app_logo, app_icon);
        if (startup_logging) {
            SLOG().info("Application: " + app_name + " v" + app_version);
            SLOG().info("Logo: " + app_logo + ", Icon: " + app_icon);
        }

        // database.* : configure SQLite connection parameters for persistent storage
        std::string db_path = config().get<std::string>("database.path").value_or("metis-genie-platform.db");
        bool db_wal_mode = config().get<bool>("database.wal_mode").value_or(true);
        int db_busy_timeout = config().get<int>("database.busy_timeout_ms").value_or(5000);
        api_.set_database_config(db_path, db_wal_mode, db_busy_timeout);
        if (startup_logging) {
            SLOG().info("Database: " + db_path +
                        ", WAL=" + std::string(db_wal_mode ? "yes" : "no") +
                        ", busy_timeout=" + std::to_string(db_busy_timeout) + "ms");
        }

        // auth.* : configure authentication behavior
        bool auth_required = config().get<bool>("auth.require_auth").value_or(true);
        bool create_demo_users = config().get<bool>("auth.create_demo_users").value_or(true);
        int max_login_attempts = config().get<int>("auth.max_login_attempts").value_or(5);
        int lockout_minutes = config().get<int>("auth.lockout_minutes").value_or(15);
        int session_timeout = config().get<int>("auth.session_timeout_minutes").value_or(480);
        api_.set_auth_required(auth_required);
        api_.set_session_timeout(session_timeout);
        api_.set_create_demo_users(create_demo_users);
        api_.set_demo_credentials(
            config().get<std::string>("auth.demo_admin_user").value_or("admin"),
            config().get<std::string>("auth.demo_admin_pass").value_or("demo"),
            config().get<std::string>("auth.demo_trader_user").value_or("trader"),
            config().get<std::string>("auth.demo_trader_pass").value_or("trade"),
            config().get<std::string>("auth.demo_analyst_user").value_or("user"),
            config().get<std::string>("auth.demo_analyst_pass").value_or("user")
        );
        api_.set_login_limits(max_login_attempts, lockout_minutes);
        if (startup_logging) {
            SLOG().info("Auth: required=" + std::string(auth_required ? "yes" : "no") +
                        ", demo_users=" + std::string(create_demo_users ? "yes" : "no") +
                        ", max_attempts=" + std::to_string(max_login_attempts) +
                        ", lockout=" + std::to_string(lockout_minutes) + "min");
        }

        // api.* : rate limiting and pagination
        int rate_limit_requests = config().get<int>("api.rate_limit_requests").value_or(100);
        int rate_limit_window = config().get<int>("api.rate_limit_window_seconds").value_or(60);
        int pagination_default = config().get<int>("api.pagination_default_limit").value_or(50);
        int pagination_max = config().get<int>("api.pagination_max_limit").value_or(500);
        api_.set_rate_limits(rate_limit_requests, rate_limit_window);
        api_.set_pagination_defaults(pagination_default, pagination_max);
        if (startup_logging) {
            SLOG().info("API: rate_limit=" + std::to_string(rate_limit_requests) +
                        "/" + std::to_string(rate_limit_window) + "s" +
                        ", pagination=" + std::to_string(pagination_default) +
                        "/" + std::to_string(pagination_max));
        }

        // server.* : additional server tuning (applied to HttpServer in cmd_serve)
        int max_body_bytes = config().get<int>("server.max_request_body_bytes").value_or(1048576);
        int worker_threads = config().get<int>("server.worker_threads").value_or(4);
        if (startup_logging) {
            SLOG().info("Server: max_body=" + std::to_string(max_body_bytes) +
                        " bytes, workers=" + std::to_string(worker_threads));
        }

        // storage.prices_db : configure prices database path
        std::string prices_db = config().get<std::string>("storage.prices_db").value_or("prices.db");
        api_.set_prices_db(prices_db);
        if (startup_logging) {
            SLOG().info("Prices DB: " + prices_db);
        }

        // backup.* : configure backup manager parameters
        bool backup_enabled = config().get<bool>("backup.enabled").value_or(true);
        std::string backup_dir = config().get<std::string>("backup.directory").value_or("backups");
        int backup_interval = config().get<int>("backup.interval_hours").value_or(24);
        int backup_retention = config().get<int>("backup.retention_days").value_or(30);
        api_.set_backup_config(backup_enabled, backup_dir, backup_interval, backup_retention);
        if (startup_logging) {
            SLOG().info("Backup: enabled=" + std::string(backup_enabled ? "yes" : "no") +
                        ", dir=" + backup_dir +
                        ", interval=" + std::to_string(backup_interval) + "h" +
                        ", retention=" + std::to_string(backup_retention) + "d");
        }

        // health.* : configure health monitoring thresholds
        int health_interval = config().get<int>("health.check_interval_seconds").value_or(60);
        int health_disk_pct = config().get<int>("health.disk_warning_pct").value_or(85);
        int health_mem_pct = config().get<int>("health.memory_warning_pct").value_or(90);
        api_.set_health_config(health_interval, health_disk_pct, health_mem_pct);
        if (startup_logging) {
            SLOG().info("Health: interval=" + std::to_string(health_interval) + "s" +
                        ", disk_warn=" + std::to_string(health_disk_pct) + "%" +
                        ", mem_warn=" + std::to_string(health_mem_pct) + "%");
        }

        // Enable persistent storage
        bool use_persistent = config().require<bool>("storage.persistent");
        if (use_persistent) {
            SLOG().info("Enabling persistent storage...");
            std::string log_directory = config().require<std::string>("logging.directory");
            if (!dir_exists(log_directory)) ensure_dir(log_directory);

            std::string users_db = config().require<std::string>("storage.users_db");
            std::string audit_db = config().require<std::string>("storage.audit_db");

            if (api_.enable_persistent_storage(users_db, audit_db, log_directory + "/audit.log")) {
                SLOG().info("Persistent storage enabled (" + users_db + ", " + audit_db + ")");
            } else {
                SLOG().warn("Persistent storage failed, using in-memory mode");
            }
        }

        // api.cache (v5.3.4)
        bool cache_enabled = config().get<bool>("api.cache_enabled").value_or(true);
        int cache_default_ttl = config().get<int>("api.cache_default_ttl_seconds").value_or(300);
        int cache_max_entries = config().get<int>("api.cache_max_entries").value_or(10000);
        api_.set_cache_enabled(cache_enabled);
        if (cache_enabled) {
            api_.set_cache_default_ttl(cache_default_ttl);
            // Per-endpoint TTLs from cache_ttl.*
            for (const std::string ep : {"health", "portfolios", "positions", "risk",
                    "market", "orders", "analytics", "benchmarks", "compliance",
                    "reporting", "tax", "performance"}) {
                auto ttl = config().get<int>("cache_ttl." + ep);
                if (ttl.has_value())
                    api_.set_cache_ttl("/api/v1/" + ep, ttl.value());
            }
            if (startup_logging)
                SLOG().info("Cache: enabled, default_ttl=" + std::to_string(cache_default_ttl) +
                            "s, max_entries=" + std::to_string(cache_max_entries));
        }

        // api.validation_* : configure request validation (v5.3.1)
        bool validation_enabled = config().get<bool>("api.validation_enabled").value_or(true);
        int validation_max_body = config().get<int>("api.validation_max_body_bytes").value_or(1048576);
        api_.set_validation_enabled(validation_enabled);
        api_.set_validation_max_body(static_cast<size_t>(validation_max_body));
        if (startup_logging)
            SLOG().info("Validation: " + std::string(validation_enabled ? "enabled" : "disabled") +
                        ", max_body=" + std::to_string(validation_max_body) + " bytes");

        // api.graceful_shutdown_* : configure graceful shutdown (v5.3.1)
        bool gs_enabled = config().get<bool>("api.graceful_shutdown_enabled").value_or(true);
        int drain_timeout = config().get<int>("api.graceful_drain_timeout_seconds").value_or(30);
        api_.set_graceful_shutdown_enabled(gs_enabled);
        api_.set_drain_timeout(drain_timeout);
        if (startup_logging)
            SLOG().info("GracefulShutdown: " + std::string(gs_enabled ? "enabled" : "disabled") +
                        ", drain_timeout=" + std::to_string(drain_timeout) + "s");

        // v5.3.4: SSE
        bool sse_enabled = config().get<bool>("sse.enabled").value_or(true);
        api_.set_sse_enabled(sse_enabled);

        // v5.3.4: Prometheus
        bool prom_enabled = config().get<bool>("prometheus.enabled").value_or(true);
        std::string prom_ns = config().get<std::string>("prometheus.namespace").value_or("metis_genie");
        api_.set_prometheus_enabled(prom_enabled);
        api_.set_prometheus_namespace(prom_ns);

        // v5.3.4: File watcher (now with cache invalidation + config reload)
        bool fw_enabled = config().get<bool>("file_watcher.enabled").value_or(true);
        int fw_poll_ms  = config().get<int>("file_watcher.poll_fallback_ms").value_or(2000);
        api_.set_file_watcher_enabled(fw_enabled);
        api_.set_file_watcher_poll_ms(fw_poll_ms);
        if (fw_enabled) {
            // Wire FileWatcher to trigger cache invalidation + config reload
            // when config.pson changes on disk. The watcher runs on its own
            // background thread for the lifetime of cmd_serve().
            static ::genie::core::FileWatcher s_fw(fw_poll_ms);
            s_fw.watch(config_file_, [this](const std::string& changed_path) {
                ::genie::logger().log(::genie::LogLevel::INFO, "Main",
                    "config.pson changed -- reloading config and clearing response cache");
                try {
                    config().load_from_file(changed_path);
                    api_.clear_cache();
                    ::genie::logger().log(::genie::LogLevel::INFO, "Main",
                        "Config reloaded: " + std::to_string(config().size()) + " settings");
                } catch (const std::exception& e) {
                    ::genie::logger().log(::genie::LogLevel::ERROR, "Main",
                        "Config reload failed: " + std::string(e.what()));
                }
            });
            s_fw.start();
        }

        // v5.3.4: Response compression
        bool compress_enabled = config().get<bool>("compression.enabled").value_or(true);
        int compress_min = config().get<int>("compression.min_size_bytes").value_or(1024);
        int compress_level = config().get<int>("compression.level").value_or(6);
        api_.set_compression_enabled(compress_enabled);
        api_.set_compression_min_size(static_cast<size_t>(compress_min));
        api_.set_compression_level(compress_level);
        if (startup_logging)
            SLOG().info("Compression: " + std::string(compress_enabled ? "enabled" : "disabled")
                        + ", min=" + std::to_string(compress_min) + "B"
                        + ", level=" + std::to_string(compress_level));

        // v5.3.4: HTTP/2
        bool h2_enabled = config().get<bool>("http2.enabled").value_or(false);
        api_.set_http2_enabled(h2_enabled);

        // v5.3.4: Kubernetes
        ::genie::ops::K8sConfig k8s_cfg;
        k8s_cfg.enabled         = config().get<bool>("kubernetes.enabled").value_or(false);
        k8s_cfg.api_url         = config().get<std::string>("kubernetes.api_url").value_or("");
        k8s_cfg.namespace_name  = config().get<std::string>("kubernetes.namespace").value_or("default");
        k8s_cfg.token           = config().get<std::string>("kubernetes.service_account_token").value_or("");
        k8s_cfg.deployment_name = config().get<std::string>("kubernetes.deployment_name").value_or("metis-genie-platform");
        k8s_cfg.replica_count   = config().get<int>("kubernetes.replica_count").value_or(1);
        api_.set_k8s_config(k8s_cfg);

        // v5.3.4: FIX engine v2
        ::genie::trading::fix::FixConfig fix_cfg;
        fix_cfg.enabled          = config().get<bool>("fix.enabled").value_or(false);
        fix_cfg.version          = config().get<std::string>("fix.version").value_or("FIX.4.4");
        fix_cfg.sender_comp_id   = config().get<std::string>("fix.sender_comp_id").value_or("METIS");
        fix_cfg.target_comp_id   = config().get<std::string>("fix.target_comp_id").value_or("BROKER");
        fix_cfg.host             = config().get<std::string>("fix.host").value_or("");
        fix_cfg.port             = config().get<int>("fix.port").value_or(9876);
        fix_cfg.heartbeat_interval = config().get<int>("fix.heartbeat_interval").value_or(30);
        api_.set_fix_v2_config(fix_cfg);

        // v5.3.4: WASM
        ::genie::net::wasm::WasmConfig wasm_cfg;
        wasm_cfg.enabled           = config().get<bool>("wasm.enabled").value_or(false);
        wasm_cfg.serve_wasm        = config().get<bool>("wasm.serve_wasm").value_or(true);
        wasm_cfg.coop_coep_headers = config().get<bool>("wasm.coop_coep_headers").value_or(true);
        api_.set_wasm_config(wasm_cfg);

        SLOG().info("Configuring API...");
        api_.configure_defaults();
        SLOG().info("API configured with " + std::to_string(api_.route_count()) + " routes");

        // Execute mode
        if (mode == "server") {
            return cmd_serve(port);
        } else if (mode == "status") {
            return cmd_status();
        } else if (mode == "selftest") {
            return cmd_selftest();
        } else {
            interactive_loop();
            system_.shutdown();
            return 0;
        }
    }

    int cmd_serve(int port) {
        print_banner();

        std::string host = config().require<std::string>("server.host");
        std::cout << "Starting HTTP server on " << host << ":" << port << "...\n\n";

        std::cout << "Routes: " << api_.route_count() << "\n";
        std::cout << "  Health:      GET  /api/v1/health\n";
        std::cout << "  Status:      GET  /api/v1/status\n";
        std::cout << "  Login:       POST /api/v1/auth/login\n";
        std::cout << "  Portfolios:  GET  /api/v1/portfolios\n";
        std::cout << "  Positions:   GET  /api/v1/positions\n";
        std::cout << "  Risk:        GET  /api/v1/risk\n";
        std::cout << "  Market:      GET  /api/v1/market\n";
        std::cout << "  Orders:      GET  /api/v1/orders\n";
        std::cout << "  Analytics:   GET  /api/v1/analytics\n";
        std::cout << "  Tax:         GET  /api/v1/tax\n";
        std::cout << "  Security:    GET  /api/v1/security/*\n";
        std::cout << "  Operations:  GET  /api/v1/operations/*\n\n";
        std::cout << "Default credentials: admin/demo, trader/trade, user/user\n";
        std::cout << "Config: " << config_file_ << "\n";
        std::cout << "Log: " << SLOG().log_path() << "\n";
        std::cout << "Press Ctrl+C to stop.\n\n";

        SLOG().info("Creating HttpServer on port " + std::to_string(port));
        net::HttpServer server(api_, port);
        g_server_ptr = &server;

        // Wire shutdown callback so POST /api/v1/ops/shutdown actually stops the server
        api_.set_shutdown_callback([&]() {
            SLOG().info("Shutdown requested via API");
            g_shutdown_requested.store(true, std::memory_order_relaxed);
            if (g_server_ptr) g_server_ptr->request_stop();
        });

        // Server tuning from config.pson
        int max_body = config().get<int>("server.max_request_body_bytes").value_or(1048576);
        int workers = config().get<int>("server.worker_threads").value_or(4);
        server.set_max_body_size(static_cast<size_t>(max_body));
        server.set_worker_count(workers);

        // CORS
        std::string cors = config().require<std::string>("server.cors_origin");
        server.set_cors_origin(cors);

        // Static file serving
        std::string static_dir = config().require<std::string>("server.static_dir");
        if (dir_exists(static_dir)) {
            server.set_static_dir(static_dir);
            SLOG().info("Static files: " + static_dir);
        } else {
            SLOG().warn("Static dir not found: " + static_dir
                + " -- run CMake build to sync client files, or set server.static_dir in config.pson");
        }

        // Request logging
        bool request_log = config().require<bool>("debug.request_logging");
        if (request_log) {
            server.set_access_logging(true);
        }

        // Wire live HttpServer stats into Prometheus /metrics
        // The lambda captures &server by reference -- safe since server outlives api_.
        api_.set_stats_provider([&server](
            int64_t& tot, int64_t& conn, int64_t& bs,
            int64_t& br, int64_t& e4, int64_t& e5, double& up) {
            auto s = server.stats();
            tot  = s.total_requests;
            conn = s.active_connections;
            bs   = s.bytes_sent;
            br   = s.bytes_received;
            e4   = s.errors_4xx;
            e5   = s.errors_5xx;
            up   = static_cast<double>(s.uptime_ms) / 1000.0;
        });

        // Wire true SSE streaming -- SseChannel takes over /api/v1/stream/<channel>
        // connections directly, writing events to the raw TCP socket.
        bool sse_enabled = config().get<bool>("sse.enabled").value_or(true);
        int  sse_ka_sec  = config().get<int>("sse.keep_alive_seconds").value_or(15);
        if (sse_enabled) {
            server.set_streaming_handler(::genie::net::make_sse_streaming_handler(sse_ka_sec));
            ::genie::logger().log(::genie::LogLevel::INFO, "Main",
                "SSE streaming enabled (keep-alive=" + std::to_string(sse_ka_sec) + "s)");
        }

        SLOG().info("Starting HttpServer...");
        bool ok = server.start();

        g_server_ptr = nullptr;
        SLOG().info("HttpServer stopped: " + std::string(ok ? "clean" : "error"));

        system_.shutdown();
        SLOG().info("System shut down. Goodbye.");
        return ok ? 0 : 1;
    }

    int cmd_status() {
        if (!system_.is_initialized()) {
            system_.initialize();
            api_.configure_defaults();
        }
        std::cout << format_status();
        system_.shutdown();
        return 0;
    }

    int cmd_check_config() {
        std::cout << "=== Config Validation ===\n";
        std::cout << "File: " << config_file_ << "\n\n";

        int errors = 0;
        auto check = [&](const std::string& /*key*/, const std::string& desc, bool cond) {
            if (cond) {
                std::cout << "  [OK]   " << desc << "\n";
            } else {
                std::cout << "  [WARN] " << desc << "\n";
                errors++;
            }
        };

        // Application
        check("application.name", "Application name set",
              config().get<std::string>("application.name").has_value());
        check("application.version", "Application version set",
              config().get<std::string>("application.version").has_value());

        // Server
        check("server.port", "Server port is set",
              config().get<int>("server.port").has_value());
        check("server.worker_threads", "Worker threads configured",
              config().get<int>("server.worker_threads").value_or(4) > 0);
        check("server.max_request_body_bytes", "Max request body configured",
              config().get<int>("server.max_request_body_bytes").value_or(1048576) > 0);

        // Logging
        check("logging.directory", "Log directory configured",
              !config().require<std::string>("logging.directory").empty());
        check("logging.max_file_size_mb", "Log rotation size configured",
              config().get<int>("logging.max_file_size_mb").value_or(50) > 0);
        check("logging.max_files", "Log rotation count configured",
              config().get<int>("logging.max_files").value_or(10) > 0);

        // Database
        check("database.path", "Database path configured",
              config().get<std::string>("database.path").has_value());
        check("database.busy_timeout_ms", "Database busy timeout set",
              config().get<int>("database.busy_timeout_ms").value_or(5000) > 0);

        // Auth
        check("auth.session_timeout_minutes", "Session timeout set",
              config().require<int>("auth.session_timeout_minutes") > 0);
        check("auth.max_login_attempts", "Login attempt limit set",
              config().get<int>("auth.max_login_attempts").value_or(5) > 0);
        check("auth.lockout_minutes", "Lockout duration set",
              config().get<int>("auth.lockout_minutes").value_or(15) > 0);

        // API
        check("api.rate_limit_requests", "Rate limit configured",
              config().get<int>("api.rate_limit_requests").value_or(100) > 0);
        check("api.pagination_default_limit", "Pagination default set",
              config().get<int>("api.pagination_default_limit").value_or(50) > 0);
        check("api.pagination_max_limit", "Pagination max set",
              config().get<int>("api.pagination_max_limit").value_or(500) > 0);

        // Backup
        check("backup.directory", "Backup directory configured",
              config().get<std::string>("backup.directory").has_value());
        check("backup.retention_days", "Backup retention set",
              config().get<int>("backup.retention_days").value_or(30) > 0);

        // Health
        check("health.check_interval_seconds", "Health check interval set",
              config().get<int>("health.check_interval_seconds").value_or(60) > 0);
        check("health.disk_warning_pct", "Disk warning threshold set",
              config().get<int>("health.disk_warning_pct").value_or(85) > 0);
        check("health.memory_warning_pct", "Memory warning threshold set",
              config().get<int>("health.memory_warning_pct").value_or(90) > 0);

        // Storage
        check("storage.prices_db", "Prices database configured",
              config().get<std::string>("storage.prices_db").has_value());

        // Check API keys
        std::cout << "\n  API Providers:\n";
        int configured = 0;
        const char* providers[] = {
            "alpha_vantage", "alpaca", "iex_cloud", "finnhub",
            "polygon", "fred", "tradier"
        };
        for (const auto& p : providers) {
            bool has = genie::core::is_provider_configured(p);
            std::cout << "    " << std::setw(16) << std::left << p
                      << (has ? " [OK]" : " [not set]") << "\n";
            if (has) ++configured;
        }

        std::cout << "\n" << configured << " provider(s) configured";
        if (configured == 0) {
            std::cout << " (add API keys to " << config_file_ << " for live data)";
        }
        std::cout << "\n";

        return errors > 0 ? 1 : 0;
    }

    int cmd_selftest() {
        std::cout << "=== Metis Genie Platform v" << genie::VERSION_STRING << " Self-Test ===\n\n";

        if (!system_.is_initialized()) {
            system_.initialize();
            api_.configure_defaults();
        }

        int passed = 0, failed = 0;
        auto test = [&](const std::string& name, bool cond) {
            if (cond) { ++passed; std::cout << "  [PASS] " << name << "\n"; }
            else { ++failed; std::cout << "  [FAIL] " << name << "\n"; }
        };

        // Core
        test("System initialized", system_.is_initialized());
        test("Routes configured (> 0)", api_.route_count() > 0);
        test("Config loaded (> 0 settings)", config().size() > 0);
        test("Version string set", genie::VERSION_MAJOR == 3 && genie::VERSION_MINOR == 3);

        // REST API endpoints
        auto check_endpoint = [&](const std::string& method, const std::string& path, int expected_status) {
            auto res = api_.handle(method, path, "", {});
            test(method + " " + path + " => " + std::to_string(expected_status),
                 res.status == expected_status);
        };

        check_endpoint("GET", "/api/v1/health", 200);
        check_endpoint("GET", "/api/v1/status", 200);

        // Auth flow
        {
            std::string body = R"({"username":"admin","password":"demo"})";
            auto res = api_.handle("POST", "/api/v1/auth/login", body, {});
            test("POST /api/v1/auth/login => 200", res.status == 200);

            if (res.status == 200) {
                auto pos = res.body.find("\"token\":\"");
                std::string token;
                if (pos != std::string::npos) {
                    auto start = pos + 9;
                    auto end = res.body.find("\"", start);
                    token = res.body.substr(start, end - start);
                }
                test("Login returns bearer token", !token.empty());

                if (!token.empty()) {
                    std::map<std::string, std::string> auth_hdr = {
                        {"Authorization", "Bearer " + token}
                    };

                    auto r2 = api_.handle("GET", "/api/v1/portfolios", "", auth_hdr);
                    test("GET /api/v1/portfolios (authed) => 200", r2.status == 200);

                    auto r3 = api_.handle("GET", "/api/v1/positions", "", auth_hdr);
                    test("GET /api/v1/positions (authed) => 200", r3.status == 200);

                    auto r4 = api_.handle("GET", "/api/v1/risk", "", auth_hdr);
                    test("GET /api/v1/risk (authed) => 200", r4.status == 200);

                    auto r5 = api_.handle("GET", "/api/v1/market", "", auth_hdr);
                    test("GET /api/v1/market (authed) => 200", r5.status == 200);

                    auto r6 = api_.handle("GET", "/api/v1/orders", "", auth_hdr);
                    test("GET /api/v1/orders (authed) => 200", r6.status == 200);

                    auto r7 = api_.handle("GET", "/api/v1/analytics", "", auth_hdr);
                    test("GET /api/v1/analytics (authed) => 200", r7.status == 200);

                    // Logout
                    auto r8 = api_.handle("POST", "/api/v1/auth/logout", "", auth_hdr);
                    test("POST /api/v1/auth/logout => 200", r8.status == 200);
                }
            }
        }

        // Unauthenticated access should be rejected (401)
        {
            auto res = api_.handle("GET", "/api/v1/portfolios", "", {});
            test("GET /api/v1/portfolios (no auth) => 401", res.status == 401);
        }

        // Math utilities
        {
            std::vector<double> data = {1, 2, 3, 4, 5};
            test("math::mean([1..5]) = 3.0",
                 std::abs(math::mean(data) - 3.0) < 0.001);
            test("math::stddev([1..5]) > 0",
                 math::stddev(data) > 0);
        }

        // Trading calendar
        {
            const auto& cal = genie::market::trading_calendar();
            test("Christmas 2025 is a holiday", cal.is_holiday("2025-12-25"));
            test("Weekday is a trading day", cal.is_trading_day("2026-02-05"));
        }

        std::cout << "\n=== Results: " << passed << "/" << (passed + failed)
                  << " passed ===\n";

        system_.shutdown();
        return failed > 0 ? 1 : 0;
    }
};

// =========================================================================
// Main entry point
// =========================================================================

int main(int argc, char* argv[]) {
    SLOG().configure(true, true, false, "", "");  // Console-only until config.pson is loaded
    SLOG().info("=== main() entered ===");

    try {
        Application app;
        int result = app.run(argc, argv);
        SLOG().info("=== main() exiting with code " + std::to_string(result) + " ===");
        return result;
    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL] Unhandled exception: " << e.what() << std::endl;
        SLOG().error("FATAL: " + std::string(e.what()));
        return 99;
    } catch (...) {
        std::cerr << "\n[FATAL] Unknown exception\n";
        SLOG().error("FATAL: Unknown exception");
        return 99;
    }
}
