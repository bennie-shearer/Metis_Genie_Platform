// Metis Genie Platform v5.3.1
/**
 * Metis Genie Platform v5.3.1 - Widget Management
 * Drag-and-drop dashboard customization
 * @author Bennie Shearer  @copyright 2026
 */
const Widgets = {
    widgets: [
        { id:'perf', label:'Portfolio Performance', visible:true },
        { id:'alloc', label:'Asset Allocation', visible:true },
        { id:'hold', label:'Top Holdings', visible:true },
        { id:'act', label:'Activity', visible:true },
        { id:'risk', label:'Risk Metrics', visible:true },
        { id:'sector', label:'Sector Exposure', visible:true }
    ],
    dragItem: null,
    
    init() {
        // Load saved config
        const saved = U.store.get('widgets');
        if (saved) this.widgets = saved;
        this.applyVisibility();
        this.bindDrag();
        
        U.$('#configWidgets').onclick = () => this.openConfig();
    },
    
    bindDrag() {
        const grid = U.$('#dashGrid');
        if (!grid) return;
        
        grid.querySelectorAll('.drag-widget').forEach(el => {
            el.addEventListener('dragstart', e => {
                this.dragItem = el;
                el.classList.add('dragging');
                e.dataTransfer.effectAllowed = 'move';
                e.dataTransfer.setData('text/plain', el.dataset.widget);
            });
            el.addEventListener('dragend', () => {
                el.classList.remove('dragging');
                grid.querySelectorAll('.drag-over').forEach(o => o.classList.remove('drag-over'));
                this.dragItem = null;
                this.saveOrder();
            });
            el.addEventListener('dragover', e => {
                e.preventDefault();
                e.dataTransfer.dropEffect = 'move';
                if (el !== this.dragItem) el.classList.add('drag-over');
            });
            el.addEventListener('dragleave', () => el.classList.remove('drag-over'));
            el.addEventListener('drop', e => {
                e.preventDefault();
                el.classList.remove('drag-over');
                if (this.dragItem && this.dragItem !== el) {
                    const parent = el.parentNode;
                    const items = [...parent.children];
                    const fromIdx = items.indexOf(this.dragItem);
                    const toIdx = items.indexOf(el);
                    if (fromIdx < toIdx) parent.insertBefore(this.dragItem, el.nextSibling);
                    else parent.insertBefore(this.dragItem, el);
                }
            });
        });
    },
    
    saveOrder() {
        const grid = U.$('#dashGrid');
        if (!grid) return;
        const order = [...grid.querySelectorAll('.drag-widget')].map(el => el.dataset.widget);
        U.store.set('widgetOrder', order);
    },
    
    applyVisibility() {
        this.widgets.forEach(w => {
            const el = document.querySelector(`[data-widget="${w.id}"]`);
            if (el && el.classList.contains('drag-widget')) el.style.display = w.visible ? '' : 'none';
        });
    },
    
    openConfig() {
        const bg = U.$('#widgetModalBg');
        const list = U.$('#widgetList');
        list.innerHTML = this.widgets.map(w => `<div class="widget-item"><label class="toggle"><input type="checkbox" data-wid="${w.id}" ${w.visible?'checked':''}><span class="slider"></span></label><label>${w.label}</label></div>`).join('');
        bg.classList.add('show');
    },
    
    saveConfig() {
        U.$('#widgetList').querySelectorAll('input[type="checkbox"]').forEach(cb => {
            const w = this.widgets.find(w => w.id === cb.dataset.wid);
            if (w) w.visible = cb.checked;
        });
        U.store.set('widgets', this.widgets);
        this.applyVisibility();
        U.$('#widgetModalBg').classList.remove('show');
        App.toast('success', 'Dashboard layout saved');
    }
};

// Expose
window.Widgets = Widgets;
