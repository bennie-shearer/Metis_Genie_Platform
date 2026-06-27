/**
 * @file config.js
 * @brief Client configuration for Metis Genie Platform
 * @version 5.5.11
 * 
 * Loads configuration from config.pson (PSON format) and provides defaults.
 * Must be loaded BEFORE other JS modules.
 */

const Config = (function() {
    'use strict';

    // Default configuration (used if config.pson fails to load)
    const DEFAULTS = {
        server: {
            url: "http://localhost:8080",
            timeout_ms: 30000,
            retry_attempts: 3,
            retry_delay_ms: 1000
        },
        logging: {
            enabled: true,
            level: "DEBUG",      // DEBUG, INFO, WARN, ERROR, NONE
            console: true,
            timestamps: true,
            panel: false,        // Show on-screen log panel
            max_entries: 1000
        },
        ui: {
            theme: "light",      // light, dark
            animations: true,
            auto_refresh_ms: 0,  // 0 = disabled
            date_format: "YYYY-MM-DD",
            number_locale: "en-US",
            currency: "USD"
        },
        auth: {
            remember_credentials: false,
            session_warning_minutes: 5,
            auto_logout_minutes: 60
        },
        demo: {
            enabled: true,
            default_username: "admin",
            default_password: "demo"
        }
    };

    let config = JSON.parse(JSON.stringify(DEFAULTS)); // Deep copy
    let loaded = false;
    let loadError = null;

    // Deep merge helper
    function deepMerge(target, source) {
        for (const key in source) {
            if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
                if (!target[key]) target[key] = {};
                deepMerge(target[key], source[key]);
            } else {
                target[key] = source[key];
            }
        }
        return target;
    }

    // Load config.pson asynchronously
    async function loadConfig() {
        try {
            const response = await fetch('config.pson');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            // PSON: strip // comments and trailing commas before JSON.parse
            // Regex skips // inside quoted strings to avoid mangling URLs like http://
            const raw = await response.text();
            const stripped = raw
                .replace(/"(?:[^"\\]|\\.)*"|\/\/[^\n]*/g, m => m.startsWith('"') ? m : '')
                .replace(/,\s*([}\]])/g, '$1');        // remove trailing commas
            const json = JSON.parse(stripped);
            deepMerge(config, json);
            loaded = true;
            console.log('[CONFIG] Loaded config.pson successfully');
            return true;
        } catch (err) {
            loadError = err;
            console.warn('[CONFIG] Failed to load config.pson, using defaults:', err.message);
            loaded = true; // Mark as loaded even on failure (using defaults)
            return false;
        }
    }

    // Public API
    return {
        // Initialize - call this first
        async init() {
            await loadConfig();
            return this;
        },

        // Get nested config value with dot notation: Config.get('server.url')
        get(path, defaultValue = undefined) {
            const keys = path.split('.');
            let value = config;
            for (const key of keys) {
                if (value === undefined || value === null) {
                    return defaultValue;
                }
                value = value[key];
            }
            return value !== undefined ? value : defaultValue;
        },

        // Set config value at runtime
        set(path, value) {
            const keys = path.split('.');
            let obj = config;
            for (let i = 0; i < keys.length - 1; i++) {
                if (!obj[keys[i]]) obj[keys[i]] = {};
                obj = obj[keys[i]];
            }
            obj[keys[keys.length - 1]] = value;
        },

        // Get entire section
        getSection(section) {
            return config[section] ? { ...config[section] } : {};
        },

        // Check if loaded
        isLoaded() { return loaded; },
        getLoadError() { return loadError; },

        // Get all config (readonly copy)
        getAll() { return JSON.parse(JSON.stringify(config)); },

        // Export config to JSON string
        toJSON() { return JSON.stringify(config, null, 2); },

        // Reset to defaults
        reset() { 
            config = JSON.parse(JSON.stringify(DEFAULTS)); 
        },

        // Save to localStorage for persistence
        saveToStorage() {
            try {
                localStorage.setItem('mg_config', JSON.stringify(config));
                return true;
            } catch (e) {
                console.error('[CONFIG] Failed to save to localStorage:', e);
                return false;
            }
        },

        // Load from localStorage
        loadFromStorage() {
            try {
                const stored = localStorage.getItem('mg_config');
                if (stored) {
                    deepMerge(config, JSON.parse(stored));
                    return true;
                }
            } catch (e) {
                console.error('[CONFIG] Failed to load from localStorage:', e);
            }
            return false;
        },

        // Shortcut accessors for common values
        get serverUrl() { return this.get('server.url'); },
        get timeout() { return this.get('server.timeout_ms'); },
        get logLevel() { return this.get('logging.level'); },
        get theme() { return this.get('ui.theme'); },

        DEFAULTS
    };
})();

// Make available globally
window.Config = Config;
