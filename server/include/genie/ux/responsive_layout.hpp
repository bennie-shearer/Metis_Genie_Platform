/**
 * @file responsive_layout.hpp
 * @brief Mobile-responsive layout CSS and component framework
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: User Experience - Create mobile-responsive layout
 */

#ifndef GENIE_UX_RESPONSIVE_LAYOUT_HPP
#define GENIE_UX_RESPONSIVE_LAYOUT_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>

namespace genie {
namespace ux {

/**
 * @brief Breakpoint definitions
 */
struct Breakpoints {
    static constexpr int XS = 0;      // Extra small (phones)
    static constexpr int SM = 576;    // Small (landscape phones)
    static constexpr int MD = 768;    // Medium (tablets)
    static constexpr int LG = 992;    // Large (desktops)
    static constexpr int XL = 1200;   // Extra large (large desktops)
    static constexpr int XXL = 1400;  // Extra extra large
};

/**
 * @brief CSS Framework generator for responsive layouts
 */
class ResponsiveCSS {
public:
    /**
     * @brief Generate complete responsive CSS framework
     */
    static std::string generate_framework() {
        std::ostringstream css;
        
        // CSS Reset and base styles
        css << generate_reset();
        css << generate_variables();
        css << generate_typography();
        css << generate_grid_system();
        css << generate_utilities();
        css << generate_components();
        css << generate_trading_components();
        css << generate_dark_theme();
        
        return css.str();
    }
    
    /**
     * @brief Generate CSS variables
     */
    static std::string generate_variables() {
        return R"(
/* CSS Variables */
:root {
    /* Colors */
    --color-primary: #00d4aa;
    --color-primary-dark: #00b894;
    --color-secondary: #6c5ce7;
    --color-success: #00b894;
    --color-danger: #d63031;
    --color-warning: #fdcb6e;
    --color-info: #0984e3;
    
    /* Background colors */
    --bg-dark: #1a1a2e;
    --bg-darker: #16213e;
    --bg-darkest: #0f0f1a;
    --bg-card: #16213e;
    --bg-input: #0f3460;
    
    /* Text colors */
    --text-primary: #ffffff;
    --text-secondary: #a0a0b0;
    --text-muted: #6c6c7c;
    
    /* Spacing */
    --spacing-xs: 0.25rem;
    --spacing-sm: 0.5rem;
    --spacing-md: 1rem;
    --spacing-lg: 1.5rem;
    --spacing-xl: 2rem;
    --spacing-xxl: 3rem;
    
    /* Border radius */
    --radius-sm: 4px;
    --radius-md: 8px;
    --radius-lg: 12px;
    --radius-xl: 16px;
    --radius-full: 9999px;
    
    /* Shadows */
    --shadow-sm: 0 1px 2px rgba(0, 0, 0, 0.3);
    --shadow-md: 0 4px 6px rgba(0, 0, 0, 0.3);
    --shadow-lg: 0 10px 15px rgba(0, 0, 0, 0.3);
    --shadow-xl: 0 20px 25px rgba(0, 0, 0, 0.3);
    
    /* Transitions */
    --transition-fast: 0.15s ease;
    --transition-normal: 0.3s ease;
    --transition-slow: 0.5s ease;
    
    /* Z-index layers */
    --z-dropdown: 100;
    --z-sticky: 200;
    --z-fixed: 300;
    --z-modal-backdrop: 400;
    --z-modal: 500;
    --z-tooltip: 600;
    --z-toast: 700;
    
    /* Typography */
    --font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
    --font-mono: 'SF Mono', Monaco, 'Cascadia Code', monospace;
    --font-size-xs: 0.75rem;
    --font-size-sm: 0.875rem;
    --font-size-base: 1rem;
    --font-size-lg: 1.125rem;
    --font-size-xl: 1.25rem;
    --font-size-2xl: 1.5rem;
    --font-size-3xl: 1.875rem;
    --font-size-4xl: 2.25rem;
    
    /* Line heights */
    --line-height-tight: 1.25;
    --line-height-normal: 1.5;
    --line-height-relaxed: 1.75;
    
    /* Container widths */
    --container-sm: 540px;
    --container-md: 720px;
    --container-lg: 960px;
    --container-xl: 1140px;
    --container-xxl: 1320px;
}
)";
    }
    
