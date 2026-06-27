// Metis Genie Platform v5.5.11
/**
 * Metis Genie Platform v5.5.11 - Connection Manager
 * HTTP client for REST API communication
 * @author Bennie Shearer  @copyright 2026
 *
 * REST API endpoints (net/rest_api.hpp):
 *   GET  /api/v1/health      - Health check (no auth)
 *   POST /api/v1/auth/login  - Authenticate, returns Bearer token
 *   POST /api/v1/auth/logout - End session
 *   GET  /api/v1/status      - System status
 *   GET  /api/v1/portfolios  - Portfolio list (paginated)
 *   GET  /api/v1/positions   - Position data (paginated)
 *   GET  /api/v1/risk        - Risk metrics
 *   GET  /api/v1/market      - Market data
 *   GET  /api/v1/orders      - Order list
 *   POST /api/v1/orders      - Submit order
 *
 * Connection lifecycle:
 *   - Health check runs every 30s once connected.
 *   - If a check fails, the health timer is STOPPED and reconnect takes over.
 *   - Reconnect retries with exponential backoff (2s base, 30s cap, 5 attempts).
 *   - On success, reconnect stops and the health timer restarts.
 *   - The two loops never run concurrently.
 */
const Connection = {
    baseUrl: '',
    apiVersion: 'v1',
    token: null,
    user: null,
    connected: false,
    demoMode: false,
    timeout: 10000,

    // Health check (only runs when connected, stops during reconnect)
    healthTimer: null,
    healthInterval: 30000,

    // Reconnect (only runs when disconnected, stops on success)
    reconnecting: false,
    reconnectAttempts: 0,
    reconnectMax: 5,           // reduced from 10 -- 5 attempts covers ~2 minutes
    reconnectBaseDelay: 2000,
    reconnectMaxDelay: 30000,  // reduced from 60s -- recover faster
    reconnectTimer: null,

    sessionStart: null,
    sessionWarnTimer: null,
    sessionMaxMs: 24 * 3600000,
    sessionWarnMs: 23 * 3600000,

    api(path) { return this.baseUrl + '/api/' + this.apiVersion + path; },

    init() {
        const configUrl = window.Config?.get('server.url') || 'http://localhost:8080';
        this.baseUrl = U.store.get('serverUrl', configUrl);
        const configTimeout = window.Config?.get('server.timeout_ms') || 30000;
        this.timeout = parseInt(U.store.get('apiTimeout', configTimeout / 1000)) * 1000;
        U.log.info('Conn', `Initialized: url=${this.baseUrl}, timeout=${this.timeout}ms`);

        const saved = U.store.get('session', null);
        if (saved) {
            try {
                const s = JSON.parse(saved);
                this.token = s.token; this.user = s.user;
                this.demoMode = s.demo; this.sessionStart = s.sessionStart;
                if (this.token) sessionStorage.setItem('genie_token', this.token);
                if (this.demoMode) { this.connected = false; return true; }
                // Don't mark connected yet -- wait for first health check to confirm
                this.connected = false;
                this.startReconnect();   // verify server is up before declaring connected
                this.startSessionTimer();
                return true;
            } catch (_) { console.debug('Corrupt session data:', _); }
        }
        return false;
    },

    async testConnection(url) {
        url = url || this.baseUrl;
        try {
            const res = await this.fetchWithTimeout(url + '/api/' + this.apiVersion + '/health');
            if (res.ok) {
                const d = await res.json();
                U.log.info('Conn', 'Health OK: v' + (d.version || '?'));
                return { ok: true, version: d.version || '?', uptime: d.uptime || 0, routes: d.routes || 0 };
            }
            U.log.warn('Conn', 'Health failed: HTTP ' + res.status);
            return { ok: false, error: 'Server returned ' + res.status };
        } catch (e) {
            U.log.warn('Conn', 'Health error: ' + (e.message || 'Failed'));
            return { ok: false, error: e.name === 'AbortError' ? 'Timed out' : (e.message || 'Failed') };
        }
    },

    async login(username, password, serverUrl) {
        if (serverUrl) { this.baseUrl = serverUrl; U.store.set('serverUrl', serverUrl); }
        U.log.info('Conn', 'Login attempt: ' + username + ' @ ' + this.baseUrl);
        try {
            const res = await this.fetchWithTimeout(this.api('/auth/login'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password })
            });
            if (res.ok) {
                const d = await res.json();
                this.token = d.token || 'session-' + Date.now();
                this.user = { name: d.name || username, role: d.role || 'User' };
                this.connected = true; this.demoMode = false;
                this.sessionStart = Date.now();
                this.saveSession(); this.startHealthCheck(); this.startSessionTimer();
                this.fetchServerConfig();
                U.log.info('Conn', 'Login OK: ' + this.user.name + ' (' + this.user.role + ')');
                return { ok: true, user: this.user };
            }
            const err = await res.json().catch(() => ({}));
            U.log.warn('Conn', 'Login failed: ' + (err.error || 'HTTP ' + res.status));
            return { ok: false, error: err.error || 'Authentication failed' };
        } catch (e) {
            U.log.error('Conn', 'Login error: ' + (e.message || 'Failed'));
            return { ok: false, error: e.name === 'AbortError' ? 'Timed out' : (e.message || 'Failed') };
        }
    },

    loginDemo(username) {
        this.token = 'demo-' + Date.now();
        this.user = { name: username || 'Demo User', role: 'Demo Mode' };
        this.connected = false; this.demoMode = true;
        this.sessionStart = Date.now(); this.saveSession();
        U.log.info('Conn', 'Demo mode: ' + this.user.name);
        return { ok: true, user: this.user };
    },

    logout() {
        U.log.info('Conn', 'Logout: ' + (this.user ? this.user.name : 'unknown'));
        if (this.connected && this.token) {
            this.fetchWithTimeout(this.api('/auth/logout'), { method: 'POST', headers: this.authHeaders() }).catch(() => {});
        }
        this.stopHealthCheck(); this.stopReconnect(); this.stopSessionTimer();
        this.token = null; this.user = null;
        this.connected = false; this.demoMode = false;
        this.reconnectAttempts = 0; this.reconnecting = false;
        U.store.remove('session');
        sessionStorage.removeItem('genie_token');
    },

    // ---- Server Config Sync ----
    async fetchServerConfig() {
        try {
            const res = await this.fetchWithTimeout(this.api('/config'));
            if (res.ok) {
                const cfg = await res.json();
                this.serverConfig = cfg;
                U.log.info('Conn', 'Server config: v' + (cfg.version || '?'));
                if (cfg.session_timeout_minutes && window.Config) {
                    window.Config.server_session_timeout = cfg.session_timeout_minutes;
                }
            }
        } catch (_) {
            U.log.debug('Conn', 'Server config fetch skipped');
        }
    },

    // ---- Registration ----
    async register(username, password, email, displayName, serverUrl) {
        if (serverUrl) { this.baseUrl = serverUrl; U.store.set('serverUrl', serverUrl); }
        U.log.info('Conn', 'Register attempt: ' + username + ' @ ' + this.baseUrl);
        try {
            const res = await this.fetchWithTimeout(this.api('/auth/register'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password, email: email || '', display_name: displayName || '' })
            });
            const d = await res.json();
            if (res.ok) {
                this.token = d.token;
                this.user = { name: d.username || username, role: d.role || 'Viewer' };
                this.connected = true; this.demoMode = false;
                this.sessionStart = Date.now();
                this.saveSession(); this.startHealthCheck(); this.startSessionTimer();
                U.log.info('Conn', 'Registered: ' + this.user.name);
                return { ok: true, user: this.user };
            }
            U.log.warn('Conn', 'Register failed: ' + (d.error || 'HTTP ' + res.status));
            return { ok: false, error: d.error || 'Registration failed' };
        } catch (e) {
            U.log.error('Conn', 'Register error: ' + (e.message || 'Failed'));
            return { ok: false, error: e.name === 'AbortError' ? 'Timed out' : (e.message || 'Failed') };
        }
    },

    // ---- User Profile ----
    async getProfile() {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/users/me'), { headers: this.authHeaders() });
            if (res.ok) { return { ok: true, data: await res.json() }; }
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            const err = await res.json().catch(() => ({}));
            return { ok: false, error: err.error || 'Failed to load profile' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    async updateProfile(displayName, email) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/users/me'), {
                method: 'PUT',
                headers: { ...this.authHeaders(), 'Content-Type': 'application/json' },
                body: JSON.stringify({ display_name: displayName, email })
            });
            if (res.ok) {
                if (displayName) this.user.name = displayName;
                this.saveSession(); this.updateUserUI();
                return { ok: true };
            }
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            const err = await res.json().catch(() => ({}));
            return { ok: false, error: err.error || 'Failed to update profile' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    async changePassword(oldPassword, newPassword) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/users/me/password'), {
                method: 'POST',
                headers: { ...this.authHeaders(), 'Content-Type': 'application/json' },
                body: JSON.stringify({ old_password: oldPassword, new_password: newPassword })
            });
            if (res.ok) return { ok: true };
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            const err = await res.json().catch(() => ({}));
            return { ok: false, error: err.error || 'Failed to change password' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    // ---- Admin User Management ----
    async getUsers() {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/admin/users'), { headers: this.authHeaders() });
            if (res.ok) return { ok: true, data: await res.json() };
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            if (res.status === 403) return { ok: false, error: 'Admin access required' };
            return { ok: false, error: 'Failed to load users' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    async getUser(username) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/admin/users/' + encodeURIComponent(username)), { headers: this.authHeaders() });
            if (res.ok) return { ok: true, data: await res.json() };
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            if (res.status === 403) return { ok: false, error: 'Admin access required' };
            if (res.status === 404) return { ok: false, error: 'User not found' };
            return { ok: false, error: 'Failed to load user' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    async updateUser(username, data) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/admin/users/' + encodeURIComponent(username)), {
                method: 'PUT',
                headers: { ...this.authHeaders(), 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            });
            if (res.ok) return { ok: true };
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            if (res.status === 403) return { ok: false, error: 'Admin access required' };
            const err = await res.json().catch(() => ({}));
            return { ok: false, error: err.error || 'Failed to update user' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    async deleteUser(username) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api('/admin/users/' + encodeURIComponent(username)), {
                method: 'DELETE',
                headers: this.authHeaders()
            });
            if (res.ok) return { ok: true };
            if (res.status === 401) { this.onAuthExpired(); return { ok: false, error: 'Session expired' }; }
            if (res.status === 403) return { ok: false, error: 'Admin access required' };
            const err = await res.json().catch(() => ({}));
            return { ok: false, error: err.error || 'Failed to delete user' };
        } catch (e) { return { ok: false, error: e.message || 'Failed' }; }
    },

    isAdmin() { return this.user && this.user.role === 'Administrator'; },

    async get(path, params) {
        if (this.demoMode) return { ok: false, demo: true };
        let url = this.api(path);
        if (params) {
            const qs = Object.entries(params).filter(([,v]) => v != null).map(([k,v]) => k + '=' + encodeURIComponent(v)).join('&');
            if (qs) url += '?' + qs;
        }
        try {
            const res = await this.fetchWithTimeout(url, { headers: this.authHeaders() });
            if (res.ok) { U.log.debug('Conn', 'GET ' + path + ' -> 200'); return { ok: true, data: await res.json() }; }
            U.log.warn('Conn', 'GET ' + path + ' -> ' + res.status);
            if (res.status === 401) this.onAuthExpired();
            return { ok: false, status: res.status };
        } catch (e) { U.log.error('Conn', 'GET ' + path + ' error: ' + e.message); return { ok: false, error: e.message }; }
    },

    async post(path, body) {
        if (this.demoMode) return { ok: false, demo: true };
        try {
            const res = await this.fetchWithTimeout(this.api(path), {
                method: 'POST', headers: { ...this.authHeaders(), 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
            if (res.ok) { U.log.debug('Conn', 'POST ' + path + ' -> OK'); return { ok: true, data: await res.json() }; }
            U.log.warn('Conn', 'POST ' + path + ' -> ' + res.status);
            return { ok: false, status: res.status };
        } catch (e) { U.log.error('Conn', 'POST ' + path + ' error: ' + e.message); return { ok: false, error: e.message }; }
    },

    // ---- Health Check (only runs while connected, stops during reconnect) ----
    startHealthCheck() {
        this.stopHealthCheck();
        this.healthTimer = setInterval(async () => {
            const r = await this.testConnection();
            if (!r.ok) {
                // Connection lost -- stop health check, start reconnect
                this.connected = false;
                this.stopHealthCheck();
                this.updateStatusUI();
                App.toast('error', 'Connection lost');
                this.startReconnect();
            }
            // If still ok, nothing to do -- stay connected
        }, this.healthInterval);
    },
    stopHealthCheck() {
        if (this.healthTimer) { clearInterval(this.healthTimer); this.healthTimer = null; }
    },

    // ---- Reconnect (runs until success or max attempts, then stops) ----
    // Health check is stopped while reconnect is active -- the two never overlap.
    startReconnect() {
        if (this.reconnecting) return;
        this.reconnecting = true;
        this.reconnectAttempts = 0;
        if (this.connected) {
            // Called from init() to verify a restored session -- no toast needed
        } else {
            U.log.warn('Conn', 'Starting reconnect');
        }
        this.updateStatusUI();
        this._scheduleReconnect(0);  // first attempt immediately
    },

    _scheduleReconnect(delay) {
        if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
        this.reconnectTimer = setTimeout(() => this._attemptReconnect(), delay);
    },

    async _attemptReconnect() {
        if (!this.reconnecting) return;
        this.reconnectAttempts++;

        if (this.reconnectAttempts > this.reconnectMax) {
            // Exhausted retries -- show stopped page
            U.log.error('Conn', 'Max reconnect attempts reached');
            this.reconnecting = false;
            this.connected = false;
            this.updateStatusUI();
            this.showServerStopped();
            return;
        }

        U.log.info('Conn', 'Reconnect attempt ' + this.reconnectAttempts + '/' + this.reconnectMax);
        this.updateStatusUI();

        const r = await this.testConnection();
        if (r.ok) {
            // Success -- restore connected state
            const wasReconnecting = this.reconnectAttempts > 1 || this.connected === false;
            this.connected = true;
            this.reconnecting = false;
            this.reconnectAttempts = 0;
            if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
            this.updateStatusUI();
            if (wasReconnecting && !this._initRestore) {
                App.toast('success', 'Reconnected');
            }
            this._initRestore = false;
            // Restart health check now that we're connected
            this.startHealthCheck();
            return;
        }

        // Still failing -- schedule next attempt with exponential backoff
        const delay = Math.min(
            this.reconnectBaseDelay * Math.pow(2, this.reconnectAttempts - 1),
            this.reconnectMaxDelay
        );
        this._scheduleReconnect(delay);
    },

    stopReconnect() {
        this.reconnecting = false;
        this.reconnectAttempts = 0;
        if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    },

    // ---- Session Timeout ----
    startSessionTimer() {
        this.stopSessionTimer();
        if (!this.sessionStart) return;
        const remain = this.sessionWarnMs - (Date.now() - this.sessionStart);
        if (remain <= 0) { this.showSessionWarning(); return; }
        this.sessionWarnTimer = setTimeout(() => this.showSessionWarning(), remain);
    },
    stopSessionTimer() { if (this.sessionWarnTimer) { clearTimeout(this.sessionWarnTimer); this.sessionWarnTimer = null; } },
    showSessionWarning() { const w = U.$('#sessionWarn'); if (w) w.classList.add('show'); },
    hideSessionWarning() { const w = U.$('#sessionWarn'); if (w) w.classList.remove('show'); },
    renewSession() {
        this.sessionStart = Date.now(); this.saveSession();
        this.hideSessionWarning(); this.startSessionTimer();
        App.toast('info', 'Session renewed');
    },

    // ---- UI ----
    showServerStopped() {
        this.stopHealthCheck(); this.stopReconnect(); this.stopSessionTimer();
        document.documentElement.innerHTML =
            '<head><title>Metis Genie Platform - Server Stopped</title></head>' +
            '<body style="margin:0;padding:0;display:flex;align-items:center;justify-content:center;height:100vh;' +
            'background:#1a1b26;color:#a9b1d6;font-family:system-ui,sans-serif;">' +
            '<div style="text-align:center;">' +
            '<div style="margin-bottom:20px;"><img src="img/logo.svg" alt="Metis Genie Platform" style="width:96px;height:96px;border-radius:16px;opacity:0.8;"></div>' +
            '<h1 style="margin:0 0 8px;font-size:24px;">Server Stopped</h1>' +
            '<p style="margin:0 0 24px;opacity:0.7;">Metis Genie Platform has been shut down.</p>' +
            '<p style="margin:0;opacity:0.5;font-size:14px;">Restart the server and reload this page to continue.</p>' +
            '</div></body>';
    },

    updateStatusUI() {
        const el = U.$('#connStatus');
        if (el) {
            if (this.demoMode)       el.innerHTML = '<span class="status-dot offline"></span>Demo Mode';
            else if (this.reconnecting) el.innerHTML = '<span class="status-dot reconnecting"></span>Retry ' + this.reconnectAttempts + '/' + this.reconnectMax;
            else if (this.connected) el.innerHTML = '<span class="status-dot online"></span>Connected';
            else                     el.innerHTML = '<span class="status-dot offline"></span>Disconnected';
        }
        const us = U.$('#userStatus'); if (us && this.user) us.textContent = this.user.name;
        const sc = U.$('#settingsConnStatus');
        if (sc) sc.textContent = this.connected ? 'Connected' : this.demoMode ? 'Demo Mode' : this.reconnecting ? 'Reconnecting...' : 'Disconnected';
        const ab = U.$('#aboutServer');
        if (ab) ab.textContent = this.connected ? this.baseUrl : this.demoMode ? 'Demo (local data)' : 'Not connected';
    },
    updateUserUI() {
        if (!this.user) return;
        const ini = this.user.name.split(' ').map(w => w[0]).join('').toUpperCase().slice(0, 2);
        const av = U.$('#userAvatar'); if (av) av.textContent = ini || 'U';
        const nm = U.$('#userName'); if (nm) nm.textContent = this.user.name;
        const rl = U.$('#userRole'); if (rl) rl.textContent = this.user.role;
    },
    onAuthExpired() { App.toast('error', 'Session expired'); this.logout(); App.showLogin(); },
    saveSession() {
        U.store.set('session', JSON.stringify({ token: this.token, user: this.user, demo: this.demoMode, sessionStart: this.sessionStart }));
        if (this.token) sessionStorage.setItem('genie_token', this.token);
        else sessionStorage.removeItem('genie_token');
    },
    authHeaders() { return this.token ? { 'Authorization': 'Bearer ' + this.token } : {}; },
    fetchWithTimeout(url, opts = {}) {
        const c = new AbortController();
        const t = setTimeout(() => c.abort(), this.timeout);
        return fetch(url, { ...opts, signal: c.signal }).finally(() => clearTimeout(t));
    },

    // === Sub-Page Session Restore ===
    initSubPage() {
        const configUrl = window.Config?.get('server.url') || 'http://localhost:8080';
        this.baseUrl = U.store.get('serverUrl', configUrl);
        const configTimeout = window.Config?.get('server.timeout_ms') || 30000;
        this.timeout = parseInt(U.store.get('apiTimeout', configTimeout / 1000)) * 1000;
        const saved = U.store.get('session', null);
        if (saved) {
            try {
                const s = JSON.parse(saved);
                this.token = s.token; this.user = s.user;
                this.demoMode = s.demo; this.connected = !s.demo;
                return true;
            } catch (_) { console.debug('Corrupt session data:', _); }
        }
        return false;
    },

    // === Extended API Methods ===
    async getAnalytics() {
        if (this.demoMode) return this.demoAnalytics();
        try { const r = await this.fetchWithTimeout(this.api('/analytics'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return this.demoAnalytics(); }
    },
    async getCompliance() {
        if (this.demoMode) return this.demoCompliance();
        try { const r = await this.fetchWithTimeout(this.api('/compliance'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return this.demoCompliance(); }
    },
    async getReporting() {
        if (this.demoMode) return this.demoReporting();
        try { const r = await this.fetchWithTimeout(this.api('/reporting'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return this.demoReporting(); }
    },
    async generateReport(templateId) {
        const r = await this.fetchWithTimeout(this.api('/reporting/generate'), { method: 'POST', headers: { ...this.authHeaders(), 'Content-Type': 'application/json' }, body: JSON.stringify({ template_id: templateId }) });
        return await r.json();
    },
    async getTax() {
        if (this.demoMode) return this.demoTax();
        try { const r = await this.fetchWithTimeout(this.api('/tax'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return this.demoTax(); }
    },
    async getSecurityOverview() {
        if (this.demoMode) return {};
        try { const r = await this.fetchWithTimeout(this.api('/security/overview'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return {}; }
    },
    async getAuditLog() {
        if (this.demoMode) return [];
        try { const r = await this.fetchWithTimeout(this.api('/security/audit'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return []; }
    },
    async getSecuritySessions() {
        if (this.demoMode) return [];
        try { const r = await this.fetchWithTimeout(this.api('/security/sessions'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return []; }
    },
    async getOpsHealth() {
        if (this.demoMode) return {};
        try { const r = await this.fetchWithTimeout(this.api('/operations/health'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return {}; }
    },
    async getOpsBackups() {
        if (this.demoMode) return {};
        try { const r = await this.fetchWithTimeout(this.api('/operations/backups'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return {}; }
    },
    async getOpsJobs() {
        if (this.demoMode) return [];
        try { const r = await this.fetchWithTimeout(this.api('/operations/jobs'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return []; }
    },
    async getComputeInfo() {
        if (this.demoMode) return {};
        try { const r = await this.fetchWithTimeout(this.api('/compute'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return {}; }
    },
    async getDeploymentInfo() {
        if (this.demoMode) return {};
        try { const r = await this.fetchWithTimeout(this.api('/deployment'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return {}; }
    },
    async getBenchmark() {
        if (this.demoMode) return this.demoBenchmark();
        try { const r = await this.fetchWithTimeout(this.api('/benchmark'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return this.demoBenchmark(); }
    },
    async getTransactions() {
        if (this.demoMode) return [];
        try { const r = await this.fetchWithTimeout(this.api('/transactions'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return []; }
    },
    async getAlerts() {
        if (this.demoMode) return [];
        try { const r = await this.fetchWithTimeout(this.api('/alerts'), { headers: this.authHeaders() }); return await r.json(); }
        catch(e) { return []; }
    },
    demoAnalytics() { return { performance: { ytd_return: 18.52, mtd_return: 2.34 }, risk: { sharpe_ratio: 1.85 } }; },
    demoCompliance() { return { status: 'Compliant', rules: [] }; },
    demoReporting() { return { templates: [], scheduled: 0, available: 0 }; },
    demoTax() { return { tax_year: 2026, summary: { net_gain: 0 }, lots: [] }; },
    demoBenchmark() { return { benchmark: 'S&P 500', portfolio_return: 18.52, benchmark_return: 16.80 }; }
};
window.Connection = Connection;
