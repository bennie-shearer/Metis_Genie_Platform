/**
 * @file notifications.hpp
 * @brief Multi-channel notification service
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 4: Notifications
 * - Email notification service
 * - SMS alerts via Twilio
 * - Slack webhook integration
 * - Push notifications
 * - Configurable alert preferences
 */
#pragma once
#ifndef GENIE_CORE_NOTIFICATIONS_HPP
#define GENIE_CORE_NOTIFICATIONS_HPP

#include "http_client.hpp"
#include "smtp_client.hpp"
#include "crypto.hpp"
#include "logging.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace genie::core {

// =============================================================================
// Common Types
// =============================================================================

/**
 * @brief Notification channel types
 */
enum class NotificationChannel {
    Email,
    SMS,
    Slack,
    Push,
    Webhook,
    InApp,
    All
};

inline std::string channel_to_string(NotificationChannel channel) {
    switch (channel) {
        case NotificationChannel::Email: return "email";
        case NotificationChannel::SMS: return "sms";
        case NotificationChannel::Slack: return "slack";
        case NotificationChannel::Push: return "push";
        case NotificationChannel::Webhook: return "webhook";
        case NotificationChannel::InApp: return "in_app";
        case NotificationChannel::All: return "all";
        default: return "unknown";
    }
}

inline NotificationChannel string_to_channel(const std::string& str) {
    if (str == "email") return NotificationChannel::Email;
    if (str == "sms") return NotificationChannel::SMS;
    if (str == "slack") return NotificationChannel::Slack;
    if (str == "push") return NotificationChannel::Push;
    if (str == "webhook") return NotificationChannel::Webhook;
    if (str == "in_app") return NotificationChannel::InApp;
    return NotificationChannel::All;
}

/**
 * @brief Notification priority levels
 */
enum class NotificationPriority {
    Low,
    Normal,
    High,
    Critical
};

inline std::string priority_to_string(NotificationPriority priority) {
    switch (priority) {
        case NotificationPriority::Low: return "low";
        case NotificationPriority::Normal: return "normal";
        case NotificationPriority::High: return "high";
        case NotificationPriority::Critical: return "critical";
        default: return "normal";
    }
}

/**
 * @brief Alert category
 */
enum class AlertCategory {
    Trade,              // Order fills, executions
    Price,              // Price alerts, limit triggers
    Portfolio,          // Rebalancing, drift
    Risk,               // Stop-loss, margin
    Tax,                // Tax-loss harvesting
    System,             // System status, errors
    Market,             // Market hours, halts
    Dividend,           // Dividend payments
    Earnings,           // Earnings announcements
    News,               // Relevant news
    Custom              // User-defined
};

inline std::string category_to_string(AlertCategory category) {
    switch (category) {
        case AlertCategory::Trade: return "trade";
        case AlertCategory::Price: return "price";
        case AlertCategory::Portfolio: return "portfolio";
        case AlertCategory::Risk: return "risk";
        case AlertCategory::Tax: return "tax";
        case AlertCategory::System: return "system";
        case AlertCategory::Market: return "market";
        case AlertCategory::Dividend: return "dividend";
        case AlertCategory::Earnings: return "earnings";
        case AlertCategory::News: return "news";
        case AlertCategory::Custom: return "custom";
        default: return "custom";
    }
}

/**
 * @brief Notification message
 */
struct Notification {
    std::string id;
    std::string user_id;
    std::string title;
    std::string body;
    std::string html_body;           // For email
    AlertCategory category{AlertCategory::Custom};
    NotificationPriority priority{NotificationPriority::Normal};
    std::vector<NotificationChannel> channels;
    
    std::map<std::string, std::string> data;  // Additional data
    std::string symbol;              // Related symbol if any
    double value{0};                 // Related value if any
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point sent_at;
    bool sent{false};
    bool delivered{false};
    bool read{false};
    
    std::string error;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "[" << priority_to_string(priority) << "] "
            << "[" << category_to_string(category) << "] "
            << title << ": " << body;
        return oss.str();
    }
    
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{"
            << "\"id\":\"" << id << "\","
            << "\"title\":\"" << title << "\","
            << "\"body\":\"" << body << "\","
            << "\"category\":\"" << category_to_string(category) << "\","
            << "\"priority\":\"" << priority_to_string(priority) << "\"";
        if (!symbol.empty()) {
            oss << ",\"symbol\":\"" << symbol << "\"";
        }
        if (value != 0) {
            oss << ",\"value\":" << value;
        }
        oss << "}";
        return oss.str();
    }
};

// =============================================================================
// Email Service
// =============================================================================

/**
 * @brief SMTP configuration
 */
struct SMTPConfig {
    std::string host{"smtp.gmail.com"};
    int port{587};
    bool use_tls{true};
    std::string username;
    std::string password;
    std::string from_address;
    std::string from_name{"Metis Genie Platform"};
};

/**
 * @brief Email notification service
 */
class EmailService {
private:
    SMTPConfig config_;
    HttpClient http_client_;
    mutable std::mutex mutex_;
    bool initialized_{false};
    
    // For API-based email (SendGrid, Mailgun, etc.)
    std::string api_key_;
    std::string api_endpoint_;
    enum class Provider { SMTP, SendGrid, Mailgun, SES } provider_{Provider::SMTP};
    