    /**
     * @brief Generate CSS reset
     */
    static std::string generate_reset() {
        return R"(
/* CSS Reset */
*, *::before, *::after {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

html {
    font-size: 16px;
    -webkit-text-size-adjust: 100%;
    -webkit-tap-highlight-color: transparent;
}

body {
    font-family: var(--font-family);
    font-size: var(--font-size-base);
    line-height: var(--line-height-normal);
    color: var(--text-primary);
    background-color: var(--bg-dark);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
}

img, picture, video, canvas, svg {
    display: block;
    max-width: 100%;
}

input, button, textarea, select {
    font: inherit;
}

a {
    color: var(--color-primary);
    text-decoration: none;
}

a:hover {
    text-decoration: underline;
}

button {
    cursor: pointer;
    border: none;
    background: none;
}

ul, ol {
    list-style: none;
}

table {
    border-collapse: collapse;
    width: 100%;
}
)";
    }
    
    /**
     * @brief Generate typography styles
     */
    static std::string generate_typography() {
        return R"(
/* Typography */
h1, h2, h3, h4, h5, h6 {
    font-weight: 600;
    line-height: var(--line-height-tight);
    color: var(--text-primary);
}

h1 { font-size: var(--font-size-4xl); }
h2 { font-size: var(--font-size-3xl); }
h3 { font-size: var(--font-size-2xl); }
h4 { font-size: var(--font-size-xl); }
h5 { font-size: var(--font-size-lg); }
h6 { font-size: var(--font-size-base); }

@media (max-width: 768px) {
    h1 { font-size: var(--font-size-3xl); }
    h2 { font-size: var(--font-size-2xl); }
    h3 { font-size: var(--font-size-xl); }
    h4 { font-size: var(--font-size-lg); }
}

.text-xs { font-size: var(--font-size-xs); }
.text-sm { font-size: var(--font-size-sm); }
.text-base { font-size: var(--font-size-base); }
.text-lg { font-size: var(--font-size-lg); }
.text-xl { font-size: var(--font-size-xl); }
.text-2xl { font-size: var(--font-size-2xl); }

.text-primary { color: var(--text-primary); }
.text-secondary { color: var(--text-secondary); }
.text-muted { color: var(--text-muted); }
.text-success { color: var(--color-success); }
.text-danger { color: var(--color-danger); }
.text-warning { color: var(--color-warning); }

.font-normal { font-weight: 400; }
.font-medium { font-weight: 500; }
.font-semibold { font-weight: 600; }
.font-bold { font-weight: 700; }

.text-left { text-align: left; }
.text-center { text-align: center; }
.text-right { text-align: right; }

.truncate {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.mono { font-family: var(--font-mono); }
)";
    }
    
