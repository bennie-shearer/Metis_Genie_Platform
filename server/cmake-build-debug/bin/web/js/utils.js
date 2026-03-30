// Metis Genie Platform v5.3.1
/**
 * Metis Genie Platform v5.3.1 - Utility Functions
 * Pure Vanilla JS
 * @author Bennie Shearer  @copyright 2026
 */
const U = {
    $(s) { return document.querySelector(s); },
    $$(s) { return document.querySelectorAll(s); },
    esc(s) { if (s == null) return ''; const d = document.createElement('div'); d.textContent = String(s); return d.innerHTML; },
    attr(s) { if (s == null) return ''; return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); },
    currency(v, d = 0) { 
        const locale = window.Config?.get('ui.number_locale') || 'en-US';
        const curr = window.Config?.get('ui.currency') || 'USD';
        return new Intl.NumberFormat(locale, { style: 'currency', currency: curr, minimumFractionDigits: d, maximumFractionDigits: d }).format(v); 
    },
    currFull(v) { 
        const locale = window.Config?.get('ui.number_locale') || 'en-US';
        const curr = window.Config?.get('ui.currency') || 'USD';
        return new Intl.NumberFormat(locale, { style: 'currency', currency: curr }).format(v); 
    },
    num(v, d = 2) { 
        const locale = window.Config?.get('ui.number_locale') || 'en-US';
        return new Intl.NumberFormat(locale, { minimumFractionDigits: d, maximumFractionDigits: d }).format(v); 
    },
    pct(v, d = 2) { return (v >= 0 ? '+' : '') + v.toFixed(d) + '%'; },
    date(d) { return new Intl.DateTimeFormat('en-US', { month: 'short', day: 'numeric' }).format(d); },
    dateFull(d) { return new Intl.DateTimeFormat('en-US', { year:'numeric', month:'short', day:'numeric' }).format(d); },
    time(d) { return new Intl.DateTimeFormat('en-US', { hour: 'numeric', minute: '2-digit' }).format(d); },
    relTime(d) {
        const diff = Date.now() - d.getTime();
        const m = Math.floor(diff/60000), h = Math.floor(diff/3600000), dy = Math.floor(diff/86400000);
        if (m < 1) return 'Just now'; if (m < 60) return m+'m ago'; if (h < 24) return h+'h ago';
        if (dy < 7) return dy+'d ago'; return this.date(d);
    },
    shortId() { return Math.random().toString(36).substr(2, 8).toUpperCase(); },
    debounce(fn, ms) { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; },
    store: {
        get(k, d = null) { try { const v = localStorage.getItem('mg_'+k); return v ? JSON.parse(v) : d; } catch { return d; } },
        set(k, v) { try { localStorage.setItem('mg_'+k, JSON.stringify(v)); } catch (_) { console.debug('localStorage set error:', _); } },
        remove(k) { try { localStorage.removeItem('mg_'+k); } catch (_) { console.debug('localStorage remove error:', _); } },
    },
    
    // Enhanced logging system with config integration
    log: {
        LEVELS: { DEBUG: 0, INFO: 1, WARN: 2, ERROR: 3, NONE: 4 },
        _entries: [],
        _panel: null,
        _startTime: Date.now(),
        
        // Get current log level from Config or default
        _getLevel() {
            const levelStr = window.Config?.get('logging.level') || 'INFO';
            return this.LEVELS[levelStr.toUpperCase()] ?? 1;
        },
        
        // Check if logging is enabled
        _isEnabled() {
            return window.Config?.get('logging.enabled') !== false;
        },
        
        // Check if console output is enabled
        _isConsoleEnabled() {
            return window.Config?.get('logging.console') !== false;
        },
        
        // Check if timestamps should be shown
        _showTimestamps() {
            return window.Config?.get('logging.timestamps') !== false;
        },
        
        // Get max entries from config
        _getMaxEntries() {
            return window.Config?.get('logging.max_entries') || 1000;
        },
        
        // Format timestamp with milliseconds
        _timestamp() {
            const now = new Date();
            const pad = (n, w = 2) => String(n).padStart(w, '0');
            return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())} ` +
                   `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}.` +
                   `${pad(now.getMilliseconds(), 3)}`;
        },
        
        // Core emit function
        _emit(level, tag, msg) {
            if (!this._isEnabled()) return;
            
            const levelNum = this.LEVELS[level] ?? 1;
            const labels = ['DEBUG', 'INFO ', 'WARN ', 'ERROR'];
            const ts = this._timestamp();
            
            // Create entry
            const entry = { 
                ts, 
                level, 
                levelNum,
                tag, 
                msg,
                elapsed: Date.now() - this._startTime
            };
            
            // Store entry
            this._entries.push(entry);
            const maxEntries = this._getMaxEntries();
            while (this._entries.length > maxEntries) {
                this._entries.shift();
            }
            
            // Console output if enabled and meets level threshold
            if (this._isConsoleEnabled() && levelNum >= this._getLevel()) {
                const fn = levelNum >= 3 ? console.error : levelNum >= 2 ? console.warn : console.log;
                const prefix = this._showTimestamps() ? `${ts} ` : '';
                fn(`${prefix}[${labels[levelNum]}] [${tag}] ${msg}`);
            }
            
            // Update panel if visible
            this._updatePanel(entry, labels[levelNum]);
        },
        
        // Logging methods
        debug(tag, msg) { this._emit('DEBUG', tag, msg); },
        info(tag, msg) { this._emit('INFO', tag, msg); },
        warn(tag, msg) { this._emit('WARN', tag, msg); },
        error(tag, msg) { this._emit('ERROR', tag, msg); },
        
        // Get recent entries
        recent(n = 50) { return this._entries.slice(-n); },
        
        // Get all entries
        all() { return [...this._entries]; },
        
        // Clear log history
        clear() { 
            this._entries = []; 
            if (this._panel) {
                this._panel.querySelector('.log-content').innerHTML = '';
            }
        },
        
        // Set log level at runtime
        setLevel(level) {
            if (window.Config) {
                window.Config.set('logging.level', level.toUpperCase());
            }
        },
        
        // Export logs to downloadable file
        export() {
            const lines = this._entries.map(e => 
                `${e.ts} [${e.level.padEnd(5)}] [${e.tag}] ${e.msg}`
            );
            const content = lines.join('\n');
            const filename = `metis-genie-platform-client-${this._timestamp().replace(/[: ]/g, '-')}.log`;
            
            const blob = new Blob([content], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            
            this.info('Log', `Exported ${this._entries.length} entries to ${filename}`);
            return filename;
        },
        
        // Create on-screen log panel
        _createPanel() {
            if (this._panel) return this._panel;
            
            const panel = document.createElement('div');
            panel.id = 'log-panel';
            panel.innerHTML = `
                <div class="log-header">
                    <span>Console Log</span>
                    <div class="log-controls">
                        <button onclick="U.log.export()" title="Export logs">[Export]</button>
                        <button onclick="U.log.clear()" title="Clear logs">[Clear]</button>
                        <button onclick="U.log.hidePanel()" title="Close">[X]</button>
                    </div>
                </div>
                <div class="log-content"></div>
            `;
            
            const style = document.createElement('style');
            style.textContent = `
                #log-panel {
                    position: fixed;
                    bottom: 0;
                    left: 0;
                    right: 0;
                    height: 250px;
                    background: #1a1a2e;
                    color: #0f0;
                    font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
                    font-size: 11px;
                    z-index: 10000;
                    display: none;
                    flex-direction: column;
                    border-top: 2px solid #444;
                }
                #log-panel .log-header {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    padding: 4px 8px;
                    background: #2a2a4e;
                    border-bottom: 1px solid #444;
                }
                #log-panel .log-header span { font-weight: bold; color: #fff; }
                #log-panel .log-controls button {
                    background: none;
                    border: none;
                    color: #888;
                    cursor: pointer;
                    padding: 2px 6px;
                    font-size: 14px;
                }
                #log-panel .log-controls button:hover { color: #fff; }
                #log-panel .log-content {
                    flex: 1;
                    overflow-y: auto;
                    padding: 8px;
                }
                #log-panel .log-entry {
                    padding: 2px 0;
                    border-bottom: 1px solid #333;
                    white-space: pre-wrap;
                    word-break: break-all;
                }
                #log-panel .log-DEBUG { color: #888; }
                #log-panel .log-INFO { color: #0f0; }
                #log-panel .log-WARN { color: #ff0; }
                #log-panel .log-ERROR { color: #f44; }
            `;
            document.head.appendChild(style);
            document.body.appendChild(panel);
            
            this._panel = panel;
            return panel;
        },
        
        // Update panel with new entry
        _updatePanel(entry, levelLabel) {
            if (!this._panel || this._panel.style.display === 'none') return;
            
            const content = this._panel.querySelector('.log-content');
            const div = document.createElement('div');
            div.className = `log-entry log-${entry.level}`;
            div.textContent = `${entry.ts} [${levelLabel}] [${entry.tag}] ${entry.msg}`;
            content.appendChild(div);
            
            // Limit entries in panel
            while (content.children.length > 200) {
                content.removeChild(content.firstChild);
            }
            
            content.scrollTop = content.scrollHeight;
        },
        
        // Show log panel
        showPanel() {
            const panel = this._createPanel();
            panel.style.display = 'flex';
            
            // Populate with recent entries
            const content = panel.querySelector('.log-content');
            content.innerHTML = '';
            const labels = ['DEBUG', 'INFO ', 'WARN ', 'ERROR'];
            this._entries.slice(-100).forEach(e => {
                const div = document.createElement('div');
                div.className = `log-entry log-${e.level}`;
                div.textContent = `${e.ts} [${labels[e.levelNum]}] [${e.tag}] ${e.msg}`;
                content.appendChild(div);
            });
            content.scrollTop = content.scrollHeight;
            
            if (window.Config) {
                window.Config.set('logging.panel', true);
            }
        },
        
        // Hide log panel
        hidePanel() {
            if (this._panel) {
                this._panel.style.display = 'none';
            }
            if (window.Config) {
                window.Config.set('logging.panel', false);
            }
        },
        
        // Toggle panel
        togglePanel() {
            if (this._panel && this._panel.style.display !== 'none') {
                this.hidePanel();
            } else {
                this.showPanel();
            }
        },
        
        // Initialize from config
        init() {
            if (window.Config?.get('logging.panel')) {
                this.showPanel();
            }
            this.info('Log', 'Logger initialized');
        }
    },
    
    on(parent, evt, sel, fn) { parent.addEventListener(evt, e => { const t = e.target.closest(sel); if (t && parent.contains(t)) fn(e, t); }); },
    sortBy(arr, key, asc=true) { return [...arr].sort((a,b) => { const va=a[key],vb=b[key]; return (va<vb?-1:va>vb?1:0)*(asc?1:-1); }); },
    sum(arr, k) { return arr.reduce((a, i) => a + (i[k] || 0), 0); },
    rand(min, max) { return Math.random() * (max - min) + min; },
    colors: ['#3b82f6','#10b981','#f59e0b','#ef4444','#8b5cf6','#ec4899','#06b6d4','#84cc16'],
    getColor(i) { return this.colors[i % this.colors.length]; },
    clock() { return new Intl.DateTimeFormat('en-US', { hour:'numeric', minute:'2-digit', second:'2-digit' }).format(new Date()); }
};
window.U = U;