    std::string build_sendgrid_payload(const std::string& to,
                                        const std::string& subject,
                                        const std::string& body,
                                        const std::string& html_body) {
        std::ostringstream oss;
        oss << "{"
            << "\"personalizations\":[{\"to\":[{\"email\":\"" << to << "\"}]}],"
            << "\"from\":{\"email\":\"" << config_.from_address << "\","
            << "\"name\":\"" << config_.from_name << "\"},"
            << "\"subject\":\"" << subject << "\","
            << "\"content\":[";
        
        if (!body.empty()) {
            oss << "{\"type\":\"text/plain\",\"value\":\"" << escape_json(body) << "\"}";
        }
        if (!html_body.empty()) {
            if (!body.empty()) oss << ",";
            oss << "{\"type\":\"text/html\",\"value\":\"" << escape_json(html_body) << "\"}";
        }
        
        oss << "]}";
        return oss.str();
    }
    
    std::string escape_json(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }

public:
    void configure_smtp(const SMTPConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        provider_ = Provider::SMTP;
        initialized_ = true;
    }
    
    void configure_sendgrid(const std::string& api_key, 
                            const std::string& from_address,
                            const std::string& from_name = "Metis Genie Platform") {
        std::lock_guard<std::mutex> lock(mutex_);
        api_key_ = api_key;
        api_endpoint_ = "https://api.sendgrid.com/v3/mail/send";
        config_.from_address = from_address;
        config_.from_name = from_name;
        provider_ = Provider::SendGrid;
        initialized_ = true;
    }
    
    void configure_mailgun(const std::string& api_key,
                           const std::string& domain,
                           const std::string& from_address) {
        std::lock_guard<std::mutex> lock(mutex_);
        api_key_ = api_key;
        api_endpoint_ = "https://api.mailgun.net/v3/" + domain + "/messages";
        config_.from_address = from_address;
        provider_ = Provider::Mailgun;
        initialized_ = true;
    }
    
    bool send(const std::string& to,
              const std::string& subject,
              const std::string& body,
              const std::string& html_body = "") {
        
        if (!initialized_) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (provider_) {
            case Provider::SendGrid: {
                std::map<std::string, std::string> headers = {
                    {"Authorization", "Bearer " + api_key_},
                    {"Content-Type", "application/json"}
                };
                
                std::string payload = build_sendgrid_payload(to, subject, body, html_body);
                auto response = http_client_.post(api_endpoint_, payload, headers);
                
                return response.status_code >= 200 && response.status_code < 300;
            }
            
            case Provider::Mailgun: {
                std::map<std::string, std::string> headers = {
                    {"Authorization", "Basic " + base64_encode("api:" + api_key_)}
                };
                
                std::string payload = "from=" + url_encode(config_.from_address) +
                                      "&to=" + url_encode(to) +
                                      "&subject=" + url_encode(subject) +
                                      "&text=" + url_encode(body);
                if (!html_body.empty()) {
                    payload += "&html=" + url_encode(html_body);
                }
                
                auto response = http_client_.post(api_endpoint_, payload, headers);
                return response.status_code >= 200 && response.status_code < 300;
            }
            
            case Provider::SMTP:
            default: {
                // Real SMTP implementation via SmtpClient
                SmtpClient::Config smtp_config;
                smtp_config.host = config_.host;
                smtp_config.port = config_.port;
                smtp_config.use_tls = config_.use_tls;
                smtp_config.username = config_.username;
                smtp_config.password = config_.password;
                smtp_config.from_address = config_.from_address;
                smtp_config.from_name = config_.from_name;
                
                SmtpClient smtp(smtp_config);
                return smtp.send_email(to, subject, body, html_body);
            }
        }
    }
    
    bool send_notification(const Notification& notification,
                           const std::string& recipient_email) {
        std::string html = notification.html_body.empty() ? 
            build_default_html(notification) : notification.html_body;
        
        return send(recipient_email, notification.title, notification.body, html);
    }
    
private:
    std::string build_default_html(const Notification& notification) {
        std::ostringstream oss;
        oss << "<!DOCTYPE html><html><head>"
            << "<style>"
            << "body{font-family:Arial,sans-serif;line-height:1.6;color:#333;}"
            << ".container{max-width:600px;margin:0 auto;padding:20px;}"
            << ".header{background:#2563eb;color:white;padding:20px;text-align:center;}"
            << ".content{padding:20px;background:#f9fafb;}"
            << ".footer{text-align:center;padding:10px;font-size:12px;color:#666;}"
            << ".priority-critical{border-left:4px solid #dc2626;}"
            << ".priority-high{border-left:4px solid #f59e0b;}"
            << ".priority-normal{border-left:4px solid #10b981;}"
            << ".priority-low{border-left:4px solid #6b7280;}"
            << "</style></head><body>"
            << "<div class=\"container\">"
            << "<div class=\"header\"><h1>Metis Genie Platform Alert</h1></div>"
            << "<div class=\"content priority-" << priority_to_string(notification.priority) << "\">"
            << "<h2>" << notification.title << "</h2>"
            << "<p>" << notification.body << "</p>";
        
        if (!notification.symbol.empty()) {
            oss << "<p><strong>Symbol:</strong> " << notification.symbol << "</p>";
        }
        if (notification.value != 0) {
            oss << "<p><strong>Value:</strong> $" << std::fixed << std::setprecision(2) 
                << notification.value << "</p>";
        }
        
        oss << "</div>"
            << "<div class=\"footer\">"
            << "<p>This is an automated message from Metis Genie Platform Investment Platform</p>"
            << "</div></div></body></html>";
        
        return oss.str();
    }
    