    /**
     * @brief Generate responsive grid system
     */
    static std::string generate_grid_system() {
        return R"(
/* Container */
.container {
    width: 100%;
    margin-left: auto;
    margin-right: auto;
    padding-left: var(--spacing-md);
    padding-right: var(--spacing-md);
}

@media (min-width: 576px) { .container { max-width: var(--container-sm); } }
@media (min-width: 768px) { .container { max-width: var(--container-md); } }
@media (min-width: 992px) { .container { max-width: var(--container-lg); } }
@media (min-width: 1200px) { .container { max-width: var(--container-xl); } }
@media (min-width: 1400px) { .container { max-width: var(--container-xxl); } }

.container-fluid {
    width: 100%;
    padding-left: var(--spacing-md);
    padding-right: var(--spacing-md);
}

/* Flexbox Grid */
.row {
    display: flex;
    flex-wrap: wrap;
    margin-left: calc(var(--spacing-md) * -0.5);
    margin-right: calc(var(--spacing-md) * -0.5);
}

.col {
    flex: 1 0 0%;
    padding-left: calc(var(--spacing-md) * 0.5);
    padding-right: calc(var(--spacing-md) * 0.5);
}

/* Column sizes */
.col-1 { flex: 0 0 8.333333%; max-width: 8.333333%; }
.col-2 { flex: 0 0 16.666667%; max-width: 16.666667%; }
.col-3 { flex: 0 0 25%; max-width: 25%; }
.col-4 { flex: 0 0 33.333333%; max-width: 33.333333%; }
.col-5 { flex: 0 0 41.666667%; max-width: 41.666667%; }
.col-6 { flex: 0 0 50%; max-width: 50%; }
.col-7 { flex: 0 0 58.333333%; max-width: 58.333333%; }
.col-8 { flex: 0 0 66.666667%; max-width: 66.666667%; }
.col-9 { flex: 0 0 75%; max-width: 75%; }
.col-10 { flex: 0 0 83.333333%; max-width: 83.333333%; }
.col-11 { flex: 0 0 91.666667%; max-width: 91.666667%; }
.col-12 { flex: 0 0 100%; max-width: 100%; }

/* Small screens */
@media (min-width: 576px) {
    .col-sm-1 { flex: 0 0 8.333333%; max-width: 8.333333%; }
    .col-sm-2 { flex: 0 0 16.666667%; max-width: 16.666667%; }
    .col-sm-3 { flex: 0 0 25%; max-width: 25%; }
    .col-sm-4 { flex: 0 0 33.333333%; max-width: 33.333333%; }
    .col-sm-5 { flex: 0 0 41.666667%; max-width: 41.666667%; }
    .col-sm-6 { flex: 0 0 50%; max-width: 50%; }
    .col-sm-7 { flex: 0 0 58.333333%; max-width: 58.333333%; }
    .col-sm-8 { flex: 0 0 66.666667%; max-width: 66.666667%; }
    .col-sm-9 { flex: 0 0 75%; max-width: 75%; }
    .col-sm-10 { flex: 0 0 83.333333%; max-width: 83.333333%; }
    .col-sm-11 { flex: 0 0 91.666667%; max-width: 91.666667%; }
    .col-sm-12 { flex: 0 0 100%; max-width: 100%; }
}

/* Medium screens */
@media (min-width: 768px) {
    .col-md-1 { flex: 0 0 8.333333%; max-width: 8.333333%; }
    .col-md-2 { flex: 0 0 16.666667%; max-width: 16.666667%; }
    .col-md-3 { flex: 0 0 25%; max-width: 25%; }
    .col-md-4 { flex: 0 0 33.333333%; max-width: 33.333333%; }
    .col-md-5 { flex: 0 0 41.666667%; max-width: 41.666667%; }
    .col-md-6 { flex: 0 0 50%; max-width: 50%; }
    .col-md-7 { flex: 0 0 58.333333%; max-width: 58.333333%; }
    .col-md-8 { flex: 0 0 66.666667%; max-width: 66.666667%; }
    .col-md-9 { flex: 0 0 75%; max-width: 75%; }
    .col-md-10 { flex: 0 0 83.333333%; max-width: 83.333333%; }
    .col-md-11 { flex: 0 0 91.666667%; max-width: 91.666667%; }
    .col-md-12 { flex: 0 0 100%; max-width: 100%; }
}

/* Large screens */
@media (min-width: 992px) {
    .col-lg-1 { flex: 0 0 8.333333%; max-width: 8.333333%; }
    .col-lg-2 { flex: 0 0 16.666667%; max-width: 16.666667%; }
    .col-lg-3 { flex: 0 0 25%; max-width: 25%; }
    .col-lg-4 { flex: 0 0 33.333333%; max-width: 33.333333%; }
    .col-lg-5 { flex: 0 0 41.666667%; max-width: 41.666667%; }
    .col-lg-6 { flex: 0 0 50%; max-width: 50%; }
    .col-lg-7 { flex: 0 0 58.333333%; max-width: 58.333333%; }
    .col-lg-8 { flex: 0 0 66.666667%; max-width: 66.666667%; }
    .col-lg-9 { flex: 0 0 75%; max-width: 75%; }
    .col-lg-10 { flex: 0 0 83.333333%; max-width: 83.333333%; }
    .col-lg-11 { flex: 0 0 91.666667%; max-width: 91.666667%; }
    .col-lg-12 { flex: 0 0 100%; max-width: 100%; }
}

/* Extra large screens */
@media (min-width: 1200px) {
    .col-xl-1 { flex: 0 0 8.333333%; max-width: 8.333333%; }
    .col-xl-2 { flex: 0 0 16.666667%; max-width: 16.666667%; }
    .col-xl-3 { flex: 0 0 25%; max-width: 25%; }
    .col-xl-4 { flex: 0 0 33.333333%; max-width: 33.333333%; }
    .col-xl-5 { flex: 0 0 41.666667%; max-width: 41.666667%; }
    .col-xl-6 { flex: 0 0 50%; max-width: 50%; }
    .col-xl-7 { flex: 0 0 58.333333%; max-width: 58.333333%; }
    .col-xl-8 { flex: 0 0 66.666667%; max-width: 66.666667%; }
    .col-xl-9 { flex: 0 0 75%; max-width: 75%; }
    .col-xl-10 { flex: 0 0 83.333333%; max-width: 83.333333%; }
    .col-xl-11 { flex: 0 0 91.666667%; max-width: 91.666667%; }
    .col-xl-12 { flex: 0 0 100%; max-width: 100%; }
}

/* CSS Grid */
.grid { display: grid; gap: var(--spacing-md); }
.grid-cols-1 { grid-template-columns: repeat(1, 1fr); }
.grid-cols-2 { grid-template-columns: repeat(2, 1fr); }
.grid-cols-3 { grid-template-columns: repeat(3, 1fr); }
.grid-cols-4 { grid-template-columns: repeat(4, 1fr); }
.grid-cols-6 { grid-template-columns: repeat(6, 1fr); }
.grid-cols-12 { grid-template-columns: repeat(12, 1fr); }

@media (min-width: 768px) {
    .md\:grid-cols-2 { grid-template-columns: repeat(2, 1fr); }
    .md\:grid-cols-3 { grid-template-columns: repeat(3, 1fr); }
    .md\:grid-cols-4 { grid-template-columns: repeat(4, 1fr); }
}

@media (min-width: 992px) {
    .lg\:grid-cols-3 { grid-template-columns: repeat(3, 1fr); }
    .lg\:grid-cols-4 { grid-template-columns: repeat(4, 1fr); }
    .lg\:grid-cols-6 { grid-template-columns: repeat(6, 1fr); }
}
)";
    }
    
