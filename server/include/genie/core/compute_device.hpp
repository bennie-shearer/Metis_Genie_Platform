/**
 * @file compute_device.hpp
 * @brief Compute device abstraction for future GPU/accelerator support
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * This header provides the abstraction layer for compute devices.
 * Currently implements CPU-only computation, but the API is designed
 * to support future GPU acceleration (CUDA, OpenCL, SYCL) without
 * breaking changes.
 *
 * Future Implementation Path:
 * - v3.x: CUDA support for NVIDIA GPUs
 * - v3.x: OpenCL support for cross-vendor GPUs
 * - v4.x: SYCL support for heterogeneous computing
 *
 * Usage:
 *   auto ctx = ComputeContext::create(DeviceType::AUTO);
 *   auto result = ctx.parallel_reduce(data, std::plus<>{});
 */

#pragma once
#ifndef GENIE_CORE_COMPUTE_DEVICE_HPP
#define GENIE_CORE_COMPUTE_DEVICE_HPP

#include <algorithm>
#include <functional>
#include <future>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace genie::compute {

// =========================================================================
// Device Types
// =========================================================================

/**
 * @brief Supported compute device types
 * 
 * Current: Only CPU is implemented
 * Future: GPU_CUDA, GPU_OPENCL, GPU_SYCL will be added
 */
enum class DeviceType {
    CPU,            ///< CPU computation (current default)
    GPU_CUDA,       ///< NVIDIA CUDA (future)
    GPU_OPENCL,     ///< OpenCL cross-vendor (future)
    GPU_SYCL,       ///< SYCL heterogeneous (future)
    AUTO            ///< Automatic selection (currently selects CPU)
};

/**
 * @brief Convert DeviceType to string
 */
inline std::string device_type_to_string(DeviceType type) {
    switch (type) {
        case DeviceType::CPU: return "CPU";
        case DeviceType::GPU_CUDA: return "GPU_CUDA";
        case DeviceType::GPU_OPENCL: return "GPU_OPENCL";
        case DeviceType::GPU_SYCL: return "GPU_SYCL";
        case DeviceType::AUTO: return "AUTO";
        default: return "UNKNOWN";
    }
}

// =========================================================================
// Device Capabilities
// =========================================================================

/**
 * @brief Device capability information
 */
struct DeviceInfo {
    DeviceType type{DeviceType::CPU};
    std::string name;
    std::string vendor;
    size_t memory_bytes{0};
    size_t max_threads{0};
    bool available{false};
    
    // GPU-specific (for future use)
    int compute_capability_major{0};
    int compute_capability_minor{0};
    size_t max_shared_memory{0};
    int multiprocessor_count{0};
};

/**
 * @brief Query available compute devices
 * @return Vector of available devices (currently only CPU)
 */
inline std::vector<DeviceInfo> enumerate_devices() {
    std::vector<DeviceInfo> devices;
    
    // CPU is always available
    DeviceInfo cpu;
    cpu.type = DeviceType::CPU;
    cpu.name = "CPU";
    cpu.vendor = "Host";
    cpu.max_threads = std::thread::hardware_concurrency();
    cpu.available = true;
    devices.push_back(cpu);
    
    // Future: Query CUDA devices
    // Future: Query OpenCL devices
    // Future: Query SYCL devices
    
    return devices;
}

/**
 * @brief Check if a specific device type is available
 */
inline bool is_device_available(DeviceType type) {
    if (type == DeviceType::CPU || type == DeviceType::AUTO) {
        return true;
    }
    // Future: Check for GPU availability
    return false;
}

// =========================================================================
// Compute Context
// =========================================================================

/**
 * @brief Compute context for device-agnostic computation
 * 
 * Provides a unified interface for parallel computation that can
 * transparently switch between CPU and GPU implementations.
 */
class ComputeContext {
    DeviceType device_type_{DeviceType::CPU};
    size_t num_threads_{std::thread::hardware_concurrency()};

public:
    /**
     * @brief Create a compute context
     * @param preferred Preferred device type (AUTO selects best available)
     * @return ComputeContext configured for the device
     */
    static ComputeContext create(DeviceType preferred = DeviceType::AUTO) {
        ComputeContext ctx;
        
        if (preferred == DeviceType::AUTO) {
            // Future: Check for GPU availability and prefer it
            // Currently: Always use CPU
            ctx.device_type_ = DeviceType::CPU;
        } else if (is_device_available(preferred)) {
            ctx.device_type_ = preferred;
        } else {
            // Fallback to CPU
            ctx.device_type_ = DeviceType::CPU;
        }
        
        return ctx;
    }
    
    /** @brief Get active device type */
    [[nodiscard]] DeviceType device() const { return device_type_; }
    
    /** @brief Get device name string */
    [[nodiscard]] std::string device_name() const {
        return device_type_to_string(device_type_);
    }
    
    /** @brief Get number of parallel execution units */
    [[nodiscard]] size_t parallelism() const { return num_threads_; }
    
    /** @brief Set number of threads (CPU only) */
    void set_threads(size_t n) { num_threads_ = n > 0 ? n : 1; }
    
    // =====================================================================
    // Parallel Operations
    // =====================================================================
    