    std::string base64_encode(const std::string& input) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int val = 0, valb = -6;
        
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result += chars[(val >> valb) & 0x3F];
                valb -= 6;
            }
        }
        
        if (valb > -6) result += chars[((val << 8) >> (valb + 8)) & 0x3F];
        while (result.size() % 4) result += '=';
        
        return result;
    }
    
    std::string url_encode(const std::string& str) {
        std::ostringstream encoded;
        encoded << std::hex << std::setfill('0');
        
        for (char c : str) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded << c;
            } else {
                encoded << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        
        return encoded.str();
    }
};

// =============================================================================
// SMS Service (Twilio)
// =============================================================================

/**
 * @brief Twilio configuration
 */
struct TwilioConfig {
    std::string account_sid;
    std::string auth_token;
    std::string from_number;          // Twilio phone number
    std::string messaging_service_sid; // Optional: use messaging service
};

/**
 * @brief SMS notification service via Twilio
 */
class SMSService {
private:
    TwilioConfig config_;
    HttpClient http_client_;
    mutable std::mutex mutex_;
    bool initialized_{false};
    
    std::string build_auth_header() {
        return "Basic " + base64_encode(config_.account_sid + ":" + config_.auth_token);
    }
    
    std::string base64_encode(const std::string& input) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int val = 0, valb = -6;
        
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result += chars[(val >> valb) & 0x3F];
                valb -= 6;
            }
        }
        
        if (valb > -6) result += chars[((val << 8) >> (valb + 8)) & 0x3F];
        while (result.size() % 4) result += '=';
        
        return result;
    }
    
    std::string url_encode(const std::string& str) {
        std::ostringstream encoded;
        encoded << std::hex << std::setfill('0');
        
        for (char c : str) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded << c;
            } else {
                encoded << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        
        return encoded.str();
    }

public:
    void configure(const TwilioConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        initialized_ = !config.account_sid.empty() && !config.auth_token.empty();
    }
    
    void configure(const std::string& account_sid,
                   const std::string& auth_token,
                   const std::string& from_number) {
        TwilioConfig config;
        config.account_sid = account_sid;
        config.auth_token = auth_token;
        config.from_number = from_number;
        configure(config);
    }
    
    bool send(const std::string& to_number, const std::string& message) {
        if (!initialized_) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string endpoint = "https://api.twilio.com/2010-04-01/Accounts/" +
                               config_.account_sid + "/Messages.json";
        
        std::map<std::string, std::string> headers = {
            {"Authorization", build_auth_header()},
            {"Content-Type", "application/x-www-form-urlencoded"}
        };
        
        std::string body;
        if (!config_.messaging_service_sid.empty()) {
            body = "MessagingServiceSid=" + url_encode(config_.messaging_service_sid);
        } else {
            body = "From=" + url_encode(config_.from_number);
        }
        body += "&To=" + url_encode(to_number);
        body += "&Body=" + url_encode(message);
        
        auto response = http_client_.post(endpoint, body, headers);
        
        return response.status_code >= 200 && response.status_code < 300;
    }
    
    bool send_notification(const Notification& notification,
                           const std::string& phone_number) {
        // SMS should be concise
        std::string message = "[Metis Genie Platform] " + notification.title;
        if (!notification.body.empty()) {
            message += ": " + notification.body;
        }
        
        // Truncate if too long (SMS limit is 160 chars for single message)
        if (message.length() > 160) {
            message = message.substr(0, 157) + "...";
        }
        
        return send(phone_number, message);
    }
    
    bool is_configured() const {
        return initialized_;
    }
};

// =============================================================================
// Slack Service
// =============================================================================

/**
 * @brief Slack configuration
 */
struct SlackConfig {
    std::string webhook_url;
    std::string bot_token;            // For API-based messages
    std::string default_channel;
    std::string username{"Metis Genie Platform"};
    std::string icon_emoji{":chart_with_upwards_trend:"};
};

/**
 * @brief Slack notification service
 */
class SlackService {
private:
    SlackConfig config_;
    HttpClient http_client_;
    mutable std::mutex mutex_;
    bool initialized_{false};
    