    /**
     * @brief Generate utility classes
     */
    static std::string generate_utilities() {
        return R"(
/* Display utilities */
.d-none { display: none !important; }
.d-block { display: block !important; }
.d-flex { display: flex !important; }
.d-grid { display: grid !important; }
.d-inline { display: inline !important; }
.d-inline-block { display: inline-block !important; }
.d-inline-flex { display: inline-flex !important; }

@media (max-width: 575px) { .d-xs-none { display: none !important; } }
@media (min-width: 576px) { .d-sm-none { display: none !important; } .d-sm-block { display: block !important; } }
@media (min-width: 768px) { .d-md-none { display: none !important; } .d-md-block { display: block !important; } .d-md-flex { display: flex !important; } }
@media (min-width: 992px) { .d-lg-none { display: none !important; } .d-lg-block { display: block !important; } .d-lg-flex { display: flex !important; } }
@media (min-width: 1200px) { .d-xl-none { display: none !important; } .d-xl-block { display: block !important; } }

/* Flexbox utilities */
.flex-row { flex-direction: row; }
.flex-column { flex-direction: column; }
.flex-wrap { flex-wrap: wrap; }
.flex-nowrap { flex-wrap: nowrap; }
.flex-grow-1 { flex-grow: 1; }
.flex-shrink-0 { flex-shrink: 0; }

.justify-start { justify-content: flex-start; }
.justify-end { justify-content: flex-end; }
.justify-center { justify-content: center; }
.justify-between { justify-content: space-between; }
.justify-around { justify-content: space-around; }
.justify-evenly { justify-content: space-evenly; }

.items-start { align-items: flex-start; }
.items-end { align-items: flex-end; }
.items-center { align-items: center; }
.items-baseline { align-items: baseline; }
.items-stretch { align-items: stretch; }

.gap-1 { gap: var(--spacing-xs); }
.gap-2 { gap: var(--spacing-sm); }
.gap-3 { gap: var(--spacing-md); }
.gap-4 { gap: var(--spacing-lg); }
.gap-5 { gap: var(--spacing-xl); }

/* Spacing utilities */
.m-0 { margin: 0; }
.m-1 { margin: var(--spacing-xs); }
.m-2 { margin: var(--spacing-sm); }
.m-3 { margin: var(--spacing-md); }
.m-4 { margin: var(--spacing-lg); }
.m-5 { margin: var(--spacing-xl); }
.m-auto { margin: auto; }

.mx-auto { margin-left: auto; margin-right: auto; }
.my-auto { margin-top: auto; margin-bottom: auto; }

.mt-0 { margin-top: 0; }
.mt-1 { margin-top: var(--spacing-xs); }
.mt-2 { margin-top: var(--spacing-sm); }
.mt-3 { margin-top: var(--spacing-md); }
.mt-4 { margin-top: var(--spacing-lg); }
.mt-5 { margin-top: var(--spacing-xl); }

.mb-0 { margin-bottom: 0; }
.mb-1 { margin-bottom: var(--spacing-xs); }
.mb-2 { margin-bottom: var(--spacing-sm); }
.mb-3 { margin-bottom: var(--spacing-md); }
.mb-4 { margin-bottom: var(--spacing-lg); }
.mb-5 { margin-bottom: var(--spacing-xl); }

.p-0 { padding: 0; }
.p-1 { padding: var(--spacing-xs); }
.p-2 { padding: var(--spacing-sm); }
.p-3 { padding: var(--spacing-md); }
.p-4 { padding: var(--spacing-lg); }
.p-5 { padding: var(--spacing-xl); }

.px-1 { padding-left: var(--spacing-xs); padding-right: var(--spacing-xs); }
.px-2 { padding-left: var(--spacing-sm); padding-right: var(--spacing-sm); }
.px-3 { padding-left: var(--spacing-md); padding-right: var(--spacing-md); }
.px-4 { padding-left: var(--spacing-lg); padding-right: var(--spacing-lg); }

.py-1 { padding-top: var(--spacing-xs); padding-bottom: var(--spacing-xs); }
.py-2 { padding-top: var(--spacing-sm); padding-bottom: var(--spacing-sm); }
.py-3 { padding-top: var(--spacing-md); padding-bottom: var(--spacing-md); }
.py-4 { padding-top: var(--spacing-lg); padding-bottom: var(--spacing-lg); }

/* Width/Height utilities */
.w-full { width: 100%; }
.w-auto { width: auto; }
.w-50 { width: 50%; }
.h-full { height: 100%; }
.h-auto { height: auto; }
.h-screen { height: 100vh; }
.min-h-screen { min-height: 100vh; }

/* Position utilities */
.relative { position: relative; }
.absolute { position: absolute; }
.fixed { position: fixed; }
.sticky { position: sticky; }
.top-0 { top: 0; }
.right-0 { right: 0; }
.bottom-0 { bottom: 0; }
.left-0 { left: 0; }
.inset-0 { top: 0; right: 0; bottom: 0; left: 0; }

/* Border utilities */
.border { border: 1px solid rgba(255, 255, 255, 0.1); }
.border-0 { border: 0; }
.border-t { border-top: 1px solid rgba(255, 255, 255, 0.1); }
.border-b { border-bottom: 1px solid rgba(255, 255, 255, 0.1); }
.rounded { border-radius: var(--radius-md); }
.rounded-sm { border-radius: var(--radius-sm); }
.rounded-lg { border-radius: var(--radius-lg); }
.rounded-full { border-radius: var(--radius-full); }

/* Shadow utilities */
.shadow { box-shadow: var(--shadow-md); }
.shadow-sm { box-shadow: var(--shadow-sm); }
.shadow-lg { box-shadow: var(--shadow-lg); }
.shadow-none { box-shadow: none; }

/* Overflow */
.overflow-hidden { overflow: hidden; }
.overflow-auto { overflow: auto; }
.overflow-scroll { overflow: scroll; }
.overflow-x-auto { overflow-x: auto; }
.overflow-y-auto { overflow-y: auto; }

/* Cursor */
.cursor-pointer { cursor: pointer; }
.cursor-default { cursor: default; }
.cursor-not-allowed { cursor: not-allowed; }

/* Visibility */
.visible { visibility: visible; }
.invisible { visibility: hidden; }
.opacity-0 { opacity: 0; }
.opacity-50 { opacity: 0.5; }
.opacity-100 { opacity: 1; }

/* Transitions */
.transition { transition: all var(--transition-normal); }
.transition-fast { transition: all var(--transition-fast); }
.transition-slow { transition: all var(--transition-slow); }
)";
    }
    
    /**
     * @brief Generate component styles
     */
    static std::string generate_components() {
        return R"(
/* Buttons */
.btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: var(--spacing-sm) var(--spacing-md);
    font-size: var(--font-size-sm);
    font-weight: 500;
    line-height: 1.5;
    border-radius: var(--radius-md);
    transition: all var(--transition-fast);
    cursor: pointer;
    white-space: nowrap;
    user-select: none;
}

.btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.btn-primary {
    background: var(--color-primary);
    color: var(--bg-dark);
}

.btn-primary:hover:not(:disabled) {
    background: var(--color-primary-dark);
}

.btn-secondary {
    background: var(--bg-input);
    color: var(--text-primary);
    border: 1px solid rgba(255, 255, 255, 0.1);
}

.btn-secondary:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.1);
}

.btn-danger {
    background: var(--color-danger);
    color: white;
}

.btn-success {
    background: var(--color-success);
    color: white;
}

.btn-sm {
    padding: var(--spacing-xs) var(--spacing-sm);
    font-size: var(--font-size-xs);
}

.btn-lg {
    padding: var(--spacing-md) var(--spacing-lg);
    font-size: var(--font-size-lg);
}

.btn-block {
    display: flex;
    width: 100%;
}

/* Cards */
.card {
    background: var(--bg-card);
    border-radius: var(--radius-lg);
    padding: var(--spacing-lg);
    box-shadow: var(--shadow-md);
}

.card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-bottom: var(--spacing-md);
    margin-bottom: var(--spacing-md);
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.card-title {
    font-size: var(--font-size-lg);
    font-weight: 600;
    color: var(--color-primary);
    margin: 0;
}

.card-body {
    padding: 0;
}

.card-footer {
    padding-top: var(--spacing-md);
    margin-top: var(--spacing-md);
    border-top: 1px solid rgba(255, 255, 255, 0.1);
}

/* Forms */
.form-group {
    margin-bottom: var(--spacing-md);
}

