// Metis Genie Platform v5.5.11
/**
 * Metis Genie Platform v5.5.11 - Keyboard Manager
 * Shortcuts, table navigation, help modal
 * @author Bennie Shearer  @copyright 2026
 */
const Keyboard = {
    selectedRow: null,

    init() {
        document.addEventListener('keydown', e => this.handle(e));
        U.$('#kbdBtn')?.addEventListener('click', () => this.showHelp());
        U.$('#kbdClose')?.addEventListener('click', () => this.hideHelp());
        U.$('#kbdModalBg')?.addEventListener('click', e => { if (e.target === U.$('#kbdModalBg')) this.hideHelp(); });
    },

    handle(e) {
        const tag = e.target.tagName;
        const inInput = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';

        // Global shortcuts (work even in inputs)
        if (e.key === 'Escape') {
            if (this.isHelpOpen()) { this.hideHelp(); return; }
            if (Commands.isOpen()) { Commands.close(); return; }
            App.closeModal(); App.toggleDrawer(false); App.hideCtxMenu();
            U.$('#widgetModalBg').classList.remove('show');
            U.$('#colPopover').classList.remove('show');
            return;
        }
        if (e.ctrlKey && e.key === 'k') { e.preventDefault(); Commands.open(); return; }
        if (e.ctrlKey && e.key === 'b') { e.preventDefault(); U.$('#sidebar').classList.toggle('hide'); return; }
        if (e.ctrlKey && e.key === 'm') { e.preventDefault(); U.$('#sidebar').classList.toggle('mini'); return; }
        if (e.ctrlKey && e.key === 'e') { e.preventDefault(); this.exportCurrent(); return; }
        if (e.ctrlKey && e.key === 'l') { e.preventDefault(); U.log.togglePanel(); return; }

        // Non-input shortcuts
        if (inInput) return;

        if (e.key === '?') { e.preventDefault(); this.showHelp(); return; }

        // Table row navigation
        if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
            e.preventDefault();
            this.navigateTable(e.key === 'ArrowDown' ? 1 : -1);
            return;
        }
        if (e.key === 'Enter' && this.selectedRow) {
            e.preventDefault();
            const sym = this.selectedRow.dataset.symbol || this.selectedRow.querySelector('.sym')?.textContent;
            if (sym) App.toast('info', 'Selected: ' + sym);
        }
    },

    navigateTable(dir) {
        const page = U.$('.page.active');
        if (!page) return;
        const tbody = page.querySelector('.tbl tbody');
        if (!tbody) return;
        const rows = Array.from(tbody.querySelectorAll('tr'));
        if (!rows.length) return;

        let idx = this.selectedRow ? rows.indexOf(this.selectedRow) : -1;
        rows.forEach(r => r.classList.remove('selected'));

        idx = Math.max(0, Math.min(rows.length - 1, idx + dir));
        rows[idx].classList.add('selected');
        rows[idx].scrollIntoView({ block: 'nearest' });
        this.selectedRow = rows[idx];
    },

    exportCurrent() {
        const page = App.page;
        if (page === 'positions') { Export.toCSV(Export.positions(), 'positions.csv'); App.toast('success', 'Exported'); }
        else if (page === 'transactions') { Export.toCSV(Export.transactions(), 'transactions.csv'); App.toast('success', 'Exported'); }
        else if (page === 'dashboard') { Export.toCSV(Export.holdings(), 'holdings.csv'); App.toast('success', 'Exported'); }
        else App.toast('info', 'No export for this page');
    },

    showHelp() { U.$('#kbdModalBg').classList.add('show'); },
    hideHelp() { U.$('#kbdModalBg').classList.remove('show'); },
    isHelpOpen() { return U.$('#kbdModalBg')?.classList.contains('show'); },

    clearSelection() { if (this.selectedRow) { this.selectedRow.classList.remove('selected'); this.selectedRow = null; } }
};
window.Keyboard = Keyboard;