    std::string escape_json(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    std::string priority_to_color(NotificationPriority priority) {
        switch (priority) {
            case NotificationPriority::Critical: return "#dc2626"; // Red
            case NotificationPriority::High: return "#f59e0b";     // Amber
            case NotificationPriority::Normal: return "#10b981";   // Green
            case NotificationPriority::Low: return "#6b7280";      // Gray
            default: return "#3b82f6";                             // Blue
        }
    }
    
    std::string build_webhook_payload(const std::string& text,
                                       const std::string& channel = "") {
        std::ostringstream oss;
        oss << "{"
            << "\"username\":\"" << escape_json(config_.username) << "\","
            << "\"icon_emoji\":\"" << config_.icon_emoji << "\","
            << "\"text\":\"" << escape_json(text) << "\"";
        
        if (!channel.empty()) {
            oss << ",\"channel\":\"" << escape_json(channel) << "\"";
        }
        
        oss << "}";
        return oss.str();
    }
    
    std::string build_rich_payload(const Notification& notification,
                                   const std::string& channel = "") {
        std::ostringstream oss;
        oss << "{"
            << "\"username\":\"" << escape_json(config_.username) << "\","
            << "\"icon_emoji\":\"" << config_.icon_emoji << "\",";
        
        if (!channel.empty()) {
            oss << "\"channel\":\"" << escape_json(channel) << "\",";
        }
        
        oss << "\"attachments\":[{"
            << "\"color\":\"" << priority_to_color(notification.priority) << "\","
            << "\"title\":\"" << escape_json(notification.title) << "\","
            << "\"text\":\"" << escape_json(notification.body) << "\","
            << "\"fields\":[";
        
        bool first_field = true;
        
        if (!notification.symbol.empty()) {
            oss << "{\"title\":\"Symbol\",\"value\":\"" << notification.symbol 
                << "\",\"short\":true}";
            first_field = false;
        }
        
        if (notification.value != 0) {
            if (!first_field) oss << ",";
            oss << "{\"title\":\"Value\",\"value\":\"$" 
                << std::fixed << std::setprecision(2) << notification.value 
                << "\",\"short\":true}";
            first_field = false;
        }
        
        if (!first_field) oss << ",";
        oss << "{\"title\":\"Category\",\"value\":\"" 
            << category_to_string(notification.category) 
            << "\",\"short\":true}";
        
        oss << "{\"title\":\"Priority\",\"value\":\"" 
            << priority_to_string(notification.priority) 
            << "\",\"short\":true}";
        
        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        oss << "],\"ts\":" << epoch << "}]}";
        
        return oss.str();
    }

public:
    void configure(const SlackConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        initialized_ = !config.webhook_url.empty() || !config.bot_token.empty();
    }
    
    void configure_webhook(const std::string& webhook_url) {
        SlackConfig config;
        config.webhook_url = webhook_url;
        configure(config);
    }
    
    bool send(const std::string& message, const std::string& channel = "") {
        if (!initialized_) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string target_channel = channel.empty() ? config_.default_channel : channel;
        std::string payload = build_webhook_payload(message, target_channel);
        
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };
        
        std::string endpoint = config_.webhook_url;
        
        // If using bot token instead of webhook
        if (endpoint.empty() && !config_.bot_token.empty()) {
            endpoint = "https://slack.com/api/chat.postMessage";
            headers["Authorization"] = "Bearer " + config_.bot_token;
        }
        
        if (endpoint.empty()) {
            return false;
        }
        
        auto response = http_client_.post(endpoint, payload, headers);
        return response.status_code >= 200 && response.status_code < 300;
    }
    
    bool send_notification(const Notification& notification,
                           const std::string& channel = "") {
        if (!initialized_) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string target_channel = channel.empty() ? config_.default_channel : channel;
        std::string payload = build_rich_payload(notification, target_channel);
        
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };
        
        std::string endpoint = config_.webhook_url;
        if (endpoint.empty() && !config_.bot_token.empty()) {
            endpoint = "https://slack.com/api/chat.postMessage";
            headers["Authorization"] = "Bearer " + config_.bot_token;
        }
        
        if (endpoint.empty()) {
            return false;
        }
        
        auto response = http_client_.post(endpoint, payload, headers);
        return response.status_code >= 200 && response.status_code < 300;
    }
    
    bool is_configured() const {
        return initialized_;
    }
};

// =============================================================================
// Push Notification Service
// =============================================================================

/**
 * @brief Push notification provider
 */
enum class PushProvider {
    FCM,        // Firebase Cloud Messaging
    APNS,       // Apple Push Notification Service
    OneSignal,
    Pusher
};

/**
 * @brief Push notification configuration
 */
struct PushConfig {
    PushProvider provider{PushProvider::FCM};
    std::string api_key;
    std::string app_id;
    std::string sender_id;
    
    // FCM specific
    std::string fcm_server_key;
    
    // APNS specific
    std::string apns_key_id;
    std::string apns_team_id;
    std::string apns_bundle_id;
    std::string apns_auth_key;
    bool apns_production{false};
    
    // OneSignal specific
    std::string onesignal_app_id;
    std::string onesignal_api_key;
};

/**
 * @brief Push notification service
 */
class PushService {
private:
    PushConfig config_;
    HttpClient http_client_;
    mutable std::mutex mutex_;
    bool initialized_{false};
    
    // Device token storage
    std::map<std::string, std::vector<std::string>> user_tokens_; // user_id -> tokens
    