.form-label {
    display: block;
    margin-bottom: var(--spacing-xs);
    font-size: var(--font-size-sm);
    font-weight: 500;
    color: var(--text-secondary);
}

.form-control {
    display: block;
    width: 100%;
    padding: var(--spacing-sm) var(--spacing-md);
    font-size: var(--font-size-base);
    color: var(--text-primary);
    background: var(--bg-input);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: var(--radius-md);
    transition: border-color var(--transition-fast), box-shadow var(--transition-fast);
}

.form-control:focus {
    outline: none;
    border-color: var(--color-primary);
    box-shadow: 0 0 0 3px rgba(0, 212, 170, 0.2);
}

.form-control::placeholder {
    color: var(--text-muted);
}

.form-select {
    appearance: none;
    background-image: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3e%3cpath fill='none' stroke='%23a0a0b0' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='m2 5 6 6 6-6'/%3e%3c/svg%3e");
    background-repeat: no-repeat;
    background-position: right 0.75rem center;
    background-size: 16px 12px;
    padding-right: 2.5rem;
}

/* Tables */
.table {
    width: 100%;
    border-collapse: collapse;
}

.table th,
.table td {
    padding: var(--spacing-sm) var(--spacing-md);
    text-align: left;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.table th {
    font-weight: 600;
    color: var(--text-secondary);
    font-size: var(--font-size-sm);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.table tbody tr:hover {
    background: rgba(255, 255, 255, 0.02);
}

.table-responsive {
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
}

/* Alerts */
.alert {
    padding: var(--spacing-md);
    border-radius: var(--radius-md);
    margin-bottom: var(--spacing-md);
}

.alert-success {
    background: rgba(0, 184, 148, 0.1);
    border: 1px solid var(--color-success);
    color: var(--color-success);
}

.alert-danger {
    background: rgba(214, 48, 49, 0.1);
    border: 1px solid var(--color-danger);
    color: var(--color-danger);
}

.alert-warning {
    background: rgba(253, 203, 110, 0.1);
    border: 1px solid var(--color-warning);
    color: var(--color-warning);
}

.alert-info {
    background: rgba(9, 132, 227, 0.1);
    border: 1px solid var(--color-info);
    color: var(--color-info);
}

/* Badges */
.badge {
    display: inline-flex;
    align-items: center;
    padding: 0.25em 0.5em;
    font-size: var(--font-size-xs);
    font-weight: 600;
    border-radius: var(--radius-sm);
}

.badge-primary { background: var(--color-primary); color: var(--bg-dark); }
.badge-success { background: var(--color-success); color: white; }
.badge-danger { background: var(--color-danger); color: white; }
.badge-warning { background: var(--color-warning); color: var(--bg-dark); }

/* Modal */
.modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.7);
    z-index: var(--z-modal-backdrop);
    display: flex;
    align-items: center;
    justify-content: center;
    padding: var(--spacing-md);
}

.modal {
    background: var(--bg-card);
    border-radius: var(--radius-lg);
    width: 100%;
    max-width: 500px;
    max-height: 90vh;
    overflow: auto;
    z-index: var(--z-modal);
}

.modal-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: var(--spacing-lg);
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.modal-body {
    padding: var(--spacing-lg);
}

.modal-footer {
    display: flex;
    justify-content: flex-end;
    gap: var(--spacing-sm);
    padding: var(--spacing-lg);
    border-top: 1px solid rgba(255, 255, 255, 0.1);
}

/* Tabs */
.tabs {
    display: flex;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    margin-bottom: var(--spacing-md);
}

.tab {
    padding: var(--spacing-sm) var(--spacing-md);
    color: var(--text-secondary);
    border-bottom: 2px solid transparent;
    transition: all var(--transition-fast);
    cursor: pointer;
}

.tab:hover {
    color: var(--text-primary);
}

.tab.active {
    color: var(--color-primary);
    border-bottom-color: var(--color-primary);
}

/* Progress */
.progress {
    height: 8px;
    background: var(--bg-input);
    border-radius: var(--radius-full);
    overflow: hidden;
}

.progress-bar {
    height: 100%;
    background: var(--color-primary);
    border-radius: var(--radius-full);
    transition: width var(--transition-normal);
}

/* Tooltip */
.tooltip {
    position: relative;
}

.tooltip::after {
    content: attr(data-tooltip);
    position: absolute;
    bottom: 100%;
    left: 50%;
    transform: translateX(-50%);
    padding: var(--spacing-xs) var(--spacing-sm);
    background: var(--bg-darkest);
    color: var(--text-primary);
    font-size: var(--font-size-xs);
    border-radius: var(--radius-sm);
    white-space: nowrap;
    opacity: 0;
    visibility: hidden;
    transition: all var(--transition-fast);
    z-index: var(--z-tooltip);
}

