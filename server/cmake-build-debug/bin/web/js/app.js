// Metis Genie Platform v5.3.1
/**
 * Metis Genie Platform v5.3.1 - Main Application
 * Pure Vanilla JS
 * @author Bennie Shearer  @copyright 2026
 */
const App = {
    version: '5.3.1',
    page: 'dashboard',

    init() {
        U.log.info('App', '=== Metis Genie Platform v' + this.version + ' Starting ===');
        U.log.info('App', 'Config loaded: ' + (Config.isLoaded() ? 'yes' : 'no'));
        U.log.debug('App', 'Log level: ' + Config.get('logging.level'));
        U.log.debug('App', 'Server URL: ' + Config.get('server.url'));
        
        this.loadTheme();
        this.bindLogin();
        
        // Pre-fill demo credentials if enabled
        if (Config.get('demo.enabled')) {
            const userInput = U.$('#loginUser');
            const passInput = U.$('#loginPass');
            if (userInput && !userInput.value) {
                userInput.value = Config.get('demo.default_username') || '';
            }
            if (passInput && !passInput.value) {
                passInput.value = Config.get('demo.default_password') || '';
            }
        }
        
        // Pre-fill server URL from config
        const serverInput = U.$('#loginServer');
        if (serverInput && !serverInput.value) {
            serverInput.value = Config.get('server.url') || 'http://localhost:8080';
        }
        
        // Check for existing session
        if (Connection.init()) {
            U.log.info('App', 'Restored existing session');
            this.enterApp();
        } else {
            U.log.info('App', 'No existing session, showing login');
        }
    },

    async enterApp() {
        U.$('#loginOverlay').classList.add('hide');
        U.$('#app').classList.remove('app-hidden');
        Connection.updateUserUI();
        Connection.updateStatusUI();
        this.bind();
        Commands.init();
        Widgets.init();
        Keyboard.init();
        await Data.init();
        this.render();
        this.startClock();
        this.startTicker();
        this.bindScrollTop();
        this.bindChartTooltip();
        
        // Show admin section if user is Administrator
        const adminSection = U.$('#adminSection');
        if (adminSection) {
            if (Connection.isAdmin()) {
                adminSection.classList.remove('hide');
            } else {
                adminSection.classList.add('hide');
            }
        }
        
        // User card click opens profile modal
        const userCard = U.$('#userCard');
        if (userCard) {
            userCard.onclick = () => this.openProfileModal();
        }
    },

    showLogin() {
        U.$('#loginOverlay').classList.remove('hide');
        U.$('#app').classList.add('app-hidden');
        U.$('#loginStatus').textContent = '';
        U.$('#loginStatus').className = 'login-status';
    },

    bindLogin() {
        // Login/Register Tab switching
        U.$('#tabLogin').onclick = () => {
            U.$('#tabLogin').classList.add('active');
            U.$('#tabRegister').classList.remove('active');
            U.$('#loginForm').classList.remove('hide');
            U.$('#registerForm').classList.add('hide');
        };
        U.$('#tabRegister').onclick = () => {
            U.$('#tabRegister').classList.add('active');
            U.$('#tabLogin').classList.remove('active');
            U.$('#registerForm').classList.remove('hide');
            U.$('#loginForm').classList.add('hide');
        };

        // Sign In button
        U.$('#loginBtn').onclick = async () => {
            const user = U.$('#loginUser').value.trim();
            const pass = U.$('#loginPass').value;
            const url = U.$('#loginServer').value.trim();
            if (!user) { this.setLoginStatus('Username required', 'fail'); return; }
            this.setLoginStatus('Connecting...', 'wait');
            const r = await Connection.login(user, pass, url);
            if (r.ok) {
                this.setLoginStatus('Welcome, ' + r.user.name, 'ok');
                setTimeout(() => this.enterApp(), 400);
            } else {
                this.setLoginStatus(r.error || 'Login failed', 'fail');
            }
        };

        // Demo Mode button
        U.$('#demoBtn').onclick = () => {
            const user = U.$('#loginUser').value.trim() || 'Demo User';
            Connection.loginDemo(user);
            this.setLoginStatus('Entering demo mode...', 'ok');
            setTimeout(() => this.enterApp(), 300);
        };

        // Test Connection button on login screen
        U.$('#loginTestBtn').onclick = async () => {
            const url = U.$('#loginServer').value.trim();
            this.setLoginStatus('Testing...', 'wait');
            const r = await Connection.testConnection(url);
            if (r.ok) {
                this.setLoginStatus('Connected! Server v' + r.version, 'ok');
            } else {
                this.setLoginStatus(r.error + ' -- use Demo Mode for offline', 'fail');
            }
        };

        // Enter key in password field
        U.$('#loginPass').onkeydown = e => { if (e.key === 'Enter') U.$('#loginBtn').click(); };

        // Registration form
        U.$('#regBtn').onclick = async () => {
            const user = U.$('#regUser').value.trim();
            const pass = U.$('#regPass').value;
            const pass2 = U.$('#regPass2').value;
            const email = U.$('#regEmail').value.trim();
            const display = U.$('#regDisplay').value.trim();
            const url = U.$('#regServer').value.trim();

            const status = U.$('#regStatus');
            const setStatus = (msg, ok) => {
                status.textContent = msg;
                status.className = 'login-status ' + (ok ? 'ok' : 'fail');
            };

            if (!user) { setStatus('Username required', false); return; }
            if (user.length < 3) { setStatus('Username must be at least 3 characters', false); return; }
            if (!pass) { setStatus('Password required', false); return; }
            if (pass.length < 4) { setStatus('Password must be at least 4 characters', false); return; }
            if (pass !== pass2) { setStatus('Passwords do not match', false); return; }

            setStatus('Creating account...', true);
            const r = await Connection.register(user, pass, email, display, url);
            if (r.ok) {
                setStatus('Account created! Logging in...', true);
                setTimeout(() => this.enterApp(), 400);
            } else {
                setStatus(r.error || 'Registration failed', false);
            }
        };
        U.$('#regPass2').onkeydown = e => { if (e.key === 'Enter') U.$('#regBtn').click(); };
    },

    setLoginStatus(msg, cls) {
        const el = U.$('#loginStatus');
        el.textContent = msg; el.className = 'login-status ' + (cls || '');
    },

    loadTheme() { const t = U.store.get('theme', 'light'); document.body.dataset.theme = t; const s = U.$('#themeSel'); if (s) s.value = t; },
    toggleTheme() {
        const t = document.body.dataset.theme === 'light' ? 'dark' : 'light';
        document.body.dataset.theme = t; U.store.set('theme', t);
        const s = U.$('#themeSel'); if (s) s.value = t; this.renderCharts();
    },

    bind() {
        U.$('#menuBtn').onclick = () => U.$('#sidebar').classList.toggle('hide');
        U.$('#themeBtn').onclick = () => this.toggleTheme();
        U.$('#refreshBtn').onclick = () => this.refresh();
        U.$('#notifBtn').onclick = () => this.toggleDrawer();
        U.$('#closeDrawer').onclick = () => this.toggleDrawer(false);
        U.$('#drawerOverlay').onclick = () => this.toggleDrawer(false);
        U.$('#clearNotifs').onclick = () => { Data.alerts = []; this.renderNotifs(); this.updateBadges(); this.toast('info', 'Cleared'); };
        U.$('#modalClose').onclick = () => this.closeModal();
        U.$('#modalBg').onclick = e => { if (e.target === U.$('#modalBg')) this.closeModal(); };

        U.on(U.$('#nav'), 'click', '.nav-item', (e, el) => { e.preventDefault(); this.navigate(el.dataset.page); });
        U.on(U.$('#perfBtns'), 'click', '.btn-sm', (e, el) => { U.$$('#perfBtns .btn-sm').forEach(b => b.classList.remove('active')); el.classList.add('active'); this.updatePerfChart(el.dataset.p); });

        // Tabs
        document.querySelectorAll('.tabs, .tabs-inline').forEach(tabs => {
            U.on(tabs, 'click', '.tab-btn', (e, el) => {
                const parent = el.closest('.page') || el.closest('.head-actions');
                parent.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
                el.classList.add('active');
                const pg = el.closest('.page');
                if (pg) { pg.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active')); const t = pg.querySelector('#tab-' + el.dataset.tab); if (t) { t.classList.add('active'); this.renderTabContent(el.dataset.tab); } }
                if (el.closest('#orderTabs')) this.renderOrders(el.dataset.tab);
            });
        });

        U.on(document, 'click', 'th.sortable', (e, el) => this.sortTable(el));
        U.on(document, 'contextmenu', '.tbl tbody tr', (e, el) => { e.preventDefault(); this.showCtxMenu(e, el); });
        document.addEventListener('click', () => this.hideCtxMenu());

        // Export
        U.$('#exportHold')?.addEventListener('click', () => { Export.toCSV(Export.holdings(), 'holdings.csv'); this.toast('success', 'Exported'); });
        U.$('#exportPos')?.addEventListener('click', () => { Export.toCSV(Export.positions(), 'positions.csv'); this.toast('success', 'Exported'); });
        U.$('#exportPerf')?.addEventListener('click', () => { Export.toCSV(Data.perfStats.map(s => ({ Metric: s.label, Value: s.value })), 'performance.csv'); this.toast('success', 'Exported'); });
        U.$('#exportTx')?.addEventListener('click', () => { Export.toCSV(Export.transactions(), 'transactions.csv'); this.toast('success', 'Exported'); });

        // Import
        U.$('#importWatch')?.addEventListener('click', () => U.$('#importFile').click());
        U.$('#importFile')?.addEventListener('change', async e => {
            const file = e.target.files[0]; if (!file) return;
            try { const data = await Export.importCSV(file); this.toast('success', 'Imported ' + data.length + ' items'); } catch { this.toast('error', 'Import failed'); }
            e.target.value = '';
        });

        // Actions
        U.$('#newPortBtn')?.addEventListener('click', () => this.showModal('New Portfolio', '<div class="form-row"><label>Name</label><input type="text" placeholder="Portfolio name"></div><div class="form-row"><label>Type</label><select class="sel"><option>Growth</option><option>Value</option><option>Balanced</option></select></div><div class="form-row"><label>Benchmark</label><select class="sel"><option>S&P 500</option><option>NASDAQ 100</option></select></div>', '<button class="btn btn-secondary" onclick="App.closeModal()">Cancel</button><button class="btn btn-primary" onclick="App.closeModal();App.toast(\'success\',\'Created\')">Create</button>'));
        U.$('#newOrdBtn')?.addEventListener('click', () => this.showOrderModal());
        U.$('#addWatchBtn')?.addEventListener('click', () => this.showModal('Add Symbol', '<div class="form-row"><label>Symbol</label><input type="text" placeholder="AAPL"></div>', '<button class="btn btn-secondary" onclick="App.closeModal()">Cancel</button><button class="btn btn-primary" onclick="App.closeModal();App.toast(\'success\',\'Added\')">Add</button>'));
        U.$('#stressBtn')?.addEventListener('click', () => { this.toast('info', 'Running...'); setTimeout(() => this.toast('success', 'Complete'), 1500); });
        U.$('#readAllBtn')?.addEventListener('click', () => { Data.alerts.forEach(a => a.read = true); this.updateBadges(); this.toast('success', 'All read'); });
        U.$('#saveBtn')?.addEventListener('click', () => { U.store.set('currency', U.$('#curSel')?.value); U.store.set('refresh', U.$('#refSel')?.value); const url = U.$('#serverUrl')?.value; if (url) { Connection.baseUrl = url; U.store.set('serverUrl', url); } this.toast('success', 'Saved'); });
        U.$('#resetBtn')?.addEventListener('click', () => { localStorage.clear(); this.toast('info', 'Reset'); });
        U.$('#themeSel')?.addEventListener('change', e => { document.body.dataset.theme = e.target.value; U.store.set('theme', e.target.value); this.renderCharts(); });

        // Logout and Exit
        U.$('#logoutBtn')?.addEventListener('click', () => { Connection.logout(); this.showLogin(); });
        U.$('#exitBtn')?.addEventListener('click', async () => {
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
            // Stop all background timers
            Connection.stopHealthCheck();
            Connection.stopReconnect();
            Connection.stopSessionTimer();
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
        });

        // Session renewal
        U.$('#renewSession')?.addEventListener('click', () => Connection.renewSession());

        // Favorites (star toggle)
        U.on(document, 'click', '.star-btn', (e, el) => {
            el.classList.toggle('active');
            const sym = el.dataset.symbol;
            const favs = JSON.parse(U.store.get('favorites', '[]'));
            if (el.classList.contains('active')) { if (!favs.includes(sym)) favs.push(sym); }
            else { const i = favs.indexOf(sym); if (i >= 0) favs.splice(i, 1); }
            U.store.set('favorites', JSON.stringify(favs));
        });

        // Test Connection in Settings
        U.$('#testConnBtn')?.addEventListener('click', async () => {
            const url = U.$('#serverUrl')?.value?.trim();
            const sc = U.$('#settingsConnStatus');
            if (sc) sc.textContent = 'Testing...';
            const r = await Connection.testConnection(url);
            if (sc) sc.textContent = r.ok ? 'Connected (v' + r.version + ')' : r.error;
            if (sc) sc.style.color = r.ok ? 'var(--green)' : 'var(--red)';
            this.toast(r.ok ? 'success' : 'error', r.ok ? 'Server connected' : r.error);
        });

        // Logging controls
        U.$('#logLevelSel')?.addEventListener('change', e => {
            U.log.setLevel(e.target.value);
            this.toast('info', 'Log level set to ' + e.target.value);
        });
        U.$('#logConsole')?.addEventListener('change', e => {
            window.Config?.set('logging.console', e.target.checked);
        });
        U.$('#showLogPanelBtn')?.addEventListener('click', () => {
            U.log.showPanel();
        });
        U.$('#downloadLogsBtn')?.addEventListener('click', () => {
            const filename = U.log.export();
            this.toast('success', 'Logs exported to ' + filename);
        });
        U.$('#clearLogsBtn')?.addEventListener('click', () => {
            U.log.clear();
            this.updateLogEntryCount();
            this.toast('info', 'Logs cleared');
        });

        // Column visibility
        U.$('#colVisPos')?.addEventListener('click', e => { e.stopPropagation(); this.showColPopover(e, 'posTblWrap'); });
        document.addEventListener('click', () => U.$('#colPopover').classList.remove('show'));

        // Keyboard (delegated to Keyboard module, keep only hash/resize)
        window.addEventListener('hashchange', () => this.navigate(location.hash.slice(1) || 'dashboard', false));
        window.addEventListener('resize', U.debounce(() => this.renderCharts(), 200));
    },

    bindScrollTop() {
        const btn = U.$('#scrollTop');
        const content = U.$('#content');
        if (!content || !btn) return;
        content.addEventListener('scroll', () => { btn.classList.toggle('show', content.scrollTop > 300); });
        btn.addEventListener('click', () => content.scrollTo({ top: 0, behavior: 'smooth' }));
    },

    bindChartTooltip() {
        const tip = U.$('#chartTip');
        document.addEventListener('mousemove', e => {
            const canvas = e.target;
            if (canvas.tagName !== 'CANVAS' || !canvas._chartData) { tip.classList.remove('show'); return; }
            const cd = canvas._chartData;
            const rect = canvas.getBoundingClientRect();
            const mx = e.clientX - rect.left;
            if (mx < cd.p.l || mx > cd.w - cd.p.r) { tip.classList.remove('show'); return; }
            const idx = Math.round(((mx - cd.p.l) / (cd.w - cd.p.l - cd.p.r)) * (cd.data.labels.length - 1));
            if (idx < 0 || idx >= cd.data.labels.length) { tip.classList.remove('show'); return; }
            const vals = cd.data.datasets.map((ds, i) => `<div class="chart-tip-value" style="color:${ds.color || U.getColor(i)}">${U.num(ds.data[idx])}</div>`).join('');
            tip.innerHTML = `<div class="chart-tip-label">${U.esc(cd.data.labels[idx])}</div>${vals}`;
            tip.style.left = (e.clientX + 12) + 'px';
            tip.style.top = (e.clientY - 10) + 'px';
            tip.classList.add('show');
        });
    },

    navigate(page, hash = true) {
        if (!page) return;  // guard: sidebar action items have data-page="" to prevent undefined navigate
        U.log.debug('App', 'Navigate: ' + page);
        this.page = page;
        Keyboard.clearSelection();
        U.$$('.nav-item').forEach(el => { el.classList.toggle('active', el.dataset.page === page); if (el.dataset.page === page) el.setAttribute('aria-current', 'page'); else el.removeAttribute('aria-current'); });
        U.$$('.page').forEach(el => el.classList.toggle('active', el.id === 'page-' + page));
        const titles = { dashboard:'Dashboard', watchlist:'Watchlist', portfolios:'Portfolios', positions:'Positions', orders:'Orders', transactions:'Transactions', performance:'Performance', risk:'Risk Analytics', benchmark:'Benchmark', alerts:'Alerts', settings:'Settings' };
        U.$('#bcPage').textContent = titles[page] || page;
        if (hash) location.hash = page;
        this.renderPage(page);
    },

    renderPage(p) {
        const m = { dashboard:()=>this.renderDashboard(), watchlist:()=>this.renderWatchlist(), portfolios:()=>this.renderPortfolios(), positions:()=>this.renderPositions(), orders:()=>this.renderOrders('active'), transactions:()=>this.renderTransactions(), performance:()=>this.renderPerformance(), risk:()=>this.renderRisk(), benchmark:()=>this.renderBenchmark(), alerts:()=>this.renderAlerts(), 'admin-users':()=>this.renderAdminUsers(), settings:()=>this.renderSettings() };
        if (m[p]) m[p]();
    },

    render() { this.navigate(location.hash.slice(1) || 'dashboard', false); this.renderNotifs(); this.updateBadges(); },

    renderDashboard() { this.renderMarketStrip(); this.renderHoldings(); this.renderActivity(); this.renderMetrics(); this.renderSectors(); this.renderCharts(); this.renderKpiSparks(); },

    renderMarketStrip() { const el = U.$('#mktStrip'); if (!el) return; el.innerHTML = Data.ticker.map(t => `<span class="mkt-item"><span class="mkt-sym">${U.esc(t.sym)}</span><span class="mkt-price">${U.num(t.price)}</span><span class="mkt-chg ${t.chg>=0?'up':'down'}">${U.pct(t.chg,1)}</span></span>`).join(''); },
    startTicker() { setInterval(() => { Data.ticker.forEach(t => { t.price *= 1 + (Math.random() - 0.5) * 0.001; t.chg += (Math.random() - 0.5) * 0.05; }); if (this.page === 'dashboard') this.renderMarketStrip(); }, 5000); },
    startClock() { const el = U.$('#clockStatus'); const tick = () => { el.textContent = U.clock(); requestAnimationFrame(tick); }; tick(); },

    renderKpiSparks() { ['sparkAum','sparkPnl','sparkYtd','sparkVar'].forEach(id => { const c = U.$('#'+id); if (c) Charts.spark(c, Charts.randomWalk(20, 100, 0.01), { width: 80, height: 30 }); }); },

    renderHoldings() {
        const favs = JSON.parse(U.store.get('favorites', '[]'));
        U.$('#holdBody').innerHTML = Data.holdings.slice(0,7).map(h => {
            const starred = favs.includes(h.symbol) ? ' active' : '';
            return `<tr data-symbol="${U.attr(h.symbol)}"><td><button class="star-btn${starred}" data-symbol="${U.attr(h.symbol)}" title="Favorite"><svg viewBox="0 0 24 24"><path d="M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01z"/></svg></button></td><td class="sym">${U.esc(h.symbol)}</td><td>${U.esc(h.name)}</td><td class="r">${U.num(h.shares,0)}</td><td class="r">${U.currFull(h.price)}</td><td class="r">${U.currency(h.value)}</td><td class="r">${U.num(h.weight)}%</td><td class="r ${h.change>=0?'up':'down'}">${U.pct(h.change)}</td></tr>`;
        }).join('');
    },

    renderActivity() {
        const ic = { buy:'<svg viewBox="0 0 24 24"><path d="M12 19V5M5 12l7-7 7 7"/></svg>', sell:'<svg viewBox="0 0 24 24"><path d="M12 5v14M19 12l-7 7-7-7"/></svg>', div:'<svg viewBox="0 0 24 24"><path d="M12 2v20M17 5H9.5a3.5 3.5 0 000 7h5a3.5 3.5 0 010 7H6"/></svg>' };
        U.$('#actList').innerHTML = Data.activities.map(a => `<div class="act-item"><div class="act-icon ${a.type}">${ic[a.type]}</div><div class="act-text"><div class="act-title">${U.esc(a.title)}</div><div class="act-desc">${U.esc(a.desc)}</div></div><span class="act-time">${U.relTime(a.time)}</span></div>`).join('');
    },

    renderMetrics() { U.$('#riskMet').innerHTML = Data.riskMetrics.map(m => `<div class="met"><div class="met-label">${U.esc(m.label)}</div><div class="met-value ${m.pos?'up':''} ${m.neg?'down':''}">${m.neg?'':m.pos?'+':''}${U.num(m.value)}${['Sharpe','Sortino','Beta'].includes(m.label)?'':'%'}</div></div>`).join(''); },
    renderSectors() { U.$('#secBars').innerHTML = Data.sectors.map(s => `<div class="bar-item"><div class="bar-head"><span>${U.esc(s.name)}</span><span>${s.value}%</span></div><div class="bar-track"><div class="bar-fill" style="width:${s.value}%;background:${s.color}"></div></div></div>`).join(''); },

    renderCharts() {
        if (this.page !== 'dashboard') return;
        const pc = U.$('#perfChart'); if (pc) { const d = 30; Charts.line(pc, { labels: Charts.dateLabels(d), datasets: [{ data: Charts.randomWalk(d,100,.015), color:'#3b82f6', fill:true }, { data: Charts.randomWalk(d,100,.012), color:'#94a3b8' }] }); }
        const ac = U.$('#allocChart'); if (ac) { Charts.donut(ac, Data.allocation, { size: 180 }); U.$('#allocLeg').innerHTML = Data.allocation.map(a => `<span class="leg-item"><span class="leg-dot" style="background:${a.color}"></span>${U.esc(a.name)} ${a.value}%</span>`).join(''); }
    },
    updatePerfChart(p) { const d = { '1M':30,'3M':90,'6M':180,'YTD':120,'1Y':365 }[p]||30; Charts.line(U.$('#perfChart'), { labels: Charts.dateLabels(d), datasets: [{ data: Charts.randomWalk(d,100,.015), color:'#3b82f6', fill:true }, { data: Charts.randomWalk(d,100,.012), color:'#94a3b8' }] }); },

    renderWatchlist() {
        U.$('#watchBody').innerHTML = Data.watchlist.map(w => `<tr data-symbol="${U.esc(w.symbol)}"><td class="sym">${U.esc(w.symbol)}</td><td>${U.esc(w.name)}</td><td class="r">${U.currFull(w.price)}</td><td class="r ${w.change>=0?'up':'down'}">${U.currFull(w.change)}</td><td class="r ${w.changePct>=0?'up':'down'}">${U.pct(w.changePct)}</td><td class="r">${(w.volume/1e6).toFixed(1)}M</td><td class="r">${U.currFull(w.high52)}</td><td class="r">${U.currFull(w.low52)}</td><td><canvas class="inline-spark" id="wsp-${U.esc(w.symbol)}" width="60" height="20"></canvas></td><td><button class="btn-xs" title="Remove"><svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12"/></svg></button></td></tr>`).join('');
        Data.watchlist.forEach(w => { const c = U.$('#wsp-'+w.symbol); if (c) Charts.spark(c, Charts.randomWalk(15,100,.01), { width:60, height:20 }); });
    },

    renderPortfolios() {
        U.$('#portGrid').innerHTML = Data.portfolios.map(p => `<div class="port-card"><div class="port-head"><span class="port-name">${U.esc(p.name)}</span><span class="port-status">${U.esc(p.status)}</span></div><div class="port-stats"><div class="port-stat"><div class="port-stat-label">AUM</div><div class="port-stat-value">${U.currency(p.aum/1e6)}M</div></div><div class="port-stat"><div class="port-stat-label">YTD</div><div class="port-stat-value up">+${p.ytd}%</div></div><div class="port-stat"><div class="port-stat-label">Sharpe</div><div class="port-stat-value">${p.sharpe}</div></div></div><div class="port-chart"><canvas id="spark-${U.esc(p.id)}"></canvas></div><div class="port-actions"><button class="btn btn-sm btn-secondary">Details</button><button class="btn btn-sm btn-primary">Trade</button></div></div>`).join('');
        Data.portfolios.forEach(p => { const c = U.$('#spark-'+p.id); if (c) Charts.spark(c, Charts.randomWalk(20,100,.01), { width:c.parentElement.offsetWidth, height:40 }); });
    },

    renderPositions() {
        const pos = Data.getPositions();
        U.$('#posTbl').innerHTML = pos.map(p => `<tr data-symbol="${U.esc(p.symbol)}"><td class="sym">${U.esc(p.symbol)}</td><td>${U.esc(p.name)}</td><td class="r">${U.num(p.shares,0)}</td><td class="r">${U.currFull(p.cost)}</td><td class="r">${U.currFull(p.price)}</td><td class="r">${U.currency(p.value)}</td><td class="r ${p.pnl>=0?'up':'down'}">${U.currency(p.pnl)}</td><td class="r ${p.pnlPct>=0?'up':'down'}">${U.pct(p.pnlPct)}</td><td class="r">${U.num(p.weight)}%</td><td><canvas class="inline-spark" id="psp-${U.esc(p.symbol)}" width="60" height="20"></canvas></td></tr>`).join('');
        pos.forEach(p => { const c = U.$('#psp-'+p.symbol); if (c) Charts.spark(c, Charts.randomWalk(15,100,.01), { width:60, height:20 }); });
        const tv = U.sum(pos,'value'), tp = U.sum(pos,'pnl');
        U.$('#posFoot').innerHTML = `<tr><td colspan="5"><strong>Total</strong></td><td class="r"><strong>${U.currency(tv)}</strong></td><td class="r ${tp>=0?'up':'down'}"><strong>${U.currency(tp)}</strong></td><td colspan="3"></td></tr>`;
    },

    renderOrders(filter = 'all') {
        const ord = filter === 'all' ? Data.orders : filter === 'active' ? Data.orders.filter(o => o.status === 'Pending') : Data.orders.filter(o => o.status === 'Filled');
        U.$('#ordTbl').innerHTML = ord.map(o => {
            const sc = { Pending:'orange', Filled:'green', Cancelled:'fg-3' }[o.status];
            return `<tr><td>${U.esc(o.id)}</td><td>${U.time(o.time)}</td><td class="sym">${U.esc(o.symbol)}</td><td class="${o.side==='Buy'?'up':'down'}">${U.esc(o.side)}</td><td class="r">${o.qty}</td><td class="r">${U.currFull(o.price)}</td><td>${U.esc(o.type)}</td><td style="color:var(--${sc})">${U.esc(o.status)}</td><td><div class="bar-track" style="width:50px;display:inline-block"><div class="bar-fill" style="width:${o.fill}%;background:var(--${sc})"></div></div></td><td>${o.status==='Pending'?'<button class="btn btn-sm btn-secondary">Cancel</button>':''}</td></tr>`;
        }).join('');
    },

    renderTransactions() {
        U.$('#txTbl').innerHTML = Data.transactions.map(t => `<tr><td>${U.dateFull(t.date)}</td><td class="sym">${U.esc(t.symbol)}</td><td>${U.esc(t.type)}</td><td class="${t.side==='Buy'?'up':t.side==='Sell'?'down':''}">${U.esc(t.side)}</td><td class="r">${t.qty}</td><td class="r">${U.currFull(t.price)}</td><td class="r">${U.currency(t.amount)}</td><td class="r">${U.currFull(t.fees)}</td><td class="r ${t.net>=0?'up':'down'}">${U.currency(Math.abs(t.net))}</td></tr>`).join('');
    },

    renderPerformance() {
        U.$('#perfStats').innerHTML = Data.perfStats.map(s => `<div class="stat"><div class="stat-label">${U.esc(s.label)}</div><div class="stat-value ${s.pos?'up':''}">${s.pos?'+':''}${U.num(s.value)}${s.label.includes('Ratio')?'':'%'}</div></div>`).join('');
        this.renderTabContent('returns');
    },
    renderRisk() {
        U.$('#varGrid').innerHTML = Data.varMetrics.map(v => `<div class="var-item"><div class="var-label">${U.esc(v.label)}</div><div class="var-value">${U.currency(v.value)}</div></div>`).join('');
        this.renderTabContent('var');
    },
    renderBenchmark() {
        U.$('#benchStats').innerHTML = Data.benchStats.map(s => `<div class="stat"><div class="stat-label">${U.esc(s.label)}</div><div class="stat-value ${s.pos?'up':''}">${s.pos?'+':''}${U.num(s.value)}${s.label.includes('Alpha')||s.label.includes('Err')?'%':''}</div></div>`).join('');
        setTimeout(() => { const c = U.$('#benchChart'); if (c) Charts.line(c, { labels:Charts.dateLabels(180), datasets:[{ data:Charts.randomWalk(180,0,.008), color:'#3b82f6', fill:true },{ data:Array(180).fill(0), color:'#94a3b8' }] }, { height:300 }); }, 50);
    },
    renderAlerts() {
        const ic = { critical:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></svg>', warning:'<svg viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><path d="M12 9v4M12 17h.01"/></svg>', info:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>', success:'<svg viewBox="0 0 24 24"><path d="M20 6L9 17l-5-5"/></svg>' };
        U.$('#alertList').innerHTML = Data.alerts.map(a => `<div class="alert-item ${a.type}"><div class="alert-icon">${ic[a.type]||ic.info}</div><div class="alert-content"><div class="alert-title">${U.esc(a.title)}</div><div class="alert-desc">${U.esc(a.desc)}</div><div class="alert-time">${U.relTime(a.time)}</div></div><button class="btn btn-sm btn-secondary">Ack</button></div>`).join('');
    },

    renderTabContent(tab) {
        setTimeout(() => {
            switch (tab) {
                case 'returns':
                    const cc = U.$('#cumChart'); if (cc) Charts.line(cc, { labels:Charts.dateLabels(120), datasets:[{ data:Charts.randomWalk(120,100,.012), color:'#3b82f6', fill:true },{ data:Charts.randomWalk(120,100,.01), color:'#94a3b8' }] }, { height:250 });
                    const mc = U.$('#monChart'); if (mc) Charts.bar(mc, ['Jan','Feb','Mar','Apr','May','Jun'].map(l => ({ label:l, value:U.rand(-2,4) })).map(d => ({...d, color:d.value>=0?'#10b981':'#ef4444'})), { height:250 });
                    break;
                case 'attribution': const ac = U.$('#attrChart'); if (ac) Charts.bar(ac, Data.sectors.map(s => ({ label:s.name, value:U.rand(-1,3), color:s.color })), { height:250 }); break;
                case 'drawdown': const dc = U.$('#ddChart'); if (dc) Charts.line(dc, { labels:Charts.dateLabels(120), datasets:[{ data:Charts.randomWalk(120,0,.005).map(v => Math.min(v,0)), color:'#ef4444', fill:true }] }, { height:250 }); break;
                case 'var':
                    const vc = U.$('#varChart'); if (vc) Charts.distribution(vc, Array.from({length:50},()=>Math.random()*100), { varLine:42 });
                    const fc = U.$('#factorChart'); if (fc) Charts.bar(fc, Data.factors.map(f => ({ label:f.name, value:f.value, color:f.value>=0?'#3b82f6':'#ef4444' })), { height:200 });
                    break;
                case 'stress':
                    U.$('#stressTbl').innerHTML = Data.stressScenarios.map(s => { const sev = Math.abs(s.pct)>15?'critical':Math.abs(s.pct)>5?'warning':'info'; return `<tr><td>${U.esc(s.name)}</td><td class="r down">${U.currency(s.impact)}</td><td class="r down">${U.pct(s.pct)}</td><td><span class="badge" style="background:var(--${sev==='critical'?'red':sev==='warning'?'orange':'accent'}-bg);color:var(--${sev==='critical'?'red':sev==='warning'?'orange':'accent'})">${sev}</span></td></tr>`; }).join('');
                    break;
                case 'heatmap': const hc = U.$('#heatmapChart'); if (hc) Charts.heatmap(hc, Data.corrMatrix, Data.corrLabels, { cellSize:60, labelWidth:60 }); break;
            }
        }, 50);
    },

    sortTable(th) {
        const col = th.dataset.col; if (!col) return;
        const table = th.closest('table'), tbody = table.querySelector('tbody');
        const isAsc = th.classList.contains('asc');
        table.querySelectorAll('th').forEach(h => h.classList.remove('asc','desc'));
        th.classList.add(isAsc ? 'desc' : 'asc');
        const rows = Array.from(tbody.querySelectorAll('tr'));
        const idx = Array.from(th.parentElement.children).indexOf(th);
        rows.sort((a, b) => {
            let va = a.children[idx]?.textContent.trim().replace(/[$,%+]/g,'') || '';
            let vb = b.children[idx]?.textContent.trim().replace(/[$,%+]/g,'') || '';
            const na = parseFloat(va), nb = parseFloat(vb);
            if (!isNaN(na) && !isNaN(nb)) { va = na; vb = nb; }
            return (va < vb ? -1 : va > vb ? 1 : 0) * (isAsc ? -1 : 1);
        });
        rows.forEach(r => tbody.appendChild(r));
    },

    showCtxMenu(e, row) {
        const sym = row.dataset.symbol || row.querySelector('.sym')?.textContent || '';
        const menu = U.$('#ctxMenu');
        menu.innerHTML = `<div class="ctx-item" role="menuitem"><svg viewBox="0 0 24 24"><path d="M12 19V5M5 12l7-7 7 7"/></svg>Buy ${U.esc(sym)}</div><div class="ctx-item" role="menuitem"><svg viewBox="0 0 24 24"><path d="M12 5v14M19 12l-7 7-7-7"/></svg>Sell ${U.esc(sym)}</div><div class="ctx-sep"></div><div class="ctx-item" role="menuitem"><svg viewBox="0 0 24 24"><path d="M1 12s4-8 11-8 11 8 11 8"/><circle cx="12" cy="12" r="3"/></svg>Add to Watchlist</div><div class="ctx-item" role="menuitem"><svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><path d="M7 10l5 5 5-5"/></svg>Export Row</div>`;
        menu.style.left = Math.min(e.clientX, window.innerWidth - 180) + 'px';
        menu.style.top = Math.min(e.clientY, window.innerHeight - 160) + 'px';
        menu.classList.add('show');
        menu.querySelectorAll('.ctx-item').forEach(item => { item.onclick = () => { this.hideCtxMenu(); this.toast('info', item.textContent.trim()); }; });
    },
    hideCtxMenu() { U.$('#ctxMenu').classList.remove('show'); },

    toggleDrawer(open) {
        const d = U.$('#notifDrawer'), o = U.$('#drawerOverlay');
        if (open === undefined) open = !d.classList.contains('open');
        d.classList.toggle('open', open); o.classList.toggle('show', open);
        if (open) this.renderNotifs();
    },
    renderNotifs() {
        const ic = { critical:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></svg>', warning:'<svg viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/></svg>', info:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>', success:'<svg viewBox="0 0 24 24"><path d="M20 6L9 17l-5-5"/></svg>' };
        U.$('#notifList').innerHTML = Data.alerts.length ? Data.alerts.map(a => `<div class="alert-item ${a.type}" style="margin-bottom:6px"><div class="alert-icon">${ic[a.type]||ic.info}</div><div class="alert-content"><div class="alert-title">${U.esc(a.title)}</div><div class="alert-desc">${U.esc(a.desc)}</div><div class="alert-time">${U.relTime(a.time)}</div></div></div>`).join('') : '<div style="text-align:center;padding:40px;color:var(--fg-3)">No notifications</div>';
    },
    updateBadges() { const c = Data.alerts.filter(a => !a.read).length; U.$('#alertBadge').textContent = c; U.$('#alertBadge').style.display = c > 0 ? '' : 'none'; U.$('#notifDot').style.display = c > 0 ? '' : 'none'; },

    refresh() { this.toast('info', 'Refreshing...'); setTimeout(() => { this.render(); this.toast('success', 'Refreshed'); }, 500); },
    showModal(title, body, footer='') { U.$('#modalTitle').textContent = title; U.$('#modalBody').innerHTML = body; U.$('#modalFoot').innerHTML = footer; U.$('#modalBg').classList.add('show'); },
    closeModal() { U.$('#modalBg').classList.remove('show'); },
    showOrderModal() { this.showModal('New Order', '<div class="form-row"><label>Symbol</label><input type="text" placeholder="AAPL"></div><div class="form-row"><label>Side</label><select class="sel"><option>Buy</option><option>Sell</option></select></div><div class="form-row"><label>Qty</label><input type="number" placeholder="100"></div><div class="form-row"><label>Type</label><select class="sel"><option>Market</option><option>Limit</option><option>Stop</option></select></div><div class="form-row"><label>Price</label><input type="number" placeholder="0.00" step="0.01"></div>', '<button class="btn btn-secondary" onclick="App.closeModal()">Cancel</button><button class="btn btn-primary" onclick="App.closeModal();App.toast(\'success\',\'Submitted\')">Submit</button>'); },
    closeWidgetModal() { U.$('#widgetModalBg').classList.remove('show'); },
    saveWidgetConfig() { Widgets.saveConfig(); },
    // Column Visibility
    showColPopover(e, tableId) {
        const pop = U.$('#colPopover');
        const tbl = U.$('#' + tableId);
        if (!tbl) return;
        const ths = tbl.querySelectorAll('thead th');
        U.$('#colPopBody').innerHTML = Array.from(ths).map((th, i) => {
            const name = th.textContent.trim() || 'Col ' + (i+1);
            const hidden = th.classList.contains('col-hidden');
            return `<label class="popover-item"><input type="checkbox" data-col="${i}" ${hidden ? '' : 'checked'}> ${U.esc(name)}</label>`;
        }).join('');
        pop.style.left = Math.min(e.clientX, window.innerWidth - 200) + 'px';
        pop.style.top = Math.min(e.clientY + 10, window.innerHeight - 300) + 'px';
        pop.classList.add('show');
        pop.onclick = e => e.stopPropagation();
        pop.querySelectorAll('input').forEach(cb => {
            cb.onchange = () => {
                const idx = parseInt(cb.dataset.col);
                const cls = cb.checked ? 'remove' : 'add';
                tbl.querySelectorAll(`th:nth-child(${idx+1}), td:nth-child(${idx+1})`).forEach(cell => cell.classList[cls]('col-hidden'));
            };
        });
    },

    toast(type, msg) {
        const ic = { success:'<svg viewBox="0 0 24 24"><path d="M20 6L9 17l-5-5"/></svg>', error:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M15 9l-6 6M9 9l6 6"/></svg>', info:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>' };
        const el = document.createElement('div'); el.className = 'toast ' + type;
        el.innerHTML = `<div class="toast-icon">${ic[type]}</div><div class="toast-content"><div class="toast-title">${type[0].toUpperCase()+type.slice(1)}</div><div class="toast-msg">${U.esc(msg)}</div></div>`;
        U.$('#toasts').appendChild(el); setTimeout(() => { el.classList.add('hide'); setTimeout(() => el.remove(), 300); }, 3000);
    },

    // ========== Profile Management ==========
    async openProfileModal() {
        if (Connection.demoMode) {
            this.toast('info', 'Profile not available in demo mode');
            return;
        }
        
        // Setup tab switching
        document.querySelectorAll('.profile-tab').forEach(tab => {
            tab.onclick = () => {
                document.querySelectorAll('.profile-tab').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
                const show = tab.dataset.tab === 'info' ? 'profileInfo' : 'profilePassword';
                const hide = tab.dataset.tab === 'info' ? 'profilePassword' : 'profileInfo';
                U.$('#' + show).classList.remove('hide');
                U.$('#' + hide).classList.add('hide');
            };
        });
        
        // Load profile data
        const r = await Connection.getProfile();
        if (r.ok) {
            U.$('#profUsername').value = r.data.username || '';
            U.$('#profRole').value = r.data.role || '';
            U.$('#profDisplayName').value = r.data.display_name || '';
            U.$('#profEmail').value = r.data.email || '';
            U.$('#profCreated').value = r.data.created ? new Date(r.data.created).toLocaleDateString() : '';
            U.$('#profLastLogin').value = r.data.last_login ? new Date(r.data.last_login).toLocaleString() : '';
        } else {
            this.toast('error', r.error || 'Failed to load profile');
        }
        
        U.$('#profStatus').textContent = '';
        U.$('#profStatus').className = 'profile-status';
        U.$('#passStatus').textContent = '';
        U.$('#passStatus').className = 'profile-status';
        U.$('#profOldPass').value = '';
        U.$('#profNewPass').value = '';
        U.$('#profNewPass2').value = '';
        
        U.$('#profileModalBg').classList.add('show');
    },
    
    closeProfileModal() {
        U.$('#profileModalBg').classList.remove('show');
    },
    
    async saveProfile() {
        const displayName = U.$('#profDisplayName').value.trim();
        const email = U.$('#profEmail').value.trim();
        const status = U.$('#profStatus');
        
        status.textContent = 'Saving...';
        status.className = 'profile-status';
        
        const r = await Connection.updateProfile(displayName, email);
        if (r.ok) {
            status.textContent = 'Profile updated!';
            status.className = 'profile-status success';
            this.toast('success', 'Profile updated');
        } else {
            status.textContent = r.error || 'Failed to update';
            status.className = 'profile-status error';
        }
    },
    
    async changePassword() {
        const oldPass = U.$('#profOldPass').value;
        const newPass = U.$('#profNewPass').value;
        const newPass2 = U.$('#profNewPass2').value;
        const status = U.$('#passStatus');
        
        if (!oldPass) { status.textContent = 'Current password required'; status.className = 'profile-status error'; return; }
        if (!newPass || newPass.length < 4) { status.textContent = 'New password must be at least 4 characters'; status.className = 'profile-status error'; return; }
        if (newPass !== newPass2) { status.textContent = 'Passwords do not match'; status.className = 'profile-status error'; return; }
        
        status.textContent = 'Changing password...';
        status.className = 'profile-status';
        
        const r = await Connection.changePassword(oldPass, newPass);
        if (r.ok) {
            status.textContent = 'Password changed!';
            status.className = 'profile-status success';
            U.$('#profOldPass').value = '';
            U.$('#profNewPass').value = '';
            U.$('#profNewPass2').value = '';
            this.toast('success', 'Password changed');
        } else {
            status.textContent = r.error || 'Failed to change password';
            status.className = 'profile-status error';
        }
    },

    // ========== Admin User Management ==========
    adminUsers: [],
    editingUser: null,
    
    async renderAdminUsers() {
        if (!Connection.isAdmin()) {
            U.$('#content').innerHTML = '<div class="empty-state"><svg viewBox="0 0 24 24"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg><div class="empty-state-title">Access Denied</div><div class="empty-state-desc">Administrator access required</div></div>';
            return;
        }
        
        U.$('#content').innerHTML = `
            <div class="page-head"><h2>User Management</h2></div>
            <div class="admin-toolbar">
                <div class="admin-search">
                    <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
                    <input type="text" id="userSearch" placeholder="Search users...">
                </div>
                <button class="btn btn-primary" onclick="App.showCreateUserModal()">
                    <svg viewBox="0 0 24 24" style="width:14px;height:14px;margin-right:4px"><path d="M12 5v14M5 12h14"/></svg>
                    Add User
                </button>
            </div>
            <div class="users-grid" id="usersGrid">
                <div class="skeleton skel-box"></div>
                <div class="skeleton skel-box"></div>
                <div class="skeleton skel-box"></div>
            </div>
        `;
        
        // Load users
        const r = await Connection.getUsers();
        if (r.ok) {
            this.adminUsers = r.data;
            this.renderUsersGrid();
        } else {
            U.$('#usersGrid').innerHTML = `<div class="empty-state"><div class="empty-state-title">Failed to load users</div><div class="empty-state-desc">${U.esc(r.error)}</div></div>`;
        }
        
        // Search filter
        U.$('#userSearch').oninput = () => this.filterUsers();
    },
    
    renderUsersGrid() {
        const search = (U.$('#userSearch')?.value || '').toLowerCase();
        const filtered = this.adminUsers.filter(u => 
            u.username.toLowerCase().includes(search) || 
            (u.display_name || '').toLowerCase().includes(search) ||
            (u.email || '').toLowerCase().includes(search)
        );
        
        if (filtered.length === 0) {
            U.$('#usersGrid').innerHTML = '<div class="empty-state"><div class="empty-state-title">No users found</div></div>';
            return;
        }
        
        const roleBadge = role => {
            const cls = { Administrator: 'admin', Trader: 'trader', Analyst: 'analyst', Viewer: 'viewer' }[role] || 'viewer';
            return `<span class="user-card-badge ${cls}">${U.esc(role)}</span>`;
        };
        
        U.$('#usersGrid').innerHTML = filtered.map(u => `
            <div class="user-card-admin ${u.active ? '' : 'inactive'}" onclick="App.editUser('${U.attr(u.username)}')">
                <div class="user-card-header">
                    <div class="user-card-avatar">${U.esc((u.display_name || u.username).charAt(0).toUpperCase())}</div>
                    <div>
                        <div class="user-card-name">${U.esc(u.display_name || u.username)}</div>
                        <div class="user-card-username">@${U.esc(u.username)}</div>
                    </div>
                </div>
                <div class="user-card-meta">
                    ${roleBadge(u.role)}
                    <span class="user-card-badge ${u.active ? 'active' : 'inactive'}">${u.active ? 'Active' : 'Inactive'}</span>
                </div>
            </div>
        `).join('');
    },
    
    filterUsers() {
        this.renderUsersGrid();
    },
    
    async editUser(username) {
        const r = await Connection.getUser(username);
        if (!r.ok) {
            this.toast('error', r.error || 'Failed to load user');
            return;
        }
        
        this.editingUser = r.data;
        U.$('#editUserTitle').textContent = username;
        U.$('#editUsername').value = r.data.username;
        U.$('#editDisplayName').value = r.data.display_name || '';
        U.$('#editEmail').value = r.data.email || '';
        U.$('#editRole').value = r.data.role;
        U.$('#editActive').value = r.data.active ? 'true' : 'false';
        U.$('#editUserStatus').textContent = '';
        U.$('#editUserStatus').className = 'profile-status';
        
        U.$('#editUserModalBg').classList.add('show');
    },
    
    closeEditUserModal() {
        U.$('#editUserModalBg').classList.remove('show');
        this.editingUser = null;
    },
    
    async saveUser() {
        if (!this.editingUser) return;
        
        const data = {
            display_name: U.$('#editDisplayName').value.trim(),
            email: U.$('#editEmail').value.trim(),
            role: U.$('#editRole').value,
            active: U.$('#editActive').value
        };
        
        const status = U.$('#editUserStatus');
        status.textContent = 'Saving...';
        status.className = 'profile-status';
        
        const r = await Connection.updateUser(this.editingUser.username, data);
        if (r.ok) {
            status.textContent = 'User updated!';
            status.className = 'profile-status success';
            this.toast('success', 'User updated');
            // Refresh list
            const users = await Connection.getUsers();
            if (users.ok) {
                this.adminUsers = users.data;
                this.renderUsersGrid();
            }
            setTimeout(() => this.closeEditUserModal(), 1000);
        } else {
            status.textContent = r.error || 'Failed to update';
            status.className = 'profile-status error';
        }
    },
    
    async deleteUser() {
        if (!this.editingUser) return;
        
        if (!confirm(`Delete user "${this.editingUser.username}"? This cannot be undone.`)) return;
        
        const status = U.$('#editUserStatus');
        status.textContent = 'Deleting...';
        status.className = 'profile-status';
        
        const r = await Connection.deleteUser(this.editingUser.username);
        if (r.ok) {
            this.toast('success', 'User deleted');
            this.closeEditUserModal();
            // Refresh list
            const users = await Connection.getUsers();
            if (users.ok) {
                this.adminUsers = users.data;
                this.renderUsersGrid();
            }
        } else {
            status.textContent = r.error || 'Failed to delete';
            status.className = 'profile-status error';
        }
    },
    
    showCreateUserModal() {
        // Reuse edit modal for create
        this.editingUser = { username: '', isNew: true };
        U.$('#editUserTitle').textContent = 'New User';
        U.$('#editUsername').value = '';
        U.$('#editUsername').removeAttribute('readonly');
        U.$('#editUsername').classList.remove('input-readonly');
        U.$('#editDisplayName').value = '';
        U.$('#editEmail').value = '';
        U.$('#editRole').value = 'Viewer';
        U.$('#editActive').value = 'true';
        U.$('#editUserStatus').textContent = '';
        U.$('#editUserStatus').className = 'profile-status';
        
        U.$('#editUserModalBg').classList.add('show');
    },

    renderSettings() {
        // Update log entry count
        this.updateLogEntryCount();
        
        // Load current log level
        const levelSel = U.$('#logLevelSel');
        if (levelSel) {
            const level = window.Config?.get('logging.level') || 'INFO';
            levelSel.value = level;
        }
        
        // Load console output setting
        const consoleChk = U.$('#logConsole');
        if (consoleChk) {
            consoleChk.checked = window.Config?.get('logging.console') !== false;
        }
    },
    
    updateLogEntryCount() {
        const el = U.$('#logEntryCount');
        if (el && U.log._entries) {
            el.textContent = U.log._entries.length;
        }
    }
};

// Note: App.init() is called from index.html after Config.init() completes