    std::string escape_json(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    bool send_fcm(const std::string& token, const Notification& notification) {
        std::string endpoint = "https://fcm.googleapis.com/fcm/send";
        
        std::map<std::string, std::string> headers = {
            {"Authorization", "key=" + config_.fcm_server_key},
            {"Content-Type", "application/json"}
        };
        
        std::ostringstream payload;
        payload << "{"
                << "\"to\":\"" << token << "\","
                << "\"notification\":{"
                << "\"title\":\"" << escape_json(notification.title) << "\","
                << "\"body\":\"" << escape_json(notification.body) << "\","
                << "\"sound\":\"default\""
                << "},"
                << "\"data\":{"
                << "\"category\":\"" << category_to_string(notification.category) << "\","
                << "\"priority\":\"" << priority_to_string(notification.priority) << "\"";
        
        if (!notification.symbol.empty()) {
            payload << ",\"symbol\":\"" << notification.symbol << "\"";
        }
        
        payload << "}}";
        
        auto response = http_client_.post(endpoint, payload.str(), headers);
        return response.status_code >= 200 && response.status_code < 300;
    }
    
    bool send_onesignal(const std::string& user_id, const Notification& notification) {
        std::string endpoint = "https://onesignal.com/api/v1/notifications";
        
        std::map<std::string, std::string> headers = {
            {"Authorization", "Basic " + config_.onesignal_api_key},
            {"Content-Type", "application/json"}
        };
        
        std::ostringstream payload;
        payload << "{"
                << "\"app_id\":\"" << config_.onesignal_app_id << "\","
                << "\"include_external_user_ids\":[\"" << user_id << "\"],"
                << "\"headings\":{\"en\":\"" << escape_json(notification.title) << "\"},"
                << "\"contents\":{\"en\":\"" << escape_json(notification.body) << "\"},"
                << "\"data\":{"
                << "\"category\":\"" << category_to_string(notification.category) << "\","
                << "\"priority\":\"" << priority_to_string(notification.priority) << "\"";
        
        if (!notification.symbol.empty()) {
            payload << ",\"symbol\":\"" << notification.symbol << "\"";
        }
        
        payload << "}}";
        
        auto response = http_client_.post(endpoint, payload.str(), headers);
        return response.status_code >= 200 && response.status_code < 300;
    }

public:
    void configure(const PushConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        
        switch (config.provider) {
            case PushProvider::FCM:
                initialized_ = !config.fcm_server_key.empty();
                break;
            case PushProvider::OneSignal:
                initialized_ = !config.onesignal_api_key.empty() && 
                               !config.onesignal_app_id.empty();
                break;
            default:
                initialized_ = false;
        }
    }
    
    void configure_fcm(const std::string& server_key) {
        PushConfig config;
        config.provider = PushProvider::FCM;
        config.fcm_server_key = server_key;
        configure(config);
    }
    
    void configure_onesignal(const std::string& app_id, const std::string& api_key) {
        PushConfig config;
        config.provider = PushProvider::OneSignal;
        config.onesignal_app_id = app_id;
        config.onesignal_api_key = api_key;
        configure(config);
    }
    
    void register_device(const std::string& user_id, const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& tokens = user_tokens_[user_id];
        if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
            tokens.push_back(token);
        }
    }
    
    void unregister_device(const std::string& user_id, const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_tokens_.find(user_id);
        if (it != user_tokens_.end()) {
            auto& tokens = it->second;
            tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
        }
    }
    
    bool send_notification(const Notification& notification,
                           const std::string& user_id) {
        if (!initialized_) {
            return false;
        }
        
        switch (config_.provider) {
            case PushProvider::FCM: {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = user_tokens_.find(user_id);
                if (it == user_tokens_.end() || it->second.empty()) {
                    return false;
                }
                
                bool success = true;
                for (const auto& token : it->second) {
                    if (!send_fcm(token, notification)) {
                        success = false;
                    }
                }
                return success;
            }
            
            case PushProvider::OneSignal:
                return send_onesignal(user_id, notification);
            
            default:
                return false;
        }
    }
    
    bool is_configured() const {
        return initialized_;
    }
};

// =============================================================================
// Webhook Service
// =============================================================================

/**
 * @brief Generic webhook configuration
 */
struct WebhookConfig {
    std::string url;
    std::string secret;               // For signature verification
    std::map<std::string, std::string> headers;
    bool include_signature{true};
    std::string signature_header{"X-Webhook-Signature"};
};

/**
 * @brief Generic webhook notification service
 */
class WebhookService {
private:
    std::map<std::string, WebhookConfig> webhooks_;
    HttpClient http_client_;
    mutable std::mutex mutex_;
    
