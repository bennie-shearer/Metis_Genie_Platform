/**
 * @file cli.hpp
 * @brief Command-line interface for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CLI_HPP
#define GENIE_CLI_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>

#ifndef GENIE_CLI_VERSION
#define GENIE_CLI_VERSION "5.3.1"
#endif

namespace genie {
namespace cli {

struct Command {
    std::string name;
    std::string description;
    std::string usage;
    std::function<std::string(const std::vector<std::string>&)> handler;
};

class CommandParser {
public:
    static std::vector<std::string> parse(const std::string& input) {
        std::vector<std::string> args;
        std::string current;
        bool in_quotes = false;
        
        for (char c : input) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ' ' && !in_quotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) args.push_back(current);
        
        return args;
    }
    
    static std::map<std::string, std::string> parse_options(const std::vector<std::string>& args) {
        std::map<std::string, std::string> options;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i].substr(0, 2) == "--") {
                std::string key = args[i].substr(2);
                std::string value = (i + 1 < args.size() && args[i + 1].substr(0, 2) != "--") 
                    ? args[++i] : "true";
                options[key] = value;
            } else if (args[i][0] == '-') {
                std::string key = args[i].substr(1);
                std::string value = (i + 1 < args.size() && args[i + 1][0] != '-')
                    ? args[++i] : "true";
                options[key] = value;
            }
        }
        return options;
    }
};

class CLI {
    std::map<std::string, Command> commands_;
    std::string prompt_{"genie> "};
    bool running_{false};
    std::function<void(const std::string&)> output_;
    
public:
    CLI() {
        output_ = [](const std::string& s) { std::cout << s << std::flush; };
        register_builtin_commands();
    }
    
    void set_prompt(const std::string& prompt) { prompt_ = prompt; }
    void set_output(std::function<void(const std::string&)> out) { output_ = out; }
    
    void register_command(const Command& cmd) {
        commands_[cmd.name] = cmd;
    }
    
    void register_command(const std::string& name, 
                         const std::string& desc,
                         const std::string& usage,
                         std::function<std::string(const std::vector<std::string>&)> handler) {
        commands_[name] = {name, desc, usage, handler};
    }
    
    std::string execute(const std::string& input) {
        auto args = CommandParser::parse(input);
        if (args.empty()) return "";
        
        std::string cmd_name = args[0];
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
        
        if (commands_.count(cmd_name)) {
            try {
                return commands_[cmd_name].handler(args);
            } catch (const std::exception& e) {
                return "Error: " + std::string(e.what()) + "\n";
            }
        }
        
        return "Unknown command: " + cmd_name + ". Type 'help' for available commands.\n";
    }
    
    void run() {
        running_ = true;
        output_("\n=== Metis Genie Platform CLI v2.25.0 ===\n");
        output_("Type 'help' for available commands, 'exit' to quit.\n\n");
        
        std::string line;
        while (running_) {
            output_(prompt_);
            if (!std::getline(std::cin, line)) break;
            
            if (line.empty()) continue;
            
            std::string result = execute(line);
            if (!result.empty()) {
                output_(result);
                if (result.back() != '\n') output_("\n");
            }
        }
    }
    
    void stop() { running_ = false; }
    
    std::string help() const {
        std::ostringstream ss;
        ss << "\nAvailable Commands:\n";
        ss << std::string(50, '-') << "\n";
        
        std::vector<std::string> names;
        for (const auto& [name, cmd] : commands_) names.push_back(name);
        std::sort(names.begin(), names.end());
        
        for (const auto& name : names) {
            const auto& cmd = commands_.at(name);
            ss << std::left << std::setw(15) << cmd.name << " - " << cmd.description << "\n";
        }
        ss << std::string(50, '-') << "\n";
        return ss.str();
    }
    
private:
    void register_builtin_commands() {
        register_command("help", "Show available commands", "help [command]",
            [this](const std::vector<std::string>& args) -> std::string {
                if (args.size() > 1 && commands_.count(args[1])) {
                    const auto& cmd = commands_[args[1]];
                    std::ostringstream ss;
                    ss << "\n" << cmd.name << " - " << cmd.description << "\n";
                    ss << "Usage: " << cmd.usage << "\n";
                    return ss.str();
                }
                return help();
            });
        
        register_command("exit", "Exit the CLI", "exit",
            [this](const std::vector<std::string>&) -> std::string {
                stop();
                return "Goodbye!\n";
            });
        
        register_command("quit", "Exit the CLI", "quit",
            [this](const std::vector<std::string>&) -> std::string {
                stop();
                return "Goodbye!\n";
            });
        
        register_command("version", "Show version information", "version",
            [](const std::vector<std::string>&) -> std::string {
                return std::string("Metis Genie Platform Emulator v") + std::string(GENIE_CLI_VERSION) + "\nCopyright 2026 Bennie Shearer\n";
            });
        
        register_command("clear", "Clear the screen", "clear",
            [this](const std::vector<std::string>&) -> std::string {
                output_("\033[2J\033[H");  // ANSI clear screen
                return "";
            });
        
        register_command("echo", "Echo arguments", "echo <text>",
            [](const std::vector<std::string>& args) -> std::string {
                std::ostringstream ss;
                for (size_t i = 1; i < args.size(); ++i) {
                    ss << args[i];
                    if (i < args.size() - 1) ss << " ";
                }
                return ss.str();
            });
    }
};

// Batch processor for script files
class BatchProcessor {
    CLI& cli_;
    
public:
    explicit BatchProcessor(CLI& cli) : cli_(cli) {}
    
    std::string execute_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return "Error: Cannot open file: " + filename + "\n";
        
        std::ostringstream result;
        std::string line;
        int line_num = 0;
        
        while (std::getline(file, line)) {
            ++line_num;
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            
            result << ">>> " << line << "\n";
            result << cli_.execute(line);
        }
        
        return result.str();
    }
    
    std::string execute_script(const std::string& script) {
        std::istringstream ss(script);
        std::string line;
        std::ostringstream result;
        
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#') continue;
            result << cli_.execute(line);
        }
        
        return result.str();
    }
};

// Menu system for interactive mode
class Menu {
    std::string title_;
    std::vector<std::pair<std::string, std::function<void()>>> items_;
    
public:
    explicit Menu(const std::string& title) : title_(title) {}
    
    void add_item(const std::string& label, std::function<void()> action) {
        items_.emplace_back(label, action);
    }
    
    void display() const {
        std::cout << "\n=== " << title_ << " ===\n";
        for (size_t i = 0; i < items_.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << items_[i].first << "\n";
        }
        std::cout << "  0. Back/Exit\n";
        std::cout << "\nChoice: ";
    }
    
    void run() {
        while (true) {
            display();
            
            int choice;
            if (!(std::cin >> choice)) {
                std::cin.clear();
                std::cin.ignore(10000, '\n');
                continue;
            }
            std::cin.ignore(10000, '\n');
            
            if (choice == 0) break;
            if (choice > 0 && choice <= static_cast<int>(items_.size())) {
                items_[choice - 1].second();
            } else {
                std::cout << "Invalid choice.\n";
            }
        }
    }
};

// Progress indicator for long operations
class ProgressBar {
    int width_;
    std::string prefix_;
    
public:
    ProgressBar(int width = 50, const std::string& prefix = "Progress: ")
        : width_(width), prefix_(prefix) {}
    
    void update(double progress) const {
        int filled = static_cast<int>(progress * width_);
        
        std::cout << "\r" << prefix_ << "[";
        for (int i = 0; i < width_; ++i) {
            if (i < filled) std::cout << "=";
            else if (i == filled) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100) << "%" << std::flush;
    }
    
    void complete() const {
        update(1.0);
        std::cout << "\n";
    }
};

// Table formatter for CLI output
class TableFormatter {
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<int> widths_;
    
public:
    void set_headers(const std::vector<std::string>& headers) {
        headers_ = headers;
        widths_.resize(headers.size(), 0);
        for (size_t i = 0; i < headers.size(); ++i) {
            widths_[i] = static_cast<int>(headers[i].length());
        }
    }
    
    void add_row(const std::vector<std::string>& row) {
        rows_.push_back(row);
        for (size_t i = 0; i < std::min(row.size(), widths_.size()); ++i) {
            widths_[i] = std::max(widths_[i], static_cast<int>(row[i].length()));
        }
    }
    
    std::string render() const {
        std::ostringstream ss;
        
        // Header separator
        auto separator = [this, &ss]() {
            ss << "+";
            for (int w : widths_) ss << std::string(w + 2, '-') << "+";
            ss << "\n";
        };
        
        // Render header
        separator();
        ss << "|";
        for (size_t i = 0; i < headers_.size(); ++i) {
            ss << " " << std::left << std::setw(widths_[i]) << headers_[i] << " |";
        }
        ss << "\n";
        separator();
        
        // Render rows
        for (const auto& row : rows_) {
            ss << "|";
            for (size_t i = 0; i < widths_.size(); ++i) {
                std::string val = (i < row.size()) ? row[i] : "";
                ss << " " << std::left << std::setw(widths_[i]) << val << " |";
            }
            ss << "\n";
        }
        separator();
        
        return ss.str();
    }
    
    void clear() {
        rows_.clear();
    }
};

} // namespace cli
} // namespace genie
#endif // GENIE_CLI_HPP
