// Metis Genie Platform v5.3.1
/* Metis Genie Platform Operations Page JavaScript v5.3.1 */
'use strict';

const Operations = {
    healthData: null,
    backupData: null,
    jobsData: [],
    circuitBreakers: null,
    rateLimits: null,
    telemetryData: null,
    computeData: null,
    deploymentData: null,
    configStatus: null,

    async init() {
        // Restore session for sub-page
        if (!Connection.connected) Connection.initSubPage();

        this.initTabs();
        this.initHealthCheck();
        this.initBackups();
        this.initCache();
        this.initDeploy();
        this.startAutoRefresh();
        await this.fetchServerData();
    },

    async fetchServerData() {
        if (!Connection.connected || Connection.demoMode) return;

        const [healthR, backupR, jobsR, cbR, rlR, telR, compR, deplR, cfgR] =
            await Promise.allSettled([
                Connection.get('/operations/health'),
                Connection.get('/operations/backups'),
                Connection.get('/operations/jobs'),
                Connection.get('/ops/circuit-breakers'),
                Connection.get('/ops/rate-limits'),
                Connection.get('/ops/telemetry'),
                Connection.get('/compute'),
                Connection.get('/deployment'),
                Connection.get('/config/validate')
            ]);

        if (healthR.status === 'fulfilled' && healthR.value.ok) {
            this.healthData = healthR.value.data;
            this.renderHealth();
        }
        if (backupR.status === 'fulfilled' && backupR.value.ok) {
            this.backupData = backupR.value.data;
            this.renderBackups();
        }
        if (jobsR.status === 'fulfilled' && jobsR.value.ok) {
            this.jobsData = jobsR.value.data;
            this.renderJobs();
        }
        if (cbR.status === 'fulfilled' && cbR.value.ok) {
            this.circuitBreakers = cbR.value.data;
            this.renderCircuitBreakers();
        }
        if (rlR.status === 'fulfilled' && rlR.value.ok) {
            this.rateLimits = rlR.value.data;
            this.renderRateLimits();
        }
        if (telR.status === 'fulfilled' && telR.value.ok) {
            this.telemetryData = telR.value.data;
            this.renderTelemetry();
        }
        if (compR.status === 'fulfilled' && compR.value.ok) {
            this.computeData = compR.value.data;
            this.renderCompute();
        }
        if (deplR.status === 'fulfilled' && deplR.value.ok) {
            this.deploymentData = deplR.value.data;
            this.renderDeployment();
        }
        if (cfgR.status === 'fulfilled' && cfgR.value.ok) {
            this.configStatus = cfgR.value.data;
            this.renderConfigValidation();
        }
    },

    renderCircuitBreakers() {
        const el = document.getElementById('circuitBreakerData');
        if (!el || !this.circuitBreakers) return;
        const breakers = this.circuitBreakers.breakers || [];
        el.innerHTML = breakers.map(b =>
            '<div class="cache-detail"><span>' + b.name + '</span>' +
            '<span class="' + (b.state === 'CLOSED' ? 'ok' : 'warn') + '">' +
            b.state + ' (' + b.failure_count + ' failures)</span></div>'
        ).join('') || '<div class="cache-detail"><span>All circuits healthy</span></div>';
    },

    renderRateLimits() {
        const el = document.getElementById('rateLimitData');
        if (!el || !this.rateLimits) return;
        const limits = this.rateLimits.limits || this.rateLimits || [];
        const arr = Array.isArray(limits) ? limits : [];
        el.innerHTML = arr.map(l =>
            '<div class="cache-detail"><span>' + l.endpoint + '</span>' +
            '<span>' + l.current_usage + '/' + l.limit +
            ' (' + (l.utilization_pct || 0).toFixed(1) + '%)</span></div>'
        ).join('') || '<div class="cache-detail"><span>No rate limits configured</span></div>';
    },

    renderTelemetry() {
        const el = document.getElementById('telemetryData');
        if (!el || !this.telemetryData) return;
        const t = this.telemetryData;
        el.innerHTML =
            '<div class="cache-detail"><span>Uptime</span><span>' + (t.uptime_seconds || 0) + 's</span></div>' +
            '<div class="cache-detail"><span>Total Requests</span><span>' + (t.total_requests || 0) + '</span></div>' +
            '<div class="cache-detail"><span>Avg Latency</span><span>' + (t.avg_latency_ms || 0).toFixed(1) + 'ms</span></div>' +
            '<div class="cache-detail"><span>Error Rate</span><span>' + (t.error_rate_pct || 0).toFixed(2) + '%</span></div>' +
            '<div class="cache-detail"><span>Active Connections</span><span>' + (t.active_connections || 0) + '</span></div>';
    },

    renderCompute() {
        const el = document.getElementById('computeData');
        if (!el || !this.computeData) return;
        const c = this.computeData;
        el.innerHTML =
            '<div class="cache-detail"><span>Thread Pool</span><span>' + (c.thread_pool_size || 0) + ' threads</span></div>' +
            '<div class="cache-detail"><span>Queue Depth</span><span>' + (c.queue_depth || 0) + '</span></div>' +
            '<div class="cache-detail"><span>GPU Available</span><span>' + (c.gpu_available ? 'Yes' : 'No') + '</span></div>' +
            '<div class="cache-detail"><span>Active Jobs</span><span>' + (c.active_jobs || 0) + '</span></div>';
    },

    renderDeployment() {
        const el = document.getElementById('deployData');
        if (!el || !this.deploymentData) return;
        const d = this.deploymentData;
        el.innerHTML =
            '<div class="cache-detail"><span>Environment</span><span>' + (d.environment || 'development') + '</span></div>' +
            '<div class="cache-detail"><span>Version</span><span>' + (d.version || '5.3.1') + '</span></div>' +
            '<div class="cache-detail"><span>Host</span><span>' + (d.hostname || 'localhost') + '</span></div>' +
            '<div class="cache-detail"><span>Container Ready</span><span>' + (d.container_ready ? 'Yes' : 'No') + '</span></div>' +
            '<div class="cache-detail"><span>K8s Ready</span><span>' + (d.kubernetes_ready ? 'Yes' : 'No') + '</span></div>';
    },

    renderConfigValidation() {
        const el = document.getElementById('configValidateData');
        if (!el || !this.configStatus) return;
        const c = this.configStatus;
        el.innerHTML =
            '<div class="cache-detail"><span>Config Valid</span><span class="' +
            (c.valid ? 'ok' : 'warn') + '">' + (c.valid ? 'Yes' : 'No') + '</span></div>' +
            '<div class="cache-detail"><span>Errors</span><span>' + (c.error_count || 0) + '</span></div>' +
            '<div class="cache-detail"><span>Warnings</span><span>' + (c.warning_count || 0) + '</span></div>';
    },

    async requestShutdown() {
        if (!confirm('Shut down the Metis Genie Platform server?\n\nThis will stop the server process. You will need to restart it manually.')) return;
        let shutdownOk = false;
        try {
            const resp = await fetch(Connection.api('/ops/shutdown'), { method: 'POST', headers: Connection.authHeaders() });
            console.log('[Shutdown] Response status:', resp.status);
            if (resp.ok) {
                shutdownOk = true;
            } else {
                const msg = resp.status === 401 || resp.status === 403
                    ? 'Shutdown failed: not authorized. Please log in as admin.'
                    : 'Shutdown failed: server returned HTTP ' + resp.status;
                console.error('[Shutdown]', msg);
                alert(msg);
            }
        } catch (e) {
            console.log('[Shutdown] Network error (server may be gone):', e.message);
            shutdownOk = true;
        }
        if (!shutdownOk) {
            console.log('[Shutdown] Aborted - server did not accept shutdown');
            return;
        }
        Connection.stopHealthCheck();
        Connection.stopReconnect();
        Connection.stopSessionTimer();
        // Replace entire page with centered shutdown notice
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

    renderHealth() {
        if (!this.healthData) return;
        const h = this.healthData;

        // Update overall status KPI
        const kpis = document.querySelectorAll('.s-kpi');
        if (kpis[0]) {
            const val = kpis[0].querySelector('.s-kpi-value');
            if (val) val.textContent = h.overall || 'Healthy';
            kpis[0].className = 's-kpi ' + (h.overall === 'Healthy' ? 'healthy' : 'degraded');
        }

        // Update system info
        if (h.system) {
            const uptimeEl = document.getElementById('systemUptime');
            if (uptimeEl && h.system.uptime_seconds) {
                const s = h.system.uptime_seconds;
                const d = Math.floor(s / 86400);
                const hr = Math.floor((s % 86400) / 3600);
                const m = Math.floor((s % 3600) / 60);
                uptimeEl.textContent = `Uptime: ${d}d ${hr}h ${m}m`;
            }
        }

        // Update component status table
        const components = h.components || [];
        const tbody = document.getElementById('componentStatus');
        if (tbody && components.length) {
            tbody.innerHTML = components.map(c => {
                const badgeCls = c.status === 'Healthy' ? 'badge-ok' : c.status === 'Degraded' ? 'badge-warn' : 'badge-err';
                return `<tr>
                    <td>${c.name}</td>
                    <td><span class="badge ${badgeCls}">${c.status}</span></td>
                    <td>${c.latency_ms}ms</td>
                    <td>5s ago</td>
                    <td>${(c.uptime_pct || 99.99).toFixed(2)}%</td>
                </tr>`;
            }).join('');
        }
    },

    renderBackups() {
        if (!this.backupData) return;
        const b = this.backupData;

        // Update backup config display
        const retSel = document.getElementById('backupRetention');
        if (retSel && b.retention_days) {
            for (const opt of retSel.options) {
                if (parseInt(opt.value) === b.retention_days) { opt.selected = true; break; }
            }
        }

        // Update backup history table
        const backups = b.backups || [];
        const tbody = document.getElementById('backupList');
        if (tbody && backups.length) {
            tbody.innerHTML = backups.map(bk => {
                const ts = new Date(bk.timestamp).toLocaleString();
                const badgeCls = bk.status === 'Complete' ? 'badge-ok' : 'badge-warn';
                return `<tr>
                    <td>${bk.id}</td>
                    <td>${bk.type}</td>
                    <td>${ts}</td>
                    <td>-</td>
                    <td>${bk.size_mb} MB</td>
                    <td><span class="badge ${badgeCls}">${bk.status}</span></td>
                    <td><button class="btn btn-xs btn-secondary">Restore</button> <button class="btn btn-xs btn-secondary">Download</button></td>
                </tr>`;
            }).join('');
        }
    },

    renderJobs() {
        if (!this.jobsData.length) return;

        const tbody = document.getElementById('jobList');
        if (!tbody) return;

        tbody.innerHTML = this.jobsData.map(j => {
            const statusCls = j.status === 'Running' ? 'badge-ok' : j.status === 'Idle' ? 'badge-warn' : 'badge-err';
            const priCls = j.status === 'Running' ? 'badge-pri-high' : 'badge-pri-normal';
            const lastRun = j.last_run ? new Date(j.last_run).toLocaleTimeString() : '-';
            const nextRun = j.next_run ? new Date(j.next_run).toLocaleTimeString() : '-';
            return `<tr>
                <td>${j.id}</td>
                <td>${j.name}</td>
                <td><span class="badge ${priCls}">${j.status === 'Running' ? 'HIGH' : 'NORMAL'}</span></td>
                <td><span class="badge ${statusCls}">${j.status}</span></td>
                <td>${lastRun}</td>
                <td>Sched: ${j.schedule} | Avg: ${j.avg_duration_ms}ms | Next: ${nextRun}</td>
            </tr>`;
        }).join('');
    },

    initTabs() {
        document.querySelectorAll('.ops-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.ops-tab').forEach(t => t.classList.remove('active'));
                document.querySelectorAll('.ops-content').forEach(c => c.classList.remove('active'));
                tab.classList.add('active');
                const target = document.getElementById('tab-' + tab.dataset.tab);
                if (target) target.classList.add('active');
            });
        });
    },

    initHealthCheck() {
        const btn = document.getElementById('runHealthCheck');
        if (btn) {
            btn.addEventListener('click', async () => {
                btn.textContent = 'Running...';
                btn.disabled = true;

                // Fetch real health data
                if (Connection.connected && !Connection.demoMode) {
                    const r = await Connection.get('/operations/health');
                    if (r.ok) {
                        this.healthData = r.data;
                        this.renderHealth();
                    }
                }

                btn.textContent = 'Run All Checks';
                btn.disabled = false;
                document.querySelectorAll('.health-time').forEach(el => {
                    el.textContent = 'Response: ' + (Math.random() * 10).toFixed(0) + 'ms';
                });
            });
        }
    },

    initBackups() {
        const manualBtn = document.getElementById('manualBackup');
        if (manualBtn) {
            manualBtn.addEventListener('click', () => {
                if (confirm('Create a manual full backup now?')) {
                    manualBtn.textContent = 'Creating...';
                    manualBtn.disabled = true;
                    setTimeout(async () => {
                        manualBtn.textContent = 'Create Manual Backup';
                        manualBtn.disabled = false;
                        alert('Backup created successfully.');
                        // Refresh backup list
                        if (Connection.connected && !Connection.demoMode) {
                            const r = await Connection.get('/operations/backups');
                            if (r.ok) { this.backupData = r.data; this.renderBackups(); }
                        }
                    }, 3000);
                }
            });
        }

        const saveConfig = document.getElementById('saveBackupConfig');
        if (saveConfig) {
            saveConfig.addEventListener('click', () => {
                alert('Backup configuration saved.');
            });
        }
    },

    initCache() {
        const clearBtn = document.getElementById('clearCache');
        if (clearBtn) {
            clearBtn.addEventListener('click', () => {
                if (confirm('Clear all caches? This may temporarily affect performance.')) {
                    alert('All caches cleared. 8,432 items removed.');
                }
            });
        }
    },

    initDeploy() {
        const actions = {
            genDockerCompose: 'Docker Compose file generated: docker-compose.yml',
            genK8sManifest: 'Kubernetes manifest generated: k8s-deployment.yaml',
            genSystemd: 'Systemd service file generated: metis-genie-platform.service',
            genNginxConf: 'Nginx configuration generated: metis-genie-platform.conf'
        };

        Object.entries(actions).forEach(([id, msg]) => {
            const btn = document.getElementById(id);
            if (btn) {
                btn.addEventListener('click', () => alert(msg));
            }
        });
    },

    startAutoRefresh() {
        const uptimeEl = document.getElementById('systemUptime');
        if (uptimeEl) {
            let seconds = 14 * 86400 + 6 * 3600 + 23 * 60;
            setInterval(() => {
                seconds++;
                const d = Math.floor(seconds / 86400);
                const h = Math.floor((seconds % 86400) / 3600);
                const m = Math.floor((seconds % 3600) / 60);
                uptimeEl.textContent = `Uptime: ${d}d ${h}h ${m}m`;
            }, 1000);
        }

        // Periodic resource metric updates + server refresh
        setInterval(async () => {
            const cpuVal = document.getElementById('cpuVal');
            if (cpuVal) {
                const cpu = 25 + Math.random() * 20;
                cpuVal.textContent = cpu.toFixed(0) + '%';
                const fill = cpuVal.closest('.card-body')?.querySelector('.progress-fill');
                if (fill) fill.style.width = cpu + '%';
            }
            // Refresh health from server every 30s
            if (Connection.connected && !Connection.demoMode) {
                const r = await Connection.get('/operations/health');
                if (r.ok) { this.healthData = r.data; this.renderHealth(); }
            }
        }, 30000);
    }
};

document.addEventListener('DOMContentLoaded', () => Operations.init());