.tooltip:hover::after {
    opacity: 1;
    visibility: visible;
}
)";
    }
    
    /**
     * @brief Generate trading-specific components
     */
    static std::string generate_trading_components() {
        return R"(
/* Trading Components */

/* Price display */
.price {
    font-family: var(--font-mono);
    font-weight: 600;
}

.price-up {
    color: var(--color-success);
}

.price-down {
    color: var(--color-danger);
}

.price-change {
    display: inline-flex;
    align-items: center;
    gap: 4px;
}

.price-change::before {
    content: '';
    width: 0;
    height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
}

.price-change.up::before {
    border-bottom: 6px solid var(--color-success);
}

.price-change.down::before {
    border-top: 6px solid var(--color-danger);
}

/* Ticker tape */
.ticker-tape {
    display: flex;
    overflow: hidden;
    background: var(--bg-darker);
    padding: var(--spacing-xs) 0;
}

.ticker-tape-content {
    display: flex;
    animation: ticker 30s linear infinite;
}

.ticker-item {
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
    padding: 0 var(--spacing-lg);
    white-space: nowrap;
}

@keyframes ticker {
    0% { transform: translateX(0); }
    100% { transform: translateX(-50%); }
}

/* Order book */
.order-book {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: var(--spacing-md);
}

@media (max-width: 768px) {
    .order-book {
        grid-template-columns: 1fr;
    }
}

.order-book-side {
    display: flex;
    flex-direction: column;
}

.order-book-header {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    padding: var(--spacing-xs) var(--spacing-sm);
    font-size: var(--font-size-xs);
    color: var(--text-muted);
    text-transform: uppercase;
}

.order-book-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    padding: var(--spacing-xs) var(--spacing-sm);
    font-family: var(--font-mono);
    font-size: var(--font-size-sm);
    position: relative;
}

.order-book-row::before {
    content: '';
    position: absolute;
    top: 0;
    bottom: 0;
    right: 0;
    background: var(--row-color, transparent);
    opacity: 0.2;
}

.order-book-bids .order-book-row::before {
    --row-color: var(--color-success);
}

.order-book-asks .order-book-row::before {
    --row-color: var(--color-danger);
}

/* Trade ticket */
.trade-ticket {
    background: var(--bg-card);
    border-radius: var(--radius-lg);
    padding: var(--spacing-lg);
}

.trade-ticket-tabs {
    display: flex;
    margin-bottom: var(--spacing-md);
}

.trade-ticket-tab {
    flex: 1;
    padding: var(--spacing-sm);
    text-align: center;
    cursor: pointer;
    border-radius: var(--radius-md);
    transition: all var(--transition-fast);
}

.trade-ticket-tab.buy {
    background: rgba(0, 184, 148, 0.1);
    color: var(--color-success);
}

.trade-ticket-tab.buy.active {
    background: var(--color-success);
    color: white;
}

.trade-ticket-tab.sell {
    background: rgba(214, 48, 49, 0.1);
    color: var(--color-danger);
}

.trade-ticket-tab.sell.active {
    background: var(--color-danger);
    color: white;
}

/* Position card */
.position-card {
    display: grid;
    grid-template-columns: auto 1fr auto;
    gap: var(--spacing-md);
    padding: var(--spacing-md);
    background: var(--bg-card);
    border-radius: var(--radius-md);
    align-items: center;
}

@media (max-width: 576px) {
    .position-card {
        grid-template-columns: 1fr;
        text-align: center;
    }
}

.position-symbol {
    font-weight: 600;
    font-size: var(--font-size-lg);
}

.position-details {
    display: flex;
    flex-wrap: wrap;
    gap: var(--spacing-md);
}

.position-stat {
    display: flex;
    flex-direction: column;
}

.position-stat-label {
    font-size: var(--font-size-xs);
    color: var(--text-muted);
}

.position-stat-value {
    font-family: var(--font-mono);
    font-weight: 500;
}

/* Watchlist */
.watchlist-item {
    display: grid;
    grid-template-columns: auto 1fr auto auto;
    gap: var(--spacing-sm);
    padding: var(--spacing-sm) var(--spacing-md);
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    cursor: pointer;
    transition: background var(--transition-fast);
}

.watchlist-item:hover {
    background: rgba(255, 255, 255, 0.02);
}

@media (max-width: 576px) {
    .watchlist-item {
        grid-template-columns: 1fr auto;
    }
    
    .watchlist-item .d-md-block {
        display: none;
    }
}

/* Mini chart */
.mini-chart {
    width: 60px;
    height: 30px;
}

.mini-chart svg {
    width: 100%;
    height: 100%;
}