    std::string escape_json(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    std::string compute_signature(const std::string& payload, 
                                   const std::string& secret) {
        // HMAC-SHA256: H((K' ^ opad) || H((K' ^ ipad) || message))
        const size_t block_size = 64;
        
        // Step 1: Normalize key to block_size bytes
        std::string key = secret;
        if (key.size() > block_size) {
            key = genie::crypto::sha256(key);
            // sha256 returns hex string; convert to raw bytes
            std::string raw;
            for (size_t i = 0; i + 1 < key.size(); i += 2) {
                raw.push_back(static_cast<char>(
                    std::stoi(key.substr(i, 2), nullptr, 16)));
            }
            key = raw;
        }
        key.resize(block_size, '\0');
        
        // Step 2: XOR with ipad (0x36) and opad (0x5c)
        std::string ipad_key(block_size, '\0');
        std::string opad_key(block_size, '\0');
        for (size_t i = 0; i < block_size; ++i) {
            ipad_key[i] = key[i] ^ 0x36;
            opad_key[i] = key[i] ^ 0x5c;
        }
        
        // Step 3: Inner hash = SHA256(ipad_key || payload)
        std::string inner_data = ipad_key + payload;
        std::string inner_hex = genie::crypto::sha256(inner_data);
        
        // Convert inner hash hex to raw bytes
        std::string inner_raw;
        for (size_t i = 0; i + 1 < inner_hex.size(); i += 2) {
            inner_raw.push_back(static_cast<char>(
                std::stoi(inner_hex.substr(i, 2), nullptr, 16)));
        }
        
        // Step 4: Outer hash = SHA256(opad_key || inner_raw)
        std::string outer_data = opad_key + inner_raw;
        return "sha256=" + genie::crypto::sha256(outer_data);
    }

public:
    void add_webhook(const std::string& name, const WebhookConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        webhooks_[name] = config;
    }
    
    void add_webhook(const std::string& name, const std::string& url,
                     const std::string& secret = "") {
        WebhookConfig config;
        config.url = url;
        config.secret = secret;
        add_webhook(name, config);
    }
    
    void remove_webhook(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        webhooks_.erase(name);
    }
    
    bool send(const std::string& webhook_name, const Notification& notification) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = webhooks_.find(webhook_name);
        if (it == webhooks_.end()) {
            return false;
        }
        
        const auto& config = it->second;
        
        // Build JSON payload
        std::ostringstream payload;
        payload << "{"
                << "\"event\":\"notification\","
                << "\"id\":\"" << notification.id << "\","
                << "\"title\":\"" << escape_json(notification.title) << "\","
                << "\"body\":\"" << escape_json(notification.body) << "\","
                << "\"category\":\"" << category_to_string(notification.category) << "\","
                << "\"priority\":\"" << priority_to_string(notification.priority) << "\"";
        
        if (!notification.symbol.empty()) {
            payload << ",\"symbol\":\"" << notification.symbol << "\"";
        }
        if (notification.value != 0) {
            payload << ",\"value\":" << notification.value;
        }
        
        // Add timestamp
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        payload << ",\"timestamp\":" << epoch;
        
        payload << "}";
        
        std::string payload_str = payload.str();
        
        // Build headers
        std::map<std::string, std::string> headers = config.headers;
        headers["Content-Type"] = "application/json";
        
        if (config.include_signature && !config.secret.empty()) {
            headers[config.signature_header] = compute_signature(payload_str, config.secret);
        }
        
        auto response = http_client_.post(config.url, payload_str, headers);
        return response.status_code >= 200 && response.status_code < 300;
    }
    
    bool send_all(const Notification& notification) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool all_success = true;
        for (const auto& [name, _] : webhooks_) {
            if (!send(name, notification)) {
                all_success = false;
            }
        }
        return all_success;
    }
    
    std::vector<std::string> list_webhooks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : webhooks_) {
            names.push_back(name);
        }
        return names;
    }
};

// =============================================================================
// Alert Preferences
// =============================================================================

/**
 * @brief User alert preferences
 */
struct AlertPreferences {
    std::string user_id;
    
    // Channel preferences per category
    std::map<AlertCategory, std::set<NotificationChannel>> category_channels;
    
    // Priority thresholds
    std::map<NotificationChannel, NotificationPriority> channel_min_priority;
    
    // Quiet hours
    bool enable_quiet_hours{false};
    int quiet_start_hour{22};         // 10 PM
    int quiet_end_hour{7};            // 7 AM
    bool quiet_allow_critical{true};  // Allow critical during quiet hours
    
    // Digest preferences
    bool enable_digest{false};
    std::string digest_time{"08:00"}; // Daily digest time
    std::set<AlertCategory> digest_categories;
    
    // Contact info
    std::string email;
    std::string phone;
    std::string slack_user_id;
    
    // Enabled channels
    std::set<NotificationChannel> enabled_channels;
    
    // Muted symbols
    std::set<std::string> muted_symbols;
    
    // Rate limiting
    int max_per_hour{100};
    int max_per_day{500};
    
