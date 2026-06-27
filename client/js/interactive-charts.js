// Metis Genie Platform v5.5.11
/* Metis Genie Platform Interactive Charts v5.5.11 */
'use strict';

const Charts = {
    currentSymbol: 'SPY',
    activeOverlays: new Set(['sma20', 'sma50', 'bb']),
    activeStudies: new Set(['volume', 'rsi']),
    activeTool: 'cursor',

    init() {
        this.initSymbolPills();
        this.initOverlayButtons();
        this.initStudyButtons();
        this.initDrawingTools();
        this.initSubPanelClose();
        this.initFullscreen();
    },

    initSymbolPills() {
        document.querySelectorAll('.sym-pill').forEach(pill => {
            pill.addEventListener('click', () => {
                document.querySelectorAll('.sym-pill').forEach(p => p.classList.remove('active'));
                pill.classList.add('active');
                this.currentSymbol = pill.dataset.sym;
                this.updateChartInfo(pill.dataset.sym);
            });
        });
    },

    updateChartInfo(sym) {
        const el = (id) => document.getElementById(id);
        if (el('cioSymbol')) el('cioSymbol').textContent = sym;
        // In production, this would fetch from API
        const names = {SPY:'SPDR S&P 500 ETF Trust',QQQ:'Invesco QQQ Trust',AAPL:'Apple Inc',MSFT:'Microsoft Corp',GOOGL:'Alphabet Inc',AMZN:'Amazon.com Inc',NVDA:'NVIDIA Corp',TSLA:'Tesla Inc'};
        if (el('cioName')) el('cioName').textContent = names[sym] || sym;
    },

    initOverlayButtons() {
        document.querySelectorAll('[data-overlay]').forEach(btn => {
            btn.addEventListener('click', () => {
                btn.classList.toggle('active');
                const overlay = btn.dataset.overlay;
                if (btn.classList.contains('active')) {
                    this.activeOverlays.add(overlay);
                } else {
                    this.activeOverlays.delete(overlay);
                }
            });
        });
    },

    initStudyButtons() {
        document.querySelectorAll('[data-study]').forEach(btn => {
            btn.addEventListener('click', () => {
                btn.classList.toggle('active');
                const study = btn.dataset.study;
                const panel = document.getElementById(study + 'Panel');
                if (btn.classList.contains('active')) {
                    this.activeStudies.add(study);
                    if (panel) panel.style.display = '';
                } else {
                    this.activeStudies.delete(study);
                    if (panel) panel.style.display = 'none';
                }
            });
        });
    },

    initDrawingTools() {
        document.querySelectorAll('.draw-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                const tool = btn.dataset.tool;
                if (tool === 'undo' || tool === 'clear') {
                    // Action tools
                    return;
                }
                document.querySelectorAll('.draw-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.activeTool = tool;
            });
        });
    },

    initSubPanelClose() {
        document.querySelectorAll('.sp-close').forEach(btn => {
            btn.addEventListener('click', () => {
                const panel = btn.dataset.panel;
                const el = document.getElementById(panel + 'Panel');
                if (el) el.style.display = 'none';
                // Deactivate corresponding study button
                const studyBtn = document.querySelector(`[data-study="${panel}"]`);
                if (studyBtn) {
                    studyBtn.classList.remove('active');
                    this.activeStudies.delete(panel);
                }
            });
        });
    },

    initFullscreen() {
        const btn = document.getElementById('fullscreenBtn');
        if (btn) {
            btn.addEventListener('click', () => {
                const chart = document.querySelector('.full-chart-area');
                if (chart) {
                    if (document.fullscreenElement) {
                        document.exitFullscreen();
                    } else {
                        chart.requestFullscreen().catch(() => {});
                    }
                }
            });
        }
    }
};

document.addEventListener('DOMContentLoaded', () => Charts.init());