.mini-chart-up { stroke: var(--color-success); }
.mini-chart-down { stroke: var(--color-danger); }

/* Market status */
.market-status {
    display: inline-flex;
    align-items: center;
    gap: var(--spacing-xs);
    font-size: var(--font-size-sm);
}

.market-status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    animation: pulse 2s infinite;
}

.market-status-dot.open {
    background: var(--color-success);
}

.market-status-dot.closed {
    background: var(--color-danger);
    animation: none;
}

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}
)";
    }
    
    /**
     * @brief Generate dark theme overrides
     */
    static std::string generate_dark_theme() {
        return R"(
/* Dark theme (default) */
[data-theme="dark"] {
    --bg-dark: #1a1a2e;
    --bg-darker: #16213e;
    --bg-darkest: #0f0f1a;
    --bg-card: #16213e;
    --bg-input: #0f3460;
    --text-primary: #ffffff;
    --text-secondary: #a0a0b0;
    --text-muted: #6c6c7c;
}

/* Light theme */
[data-theme="light"] {
    --bg-dark: #f8f9fa;
    --bg-darker: #e9ecef;
    --bg-darkest: #dee2e6;
    --bg-card: #ffffff;
    --bg-input: #f1f3f5;
    --text-primary: #212529;
    --text-secondary: #495057;
    --text-muted: #868e96;
}

/* Responsive navigation */
.nav {
    display: flex;
    align-items: center;
    padding: var(--spacing-md) var(--spacing-lg);
    background: var(--bg-darker);
}

.nav-brand {
    font-size: var(--font-size-xl);
    font-weight: 700;
    color: var(--color-primary);
}

.nav-menu {
    display: flex;
    gap: var(--spacing-md);
    margin-left: auto;
}

.nav-link {
    color: var(--text-secondary);
    padding: var(--spacing-sm) var(--spacing-md);
    border-radius: var(--radius-md);
    transition: all var(--transition-fast);
}

.nav-link:hover,
.nav-link.active {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.05);
    text-decoration: none;
}

.nav-toggle {
    display: none;
    padding: var(--spacing-sm);
}

@media (max-width: 768px) {
    .nav-toggle {
        display: block;
    }
    
    .nav-menu {
        position: fixed;
        top: 60px;
        left: 0;
        right: 0;
        bottom: 0;
        flex-direction: column;
        background: var(--bg-darker);
        padding: var(--spacing-md);
        transform: translateX(-100%);
        transition: transform var(--transition-normal);
        z-index: var(--z-fixed);
    }
    
    .nav-menu.open {
        transform: translateX(0);
    }
}

/* Mobile bottom navigation */
.bottom-nav {
    display: none;
    position: fixed;
    bottom: 0;
    left: 0;
    right: 0;
    background: var(--bg-darker);
    padding: var(--spacing-sm);
    border-top: 1px solid rgba(255, 255, 255, 0.1);
    z-index: var(--z-fixed);
}

@media (max-width: 768px) {
    .bottom-nav {
        display: flex;
        justify-content: space-around;
    }
    
    body {
        padding-bottom: 70px;
    }
}

.bottom-nav-item {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    color: var(--text-muted);
    font-size: var(--font-size-xs);
    padding: var(--spacing-xs);
}

.bottom-nav-item.active {
    color: var(--color-primary);
}

.bottom-nav-item svg {
    width: 24px;
    height: 24px;
}

/* Safe area insets for mobile */
@supports (padding: env(safe-area-inset-bottom)) {
    .bottom-nav {
        padding-bottom: calc(var(--spacing-sm) + env(safe-area-inset-bottom));
    }
    
    body {
        padding-bottom: calc(70px + env(safe-area-inset-bottom));
    }
}

/* Touch-friendly sizing on mobile */
@media (max-width: 768px) {
    .btn {
        min-height: 44px;
        min-width: 44px;
    }
    
    .form-control {
        min-height: 44px;
    }
    
    .nav-link,
    .tab,
    .watchlist-item {
        min-height: 44px;
    }
}

/* Print styles */
@media print {
    .no-print,
    .nav,
    .bottom-nav {
        display: none !important;
    }
    
    body {
        background: white;
        color: black;
    }
    
    .card {
        box-shadow: none;
        border: 1px solid #ddd;
    }
}
)";
    }
};

/**
 * @brief Responsive layout configuration
 */
struct ResponsiveConfig {
    bool enable_dark_mode{true};
    bool enable_bottom_nav{true};
    bool enable_touch_optimizations{true};
    int sidebar_breakpoint{992};
    std::string default_theme{"dark"};
};

} // namespace ux
} // namespace genie

#endif // GENIE_UX_RESPONSIVE_LAYOUT_HPP