    bool should_send(const Notification& notification) const {
        // Check if category is enabled for any channel
        auto cat_it = category_channels.find(notification.category);
        if (cat_it == category_channels.end() || cat_it->second.empty()) {
            return false;
        }
        
        // Check muted symbols
        if (!notification.symbol.empty() && 
            muted_symbols.count(notification.symbol) > 0) {
            return false;
        }
        
        // Check quiet hours
        if (enable_quiet_hours) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto* tm = std::localtime(&time);
            int hour = tm->tm_hour;
            
            bool in_quiet = false;
            if (quiet_start_hour > quiet_end_hour) {
                // Spans midnight
                in_quiet = hour >= quiet_start_hour || hour < quiet_end_hour;
            } else {
                in_quiet = hour >= quiet_start_hour && hour < quiet_end_hour;
            }
            
            if (in_quiet) {
                if (!quiet_allow_critical || 
                    notification.priority != NotificationPriority::Critical) {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    std::set<NotificationChannel> get_channels(const Notification& notification) const {
        std::set<NotificationChannel> result;
        
        auto cat_it = category_channels.find(notification.category);
        if (cat_it == category_channels.end()) {
            return result;
        }
        
        for (auto channel : cat_it->second) {
            // Check if channel is enabled
            if (enabled_channels.count(channel) == 0) {
                continue;
            }
            
            // Check priority threshold
            auto prio_it = channel_min_priority.find(channel);
            if (prio_it != channel_min_priority.end()) {
                if (static_cast<int>(notification.priority) < 
                    static_cast<int>(prio_it->second)) {
                    continue;
                }
            }
            
            result.insert(channel);
        }
        
        return result;
    }
    
    void set_default_preferences() {
        // Enable all channels
        enabled_channels = {
            NotificationChannel::Email,
            NotificationChannel::Push,
            NotificationChannel::InApp
        };
        
        // Set default category channels
        category_channels[AlertCategory::Trade] = {
            NotificationChannel::Push, 
            NotificationChannel::InApp
        };
        category_channels[AlertCategory::Price] = {
            NotificationChannel::Push, 
            NotificationChannel::InApp
        };
        category_channels[AlertCategory::Risk] = {
            NotificationChannel::Email, 
            NotificationChannel::SMS,
            NotificationChannel::Push, 
            NotificationChannel::InApp
        };
        category_channels[AlertCategory::Portfolio] = {
            NotificationChannel::Email, 
            NotificationChannel::InApp
        };
        category_channels[AlertCategory::System] = {
            NotificationChannel::Email, 
            NotificationChannel::InApp
        };
        
        // Set minimum priority for SMS (high or critical only)
        channel_min_priority[NotificationChannel::SMS] = NotificationPriority::High;
    }
};

/**
 * @brief Preferences manager
 */
class PreferencesManager {
private:
    std::map<std::string, AlertPreferences> preferences_;
    mutable std::mutex mutex_;

public:
    AlertPreferences get_preferences(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = preferences_.find(user_id);
        if (it != preferences_.end()) {
            return it->second;
        }
        
        // Return default preferences
        AlertPreferences prefs;
        prefs.user_id = user_id;
        prefs.set_default_preferences();
        return prefs;
    }
    
    void set_preferences(const std::string& user_id, const AlertPreferences& prefs) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id] = prefs;
    }
    
    void update_email(const std::string& user_id, const std::string& email) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].email = email;
    }
    
    void update_phone(const std::string& user_id, const std::string& phone) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].phone = phone;
    }
    
    void enable_channel(const std::string& user_id, NotificationChannel channel) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].enabled_channels.insert(channel);
    }
    
    void disable_channel(const std::string& user_id, NotificationChannel channel) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].enabled_channels.erase(channel);
    }
    
    void mute_symbol(const std::string& user_id, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].muted_symbols.insert(symbol);
    }
    
    void unmute_symbol(const std::string& user_id, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[user_id].muted_symbols.erase(symbol);
    }
    
    void set_quiet_hours(const std::string& user_id, int start, int end, 
                         bool allow_critical = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& prefs = preferences_[user_id];
        prefs.enable_quiet_hours = true;
        prefs.quiet_start_hour = start;
        prefs.quiet_end_hour = end;
        prefs.quiet_allow_critical = allow_critical;
    }
};

// =============================================================================
// Unified Notification Manager
// =============================================================================

/**
 * @brief Unified notification manager
 */
class NotificationManager {
private:
    EmailService email_service_;
    SMSService sms_service_;
    SlackService slack_service_;
    PushService push_service_;
    WebhookService webhook_service_;
    PreferencesManager preferences_;
    
    std::queue<Notification> pending_queue_;
    std::vector<Notification> sent_history_;
    std::map<std::string, std::vector<Notification>> in_app_notifications_;
    
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    
    // Rate limiting
    std::map<std::string, int> hourly_counts_;
    std::map<std::string, int> daily_counts_;
    std::chrono::system_clock::time_point last_hour_reset_;
    std::chrono::system_clock::time_point last_day_reset_;
    
    int notification_counter_{0};
    
    std::string generate_id() {
        return "notif-" + std::to_string(++notification_counter_) + "-" +
               std::to_string(std::chrono::system_clock::now()
                   .time_since_epoch().count());
    }
    
    void process_queue() {
        while (running_) {
            Notification notification;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                
                // Reset rate limits if needed
                auto now = std::chrono::system_clock::now();
                if (std::chrono::duration_cast<std::chrono::hours>(
                        now - last_hour_reset_).count() >= 1) {
                    hourly_counts_.clear();
                    last_hour_reset_ = now;
                }
                if (std::chrono::duration_cast<std::chrono::hours>(
                        now - last_day_reset_).count() >= 24) {
                    daily_counts_.clear();
                    last_day_reset_ = now;
                }
                
                if (pending_queue_.empty()) {
                    // Sleep briefly if queue is empty
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                
                notification = pending_queue_.front();
                pending_queue_.pop();
            }
            
            send_notification_internal(notification);
        }
    }
    
    void send_notification_internal(Notification& notification) {
        auto prefs = preferences_.get_preferences(notification.user_id);
        
        // Check if notification should be sent
        if (!prefs.should_send(notification)) {
            return;
        }
        
        // Check rate limits
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int& hourly = hourly_counts_[notification.user_id];
            int& daily = daily_counts_[notification.user_id];
            
            if (hourly >= prefs.max_per_hour || daily >= prefs.max_per_day) {
                return; // Rate limited
            }
            
            hourly++;
            daily++;
        }
        
        // Get channels to send to
        auto channels = prefs.get_channels(notification);
        
        bool any_success = false;
        
        for (auto channel : channels) {
            bool success = false;
            
            switch (channel) {
                case NotificationChannel::Email:
                    if (!prefs.email.empty()) {
                        success = email_service_.send_notification(notification, prefs.email);
                    }
                    break;
                    
                case NotificationChannel::SMS:
                    if (!prefs.phone.empty()) {
                        success = sms_service_.send_notification(notification, prefs.phone);
                    }
                    break;
                    
                case NotificationChannel::Slack:
                    if (!prefs.slack_user_id.empty()) {
                        success = slack_service_.send_notification(notification, prefs.slack_user_id);
                    } else {
                        success = slack_service_.send_notification(notification);
                    }
                    break;
                    
                case NotificationChannel::Push:
                    success = push_service_.send_notification(notification, notification.user_id);
                    break;
                    
                case NotificationChannel::Webhook:
                    success = webhook_service_.send_all(notification);
                    break;
                    
                case NotificationChannel::InApp:
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        in_app_notifications_[notification.user_id].push_back(notification);
                        success = true;
                    }
                    break;
                    
                default:
                    break;
            }
            
