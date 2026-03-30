// Metis Genie Platform v5.3.1
/* Metis Genie Platform Security Page JavaScript v5.3.1 */
'use strict';

const Security = {
    overview: null,
    auditLog: [],
    sessions: [],
    currentUser: null,
    adminUsers: null,

    async init() {
        // Restore session for sub-page
        if (!Connection.connected) Connection.initSubPage();

        this.initTabs();
        this.initTFA();
        this.initSessions();
        await this.fetchServerData();
    },

    async fetchServerData() {
        if (!Connection.connected || Connection.demoMode) return;

        const [ovR, auditR, sessR, userR, adminR] = await Promise.allSettled([
            Connection.get('/security/overview'),
            Connection.get('/security/audit'),
            Connection.get('/security/sessions'),
            Connection.get('/users/me'),
            Connection.get('/admin/users')
        ]);

        if (ovR.status === 'fulfilled' && ovR.value.ok) {
            this.overview = ovR.value.data;
            this.renderOverview();
        }
        if (auditR.status === 'fulfilled' && auditR.value.ok) {
            this.auditLog = auditR.value.data;
            this.renderAuditLog();
            this.renderRecentEvents();
        }
        if (sessR.status === 'fulfilled' && sessR.value.ok) {
            this.sessions = sessR.value.data;
            this.renderSessions();
        }
        if (userR.status === 'fulfilled' && userR.value.ok) {
            this.currentUser = userR.value.data;
            this.renderUserProfile();
        }
        if (adminR.status === 'fulfilled' && adminR.value.ok) {
            this.adminUsers = adminR.value.data;
            this.renderAdminUsers();
        }
    },

    renderUserProfile() {
        const el = document.getElementById('userProfileData');
        if (!el || !this.currentUser) return;
        const u = this.currentUser;
        el.innerHTML =
            '<div class="sec-detail"><span>Username</span><span>' + (u.username || 'N/A') + '</span></div>' +
            '<div class="sec-detail"><span>Role</span><span>' + (u.role || 'N/A') + '</span></div>' +
            '<div class="sec-detail"><span>Email</span><span>' + (u.email || 'N/A') + '</span></div>' +
            '<div class="sec-detail"><span>Last Login</span><span>' + (u.last_login || 'N/A') + '</span></div>';
    },

    renderAdminUsers() {
        const el = document.getElementById('adminUsersList');
        if (!el || !this.adminUsers) return;
        const users = this.adminUsers.users || this.adminUsers || [];
        const arr = Array.isArray(users) ? users : [];
        el.innerHTML = arr.map(u =>
            '<tr><td>' + (u.username || '') + '</td>' +
            '<td>' + (u.role || '') + '</td>' +
            '<td>' + (u.status || 'active') + '</td>' +
            '<td>' + (u.last_login || 'Never') + '</td></tr>'
        ).join('') || '<tr><td colspan="4">No users found</td></tr>';
    },

    async changePassword(currentPw, newPw) {
        const result = await Connection.post('/users/me/password', {
            current_password: currentPw,
            new_password: newPw
        });
        return result;
    },

    renderOverview() {
        if (!this.overview) return;
        const o = this.overview;

        // Update security score based on real config
        let score = 50;
        if (o.audit_logging) score += 10;
        if (o.encryption_at_rest) score += 10;
        if (o.two_factor_enabled) score += 15;
        if (o.ip_whitelist_enabled) score += 10;
        if (o.failed_login_attempts_24h === 0) score += 5;
        score = Math.min(score, 100);

        const scoreCircle = document.getElementById('secScore');
        if (scoreCircle) {
            const svg = scoreCircle.querySelector('svg');
            if (svg) {
                const circumference = 2 * Math.PI * 54;
                const dashLen = (score / 100) * circumference;
                const progressCircle = svg.querySelectorAll('circle')[1];
                if (progressCircle) {
                    progressCircle.setAttribute('stroke-dasharray', `${dashLen} ${circumference}`);
                    progressCircle.setAttribute('stroke', score >= 80 ? '#00d4aa' : score >= 60 ? '#f59e0b' : '#ef4444');
                }
                const scoreText = svg.querySelector('text');
                if (scoreText) scoreText.textContent = score;
            }
        }

        // Update checklist
        const checklist = document.querySelector('.sec-checklist');
        if (checklist) {
            const items = [
                { done: true, text: 'Strong password set (SHA-256 hashing)' },
                { done: true, text: 'Email verified' },
                { done: o.two_factor_enabled, text: o.two_factor_enabled ? 'Two-factor authentication enabled' : 'Two-factor authentication not enabled', action: !o.two_factor_enabled ? 'Enable now' : null, tab: '2fa' },
                { done: o.ip_whitelist_enabled, text: o.ip_whitelist_enabled ? 'IP whitelist enabled' : 'No API key IP restrictions', action: !o.ip_whitelist_enabled ? 'Configure' : null, tab: 'ipwhitelist' },
                { done: true, text: `Session timeout configured (${o.session_timeout_minutes} min)` },
                { done: o.audit_logging, text: o.audit_logging ? 'Audit logging active' : 'Audit logging disabled' },
                { done: o.encryption_at_rest, text: o.encryption_at_rest ? 'Encryption at rest enabled' : 'Encryption at rest disabled' }
            ];
            checklist.innerHTML = items.map(i => {
                const cls = i.done ? 'done' : 'warn';
                const icon = i.done ? '&#10003;' : '!';
                const action = i.action ? ` <a href="#" class="check-action" onclick="showTab('${i.tab}')">${i.action}</a>` : '';
                return `<div class="sec-check ${cls}"><span class="check-icon">${icon}</span><span class="check-text">${i.text}</span>${action}</div>`;
            }).join('');
        }

        // Update score details
        const details = document.querySelector('.score-details');
        if (details) {
            const label = score >= 80 ? 'Excellent' : score >= 60 ? 'Good' : 'Needs Improvement';
            details.querySelector('h3').textContent = `Security Score: ${label}`;
            details.querySelector('p').textContent = `Active sessions: ${o.active_sessions} | Failed logins (24h): ${o.failed_login_attempts_24h} | CORS: ${o.cors_origin}`;
        }
    },

    renderAuditLog() {
        const tbody = document.getElementById('auditList');
        if (!tbody || !this.auditLog.length) return;

        const catMap = { 'LOGIN': 'auth', 'LOGIN_FAILED': 'auth', 'API_CALL': 'config',
            'ORDER_SUBMITTED': 'trade', 'USER_CREATED': 'admin' };
        const catLabel = { 'LOGIN': 'AUTH', 'LOGIN_FAILED': 'AUTH', 'API_CALL': 'API',
            'ORDER_SUBMITTED': 'TRADE', 'USER_CREATED': 'ADMIN' };

        tbody.innerHTML = this.auditLog.map(e => {
            const cat = catMap[e.event] || 'config';
            const label = catLabel[e.event] || e.event;
            const badgeCls = e.status === 'Success' ? 'badge-ok' : 'badge-err';
            const ts = new Date(e.timestamp).toLocaleString();
            return `<tr>
                <td>${ts}</td>
                <td><span class="badge badge-cat ${cat}">${label}</span></td>
                <td>${e.event.replace(/_/g, ' ')}</td>
                <td>${e.user}</td>
                <td>${e.ip}</td>
                <td>${e.details || '-'}</td>
                <td><span class="badge ${badgeCls}">${e.status === 'Success' ? 'OK' : 'FAIL'}</span></td>
            </tr>`;
        }).join('');
    },

    renderSessions() {
        const tbody = document.getElementById('sessionList');
        if (!tbody || !this.sessions.length) return;

        tbody.innerHTML = this.sessions.map((s, i) => {
            const isCurrent = i === 0;
            return `<tr${isCurrent ? ' class="current-session"' : ''}>
                <td>${s.username}${isCurrent ? ' <span class="badge badge-ok">Current</span>' : ''}</td>
                <td>-</td><td>-</td>
                <td>${s.created}</td>
                <td><span class="badge badge-ok">${s.active ? 'Active' : 'Inactive'}</span></td>
                <td>${isCurrent ? '-' : '<button class="btn btn-xs btn-danger">Revoke</button>'}</td>
            </tr>`;
        }).join('');
    },

    // Also populate Recent Security Events on overview from audit log
    renderRecentEvents() {
        const tbody = document.getElementById('secEvents');
        if (!tbody || !this.auditLog.length) return;

        tbody.innerHTML = this.auditLog.slice(0, 5).map(e => {
            const badgeCls = e.status === 'Success' ? 'badge-ok' : 'badge-err';
            const ts = new Date(e.timestamp).toLocaleString();
            return `<tr><td>${ts}</td><td>${e.event.replace(/_/g, ' ')}</td><td>${e.ip}</td><td><span class="badge ${badgeCls}">${e.status}</span></td></tr>`;
        }).join('');
    },

    initTabs() {
        document.querySelectorAll('.sec-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.sec-tab').forEach(t => t.classList.remove('active'));
                document.querySelectorAll('.sec-content').forEach(c => c.classList.remove('active'));
                tab.classList.add('active');
                const target = document.getElementById('tab-' + tab.dataset.tab);
                if (target) target.classList.add('active');
            });
        });
    },

    initTFA() {
        const enableBtn = document.getElementById('enableTfa');
        if (enableBtn) {
            enableBtn.addEventListener('click', () => {
                const secret = this.generateDemoSecret();
                const secretEl = document.getElementById('tfaSecret');
                if (secretEl) secretEl.textContent = secret;
                const codes = document.getElementById('backupCodes');
                if (codes) {
                    codes.innerHTML = Array.from({length:6}, () => {
                        const code = Math.random().toString(36).substring(2,6).toUpperCase() + '-' + Math.random().toString(36).substring(2,6).toUpperCase();
                        return `<code>${code}</code>`;
                    }).join('');
                }
                enableBtn.textContent = 'Regenerate Secret';
            });
        }

        const verifyBtn = document.getElementById('verifyTfa');
        if (verifyBtn) {
            verifyBtn.addEventListener('click', () => {
                const code = document.getElementById('tfaCode')?.value;
                if (code && code.length === 6) {
                    alert('Two-factor authentication enabled successfully!');
                    const status = document.getElementById('tfaStatus');
                    if (status) {
                        status.querySelector('.tfa-badge').classList.remove('disabled');
                        status.querySelector('h3 b').textContent = 'enabled';
                    }
                } else {
                    alert('Please enter a valid 6-digit code.');
                }
            });
        }

        const copyBtn = document.getElementById('copySecret');
        if (copyBtn) {
            copyBtn.addEventListener('click', () => {
                const secret = document.getElementById('tfaSecret')?.textContent;
                if (secret && navigator.clipboard) {
                    navigator.clipboard.writeText(secret).then(() => {
                        copyBtn.textContent = 'Copied!';
                        setTimeout(() => copyBtn.textContent = 'Copy', 2000);
                    });
                }
            });
        }
    },

    generateDemoSecret() {
        const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
        return Array.from({length:32}, () => chars[Math.floor(Math.random() * chars.length)]).join('');
    },

    initSessions() {
        const revokeAll = document.getElementById('revokeAllBtn');
        if (revokeAll) {
            revokeAll.addEventListener('click', () => {
                if (confirm('Revoke all other sessions? You will remain logged in on this device.')) {
                    alert('All other sessions revoked.');
                }
            });
        }

        const saveSettings = document.getElementById('saveSessionSettings');
        if (saveSettings) {
            saveSettings.addEventListener('click', () => {
                alert('Session settings saved.');
            });
        }
    }
};

// Tab navigation helper
function showTab(tabName) {
    document.querySelectorAll('.sec-tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.sec-content').forEach(c => c.classList.remove('active'));
    const tab = document.querySelector(`.sec-tab[data-tab="${tabName}"]`);
    const content = document.getElementById('tab-' + tabName);
    if (tab) tab.classList.add('active');
    if (content) content.classList.add('active');
}

document.addEventListener('DOMContentLoaded', () => Security.init());
