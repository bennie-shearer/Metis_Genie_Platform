// Metis Genie Platform v5.3.1
/**
 * Metis Genie Platform v5.3.1 - Command Palette
 * Keyboard-driven command navigation
 * @author Bennie Shearer  @copyright 2026
 */
const Commands = {
    commands: [
        { id:'dashboard', label:'Go to Dashboard', icon:'<svg viewBox="0 0 24 24"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/></svg>', hint:'Nav', action:()=>App.navigate('dashboard') },
        { id:'watchlist', label:'Go to Watchlist', icon:'<svg viewBox="0 0 24 24"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>', hint:'Nav', action:()=>App.navigate('watchlist') },
        { id:'portfolios', label:'Go to Portfolios', icon:'<svg viewBox="0 0 24 24"><path d="M21 16V8l-7-4-7 4v8l7 4z"/></svg>', hint:'Nav', action:()=>App.navigate('portfolios') },
        { id:'positions', label:'Go to Positions', icon:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/></svg>', hint:'Nav', action:()=>App.navigate('positions') },
        { id:'orders', label:'Go to Orders', icon:'<svg viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/></svg>', hint:'Nav', action:()=>App.navigate('orders') },
        { id:'transactions', label:'Go to Transactions', icon:'<svg viewBox="0 0 24 24"><path d="M17 1l4 4-4 4"/><path d="M7 23l-4-4 4-4"/></svg>', hint:'Nav', action:()=>App.navigate('transactions') },
        { id:'performance', label:'Go to Performance', icon:'<svg viewBox="0 0 24 24"><path d="M22 12h-4l-3 9L9 3l-3 9H2"/></svg>', hint:'Nav', action:()=>App.navigate('performance') },
        { id:'risk', label:'Go to Risk Analytics', icon:'<svg viewBox="0 0 24 24"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>', hint:'Nav', action:()=>App.navigate('risk') },
        { id:'benchmark', label:'Go to Benchmark', icon:'<svg viewBox="0 0 24 24"><path d="M18 20V10M12 20V4M6 20v-6"/></svg>', hint:'Nav', action:()=>App.navigate('benchmark') },
        { id:'alerts', label:'Go to Alerts', icon:'<svg viewBox="0 0 24 24"><path d="M18 8A6 6 0 006 8c0 7-3 9-3 9h18s-3-2-3-9"/></svg>', hint:'Nav', action:()=>App.navigate('alerts') },
        { id:'settings', label:'Go to Settings', icon:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/></svg>', hint:'Nav', action:()=>App.navigate('settings') },
        { id:'theme', label:'Toggle Theme', icon:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="5"/></svg>', hint:'Ctrl+T', action:()=>App.toggleTheme() },
        { id:'refresh', label:'Refresh Data', icon:'<svg viewBox="0 0 24 24"><path d="M23 4v6h-6"/></svg>', hint:'Ctrl+R', action:()=>App.refresh() },
        { id:'neworder', label:'New Order', icon:'<svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg>', hint:'Action', action:()=>{Commands.close();App.showOrderModal()} },
        { id:'export-csv', label:'Export Holdings CSV', icon:'<svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><path d="M7 10l5 5 5-5"/></svg>', hint:'Export', action:()=>{Export.toCSV(Export.holdings(),'holdings.csv');App.toast('success','Exported')} },
        { id:'export-pos', label:'Export Positions CSV', icon:'<svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><path d="M7 10l5 5 5-5"/></svg>', hint:'Export', action:()=>{Export.toCSV(Export.positions(),'positions.csv');App.toast('success','Exported')} },
        { id:'sidebar', label:'Toggle Sidebar', icon:'<svg viewBox="0 0 24 24"><path d="M3 12h18M3 6h18M3 18h18"/></svg>', hint:'Ctrl+B', action:()=>U.$('#sidebar').classList.toggle('hide') },
        { id:'notif', label:'Open Notifications', icon:'<svg viewBox="0 0 24 24"><path d="M18 8A6 6 0 006 8c0 7-3 9-3 9h18s-3-2-3-9"/></svg>', hint:'', action:()=>App.toggleDrawer(true) },
    ],
    activeIdx: 0,
    
    open() {
        const o = U.$('#cmdOverlay');
        o.classList.add('show');
        const inp = U.$('#cmdInput');
        inp.value = '';
        inp.focus();
        this.activeIdx = 0;
        this.render('');
    },
    
    close() { U.$('#cmdOverlay').classList.remove('show'); },
    
    isOpen() { return U.$('#cmdOverlay').classList.contains('show'); },
    
    render(query) {
        const q = query.toLowerCase();
        const filtered = q ? this.commands.filter(c => c.label.toLowerCase().includes(q) || c.id.includes(q)) : this.commands;
        const el = U.$('#cmdResults');
        el.innerHTML = filtered.map((c, i) => `<div class="cmd-item${i === this.activeIdx ? ' active' : ''}" data-idx="${i}">${c.icon}<span>${c.label}</span><span class="cmd-hint">${c.hint}</span></div>`).join('');
        if (!filtered.length) el.innerHTML = '<div style="padding:16px;text-align:center;color:var(--fg-3);font-size:13px">No results</div>';
        this._filtered = filtered;
    },
    
    navigate(dir) {
        const len = (this._filtered || this.commands).length;
        if (!len) return;
        this.activeIdx = (this.activeIdx + dir + len) % len;
        this.render(U.$('#cmdInput').value);
        const active = U.$('.cmd-item.active');
        if (active) active.scrollIntoView({ block: 'nearest' });
    },
    
    execute() {
        const items = this._filtered || this.commands;
        if (items[this.activeIdx]) { items[this.activeIdx].action(); this.close(); }
    },
    
    init() {
        U.$('#cmdBtn').onclick = () => this.open();
        U.$('#cmdOverlay').onclick = e => { if (e.target === U.$('#cmdOverlay')) this.close(); };
        U.$('#cmdInput').oninput = e => { this.activeIdx = 0; this.render(e.target.value); };
        U.$('#cmdInput').onkeydown = e => {
            if (e.key === 'ArrowDown') { e.preventDefault(); this.navigate(1); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); this.navigate(-1); }
            else if (e.key === 'Enter') { e.preventDefault(); this.execute(); }
            else if (e.key === 'Escape') this.close();
        };
        U.on(U.$('#cmdResults'), 'click', '.cmd-item', (e, el) => {
            this.activeIdx = parseInt(el.dataset.idx);
            this.execute();
        });
    }
};
window.Commands = Commands;