            if (success) {
                any_success = true;
            }
        }
        
        notification.sent = any_success;
        notification.sent_at = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sent_history_.push_back(notification);
        }
    }

public:
    NotificationManager() {
        last_hour_reset_ = std::chrono::system_clock::now();
        last_day_reset_ = std::chrono::system_clock::now();
    }
    
    ~NotificationManager() {
        stop();
    }
    
    void start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = std::thread([this]() { process_queue(); });
    }
    
    void stop() {
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    // Service accessors
    EmailService& email() { return email_service_; }
    SMSService& sms() { return sms_service_; }
    SlackService& slack() { return slack_service_; }
    PushService& push() { return push_service_; }
    WebhookService& webhook() { return webhook_service_; }
    PreferencesManager& preferences() { return preferences_; }
    
    /**
     * @brief Queue a notification for sending
     */
    std::string send(const std::string& user_id,
                     const std::string& title,
                     const std::string& body,
                     AlertCategory category = AlertCategory::Custom,
                     NotificationPriority priority = NotificationPriority::Normal,
                     const std::string& symbol = "",
                     double value = 0) {
        
        Notification notification;
        notification.id = generate_id();
        notification.user_id = user_id;
        notification.title = title;
        notification.body = body;
        notification.category = category;
        notification.priority = priority;
        notification.symbol = symbol;
        notification.value = value;
        notification.created_at = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_queue_.push(notification);
        }
        
        return notification.id;
    }
    
    /**
     * @brief Send notification immediately (blocking)
     */
    bool send_immediate(const std::string& user_id,
                        const std::string& title,
                        const std::string& body,
                        AlertCategory category = AlertCategory::Custom,
                        NotificationPriority priority = NotificationPriority::Normal) {
        
        Notification notification;
        notification.id = generate_id();
        notification.user_id = user_id;
        notification.title = title;
        notification.body = body;
        notification.category = category;
        notification.priority = priority;
        notification.created_at = std::chrono::system_clock::now();
        
        send_notification_internal(notification);
        return notification.sent;
    }
    
    /**
     * @brief Send trade alert
     */
    std::string send_trade_alert(const std::string& user_id,
                                  const std::string& symbol,
                                  const std::string& side,
                                  double quantity,
                                  double price) {
        std::ostringstream body;
        body << std::fixed << std::setprecision(2);
        body << side << " " << quantity << " shares of " << symbol 
             << " @ $" << price;
        
        return send(user_id, "Order Filled", body.str(), 
                   AlertCategory::Trade, NotificationPriority::Normal, 
                   symbol, price * quantity);
    }
    
    /**
     * @brief Send price alert
     */
    std::string send_price_alert(const std::string& user_id,
                                  const std::string& symbol,
                                  double price,
                                  double target,
                                  const std::string& condition) {
        std::ostringstream body;
        body << std::fixed << std::setprecision(2);
        body << symbol << " " << condition << " $" << target 
             << " (current: $" << price << ")";
        
        return send(user_id, "Price Alert", body.str(),
                   AlertCategory::Price, NotificationPriority::High,
                   symbol, price);
    }
    
    /**
     * @brief Send risk alert
     */
    std::string send_risk_alert(const std::string& user_id,
                                 const std::string& message,
                                 const std::string& symbol = "") {
        return send(user_id, "Risk Alert", message,
                   AlertCategory::Risk, NotificationPriority::Critical,
                   symbol);
    }
    
    /**
     * @brief Get in-app notifications for user
     */
    std::vector<Notification> get_in_app_notifications(const std::string& user_id,
                                                        bool unread_only = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = in_app_notifications_.find(user_id);
        if (it == in_app_notifications_.end()) {
            return {};
        }
        
        if (!unread_only) {
            return it->second;
        }
        
        std::vector<Notification> unread;
        for (const auto& n : it->second) {
            if (!n.read) {
                unread.push_back(n);
            }
        }
        return unread;
    }
    
    /**
     * @brief Mark notification as read
     */
    void mark_read(const std::string& user_id, const std::string& notification_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = in_app_notifications_.find(user_id);
        if (it == in_app_notifications_.end()) {
            return;
        }
        
        for (auto& n : it->second) {
            if (n.id == notification_id) {
                n.read = true;
                break;
            }
        }
    }
    
    /**
     * @brief Mark all as read
     */
    void mark_all_read(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = in_app_notifications_.find(user_id);
        if (it == in_app_notifications_.end()) {
            return;
        }
        
        for (auto& n : it->second) {
            n.read = true;
        }
    }
    
    /**
     * @brief Get notification history
     */
    std::vector<Notification> get_history(const std::string& user_id = "",
                                           int limit = 100) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<Notification> result;
        
        for (auto it = sent_history_.rbegin(); 
             it != sent_history_.rend() && result.size() < static_cast<size_t>(limit);
             ++it) {
            if (user_id.empty() || it->user_id == user_id) {
                result.push_back(*it);
            }
        }
        
        return result;
    }
    
    /**
     * @brief Get pending count
     */
    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_queue_.size();
    }
};

} // namespace genie::core

#endif // GENIE_CORE_NOTIFICATIONS_HPP