    /**
     * @brief Parallel map operation
     * @param input Input data
     * @param func Transformation function
     * @return Transformed data
     */
    template<typename T, typename F>
    std::vector<T> parallel_map(const std::vector<T>& input, F func) {
        std::vector<T> output(input.size());
        
        if (device_type_ == DeviceType::CPU) {
            // CPU implementation with thread pool
            size_t chunk_size = (input.size() + num_threads_ - 1) / num_threads_;
            std::vector<std::future<void>> futures;
            
            for (size_t t = 0; t < num_threads_; ++t) {
                size_t start = t * chunk_size;
                size_t end = std::min(start + chunk_size, input.size());
                
                if (start >= input.size()) break;
                
                futures.push_back(std::async(std::launch::async, [&, start, end]() {
                    for (size_t i = start; i < end; ++i) {
                        output[i] = func(input[i]);
                    }
                }));
            }
            
            for (auto& f : futures) f.get();
        }
        // Future: GPU implementations
        
        return output;
    }
    
    /**
     * @brief Parallel reduce operation
     * @param input Input data
     * @param op Binary operation
     * @param init Initial value
     * @return Reduced value
     */
    template<typename T, typename BinaryOp>
    T parallel_reduce(const std::vector<T>& input, BinaryOp op, T init = T{}) {
        if (input.empty()) return init;
        
        if (device_type_ == DeviceType::CPU) {
            // CPU implementation
            size_t chunk_size = (input.size() + num_threads_ - 1) / num_threads_;
            std::vector<std::future<T>> futures;
            
            for (size_t t = 0; t < num_threads_; ++t) {
                size_t start = t * chunk_size;
                size_t end = std::min(start + chunk_size, input.size());
                
                if (start >= input.size()) break;
                
                futures.push_back(std::async(std::launch::async, [&, start, end]() {
                    return std::accumulate(input.begin() + start, 
                                          input.begin() + end, 
                                          T{}, op);
                }));
            }
            
            T result = init;
            for (auto& f : futures) {
                result = op(result, f.get());
            }
            return result;
        }
        // Future: GPU implementations
        
        return init;
    }
    
    /**
     * @brief Parallel for-each operation
     * @param begin Start index
     * @param end End index
     * @param func Function to apply
     */
    template<typename F>
    void parallel_for(size_t begin, size_t end, F func) {
        if (begin >= end) return;
        
        if (device_type_ == DeviceType::CPU) {
            size_t total = end - begin;
            size_t chunk_size = (total + num_threads_ - 1) / num_threads_;
            std::vector<std::future<void>> futures;
            
            for (size_t t = 0; t < num_threads_; ++t) {
                size_t start = begin + t * chunk_size;
                size_t stop = std::min(start + chunk_size, end);
                
                if (start >= end) break;
                
                futures.push_back(std::async(std::launch::async, [&func, start, stop]() {
                    for (size_t i = start; i < stop; ++i) {
                        func(i);
                    }
                }));
            }
            
            for (auto& f : futures) f.get();
        }
        // Future: GPU implementations
    }
};

// =========================================================================
// Matrix Operations (Future GPU Acceleration Target)
// =========================================================================

/**
 * @brief Simple matrix class for future GPU-accelerated linear algebra
 */
template<typename T = double>
class Matrix {
    std::vector<T> data_;
    size_t rows_{0};
    size_t cols_{0};

public:
    Matrix() = default;
    Matrix(size_t rows, size_t cols, T init = T{}) 
        : data_(rows * cols, init), rows_(rows), cols_(cols) {}
    
    [[nodiscard]] size_t rows() const { return rows_; }
    [[nodiscard]] size_t cols() const { return cols_; }
    [[nodiscard]] size_t size() const { return data_.size(); }
    
    T& operator()(size_t i, size_t j) { return data_[i * cols_ + j]; }
    const T& operator()(size_t i, size_t j) const { return data_[i * cols_ + j]; }
    
    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }
    
    /**
     * @brief Matrix multiplication (future GPU target)
     */
    static Matrix multiply(const Matrix& a, const Matrix& b, 
                          ComputeContext& ctx = ComputeContext::create()) {
        if (a.cols() != b.rows()) {
            throw std::invalid_argument("Matrix dimensions incompatible");
        }
        
        Matrix result(a.rows(), b.cols());
        
        ctx.parallel_for(0, a.rows(), [&](size_t i) {
            for (size_t j = 0; j < b.cols(); ++j) {
                T sum = T{};
                for (size_t k = 0; k < a.cols(); ++k) {
                    sum += a(i, k) * b(k, j);
                }
                result(i, j) = sum;
            }
        });
        
        return result;
    }
    
    /**
     * @brief Matrix transpose
     */
    [[nodiscard]] Matrix transpose() const {
        Matrix result(cols_, rows_);
        for (size_t i = 0; i < rows_; ++i) {
            for (size_t j = 0; j < cols_; ++j) {
                result(j, i) = (*this)(i, j);
            }
        }
        return result;
    }
};

// =========================================================================
// Monte Carlo Simulation Support
// =========================================================================

/**
 * @brief Configuration for Monte Carlo simulations
 */
struct MonteCarloConfig {
    size_t num_paths{10000};
    size_t num_steps{252};
    unsigned seed{42};
    DeviceType device{DeviceType::AUTO};
};

/**
 * @brief Run parallel Monte Carlo simulation
 * @param config Simulation configuration
 * @param path_generator Function to generate one path
 * @return Vector of simulation results
 */
template<typename T, typename PathGen>
std::vector<T> monte_carlo_simulate(const MonteCarloConfig& config, PathGen path_generator) {
    auto ctx = ComputeContext::create(config.device);
    std::vector<T> results(config.num_paths);
    
    ctx.parallel_for(0, config.num_paths, [&](size_t i) {
        // Each path gets a unique seed
        results[i] = path_generator(config.seed + static_cast<unsigned>(i), config.num_steps);
    });
    
    return results;
}

} // namespace genie::compute

#endif // GENIE_CORE_COMPUTE_DEVICE_HPP
