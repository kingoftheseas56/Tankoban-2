// BUILD_OVERHAUL: Keyboard handler - all keydown dispatching to bus events
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  var shortcutCaptureAction = '';

  // LISTEN_P0: ttsToggle, voiceNext, voicePrev removed — owned by Listening mode
  var SHORTCUT_ACTIONS = [
    { id: 'tocToggle', label: 'Toggle TOC' },
    { id: 'bookmarkToggle', label: 'Toggle bookmark' },
    { id: 'dictLookup', label: 'Dictionary lookup' },
    { id: 'fullscreen', label: 'Toggle fullscreen' },
    { id: 'sidebarToggle', label: 'Toggle sidebar' },
    { id: 'themeToggle', label: 'Cycle theme' },
  ];

  var FIXED_SHORTCUTS = [
    { label: 'Go to page', key: 'Ctrl+G' },
    { label: 'Find in book', key: 'Ctrl+F or /' },
    { label: 'Page nav', key: 'Arrows, Space, Shift+Space, PgUp/PgDn' },
    { label: 'Next/prev chapter', key: 'Ctrl+Right / Ctrl+Left' },
    { label: 'Nav history back/fwd', key: 'Alt+Left / Alt+Right' }, // BUILD_HISTORY
    { label: 'Book bounds', key: 'Home / End' },
    { label: 'Escape chain', key: 'Esc (cascading close)' },
  ];

  function isTypingElement(node) {
    if (!node || !node.tagName) return false;
    var tag = String(node.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') return true;
    return !!node.isContentEditable;
  }

  function normalizeShortcutInput(e) {
    if (e.ctrlKey || e.metaKey || e.altKey) return '';
    var key = String(e.key || '');
    if (key.length !== 1) return '';
    if (/\s/.test(key)) return '';
    if (e.shiftKey) return key;
    return key.toLowerCase();
  }

  function formatShortcutKey(k) {
    var key = String(k || '').trim();
    if (!key) return '(default)';
    if (key.length === 1) {
      var upper = key.toUpperCase();
      if (key !== key.toLowerCase() && key === upper) {
        return 'Shift+' + upper;
      }
      return upper;
    }
    return key;
  }

  function renderShortcutEditor() {
    var els = RS.ensureEls();
    var state = RS.state;
    if (!els.shortcutsList) return;

    var html = '';
    for (var i = 0; i < SHORTCUT_ACTIONS.length; i++) {
      var row = SHORTCUT_ACTIONS[i];
      var val = state.shortcuts[row.id] || RS.DEFAULT_SHORTCUTS[row.id] || '';
      var isCapture = shortcutCaptureAction === row.id;
      html += '<div class="br-shortcut-row">'
        + '<span>' + RS.escHtml(row.label) + '</span>'
        + '<button type="button" class="br-shortcut-key br-shortcut-key-edit" data-shortcut-action="' + RS.escHtml(row.id) + '">'
        + (isCapture ? 'Press key...' : RS.escHtml(formatShortcutKey(val)))
        + '</button>'
        + '</div>';
    }

    for (var j = 0; j < FIXED_SHORTCUTS.length; j++) {
      html += '<div class="br-shortcut-row br-shortcut-row-fixed">'
        + '<span>' + RS.escHtml(FIXED_SHORTCUTS[j].label) + '</span>'
        + '<span class="br-shortcut-key">' + RS.escHtml(FIXED_SHORTCUTS[j].key) + '</span>'
        + '</div>';
    }

    els.shortcutsList.innerHTML = html;

    var buttons = els.shortcutsList.querySelectorAll('[data-shortcut-action]');
    for (var b = 0; b < buttons.length; b++) {
      (function (btn) {
        btn.addEventListener('click', function () {
          var action = String(btn.getAttribute('data-shortcut-action') || '');
          shortcutCaptureAction = action;
          renderShortcutEditor();
        });
      })(buttons[b]);
    }
  }

  function handleShortcutCapture(e) {
    var state = RS.state;
    if (!shortcutCaptureAction) return false;
    e.preventDefault();
    e.stopPropagation();

    if (e.key === 'Escape') {
      shortcutCaptureAction = '';
      renderShortcutEditor();
      return true;
    }

    if (e.key === 'Backspace' || e.key === 'Delete') {
      delete state.shortcuts[shortcutCaptureAction];
      shortcutCaptureAction = '';
      RS.saveShortcuts();
      renderShortcutEditor();
      RS.showToast('Shortcut reset');
      return true;
    }

    var key = normalizeShortcutInput(e);
    if (!key) return true;

    state.shortcuts[shortcutCaptureAction] = key;
    shortcutCaptureAction = '';
    RS.saveShortcuts();
    renderShortcutEditor();
    RS.showToast('Shortcut saved: ' + formatShortcutKey(key));
    return true;
  }

  // -- Bind ---------------------------------------------------------------------------

  function bind() {
    renderShortcutEditor();

    document.addEventListener('keydown', function (e) {
      var state = RS.state;
      if (!state.open) return;

      // LISTEN_P3: when the detached/in-reader listening overlay is open,
      // let its own keyboard handler own Escape/space/arrows to avoid double-dispatch.
      try {
        var _lp = document.getElementById('booksListenPlayerOverlay');
        if (_lp && !_lp.classList.contains('hidden')) return;
      } catch (err) {}

      if (handleShortcutCapture(e)) return;

      // Skip most global shortcuts while typing in inputs/selects/contenteditable.
      // Keep Escape handling later for blur/close behavior.
      var activeEl = document.activeElement;
      var typingNow = !!(activeEl && isTypingElement(activeEl));

      // Ctrl+G: goto dialog
      if (!typingNow && (e.ctrlKey || e.metaKey) && (e.key === 'g' || e.key === 'G')) {
        e.preventDefault();
        var Nav = window.booksReaderNav;
        if (Nav && Nav.isGotoOpen && Nav.isGotoOpen()) {
          bus.emit('nav:goto-close');
        } else {
          bus.emit('nav:goto-open');
        }
        return;
      }

      // Ctrl+F or /: open search overlay
      if (!typingNow && (e.ctrlKey || e.metaKey) && (e.key === 'f' || e.key === 'F')) {
        e.preventDefault();
        bus.emit('overlay:toggle', 'search');
        return;
      }

      // FIX_CHAP_NAV: Ctrl+Arrow: next/prev chapter
      if (!typingNow && (e.ctrlKey || e.metaKey) && e.key === 'ArrowRight') {
        e.preventDefault();
        bus.emit('nav:next-chapter');
        return;
      }
      if (!typingNow && (e.ctrlKey || e.metaKey) && e.key === 'ArrowLeft') {
        e.preventDefault();
        bus.emit('nav:prev-chapter');
        return;
      }

      // Skip navigation shortcuts when typing in inputs
      if (typingNow) {
        if (e.key === 'Escape') {
          e.preventDefault();
          activeEl.blur();
        }
        return;
      }

      if (!e.ctrlKey && !e.metaKey && !e.altKey && e.key === '/') {
        e.preventDefault();
        bus.emit('overlay:toggle', 'search');
        return;
      }

      var els = RS.ensureEls();

      // Escape: priority chain
      if (e.key === 'Escape') {
        e.preventDefault();
        var Nav2 = window.booksReaderNav;
        if (Nav2 && Nav2.isGotoOpen && Nav2.isGotoOpen()) { bus.emit('nav:goto-close'); return; }
        var Annot = window.booksReaderAnnotations;
        if (Annot && Annot.isAnnotPopupOpen && Annot.isAnnotPopupOpen()) { bus.emit('annot:hide-popup'); return; }
        // Dictionary popup
        if (els.dictPopup && !els.dictPopup.classList.contains('hidden')) { bus.emit('dict:hide'); return; }
        // Floating overlays
        var OV = window.booksReaderOverlays;
        if (OV && OV.isOpen()) { OV.closeAll(); return; }
        // Chapter transition card
        var Nav3 = window.booksReaderNav;
        if (Nav3 && Nav3.isChapterTransitionOpen && Nav3.isChapterTransitionOpen()) {
          Nav3.dismissChapterTransition(false);
          return;
        }
        // Sidebar
        if (state.sidebarOpen) { bus.emit('sidebar:close'); return; }
        // Close reader (prefer host-level close for stable shell transition)
        try {
          if (window.__ebookNav && typeof window.__ebookNav.requestClose === 'function') {
            window.__ebookNav.requestClose();
            return;
          }
        } catch (err) {}
        // Fallback: local reader close
        bus.emit('reader:close');
        return;
      }

      // FIX_HUD: Backspace closes sidebar when open
      if (e.key === 'Backspace' && state.sidebarOpen) {
        e.preventDefault();
        bus.emit('sidebar:close');
        return;
      }

      // BUILD_HISTORY: Alt+Left/Right for navigation history (before bare arrow handling)
      if (e.altKey && e.key === 'ArrowLeft') {
        e.preventDefault();
        try {
          if (state.engine && typeof state.engine.historyBack === 'function') {
            var canBack = (typeof state.engine.historyCanGoBack === 'function') ? !!state.engine.historyCanGoBack() : true;
            if (canBack) state.engine.historyBack();
          }
        } catch (err) {}
        return;
      }
      if (e.altKey && e.key === 'ArrowRight') {
        e.preventDefault();
        try {
          if (state.engine && typeof state.engine.historyForward === 'function') {
            var canFwd = (typeof state.engine.historyCanGoForward === 'function') ? !!state.engine.historyCanGoForward() : true;
            if (canFwd) state.engine.historyForward();
          }
        } catch (err) {}
        return;
      }

      // Page navigation
      if (e.key === ' ' && e.shiftKey) {
        e.preventDefault();
        bus.emit('nav:prev');
        return;
      }
      if (e.key === 'ArrowRight' || e.key === 'PageDown') {
        e.preventDefault();
        bus.emit('nav:next');
        return;
      }
      if (e.key === 'ArrowLeft' || e.key === 'PageUp') {
        e.preventDefault();
        bus.emit('nav:prev');
        return;
      }
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        bus.emit('nav:next');
        return;
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        bus.emit('nav:prev');
        return;
      }
      if (e.key === 'Home') {
        e.preventDefault();
        // FIX_AUDIT: Home/End must seek whole-book bounds, not chapter-local bounds.
        bus.emit('nav:seek-book', 0);
        return;
      }
      if (e.key === 'End') {
        e.preventDefault();
        // FIX_AUDIT: Home/End must seek whole-book bounds, not chapter-local bounds.
        bus.emit('nav:seek-book', 1);
        return;
      }

      // Customizable shortcuts
      if (RS.matchShortcut(e, 'dictLookup')) {
        e.preventDefault();
        bus.emit('dict:lookup');
        return;
      }
      if (RS.matchShortcut(e, 'bookmarkToggle')) {
        e.preventDefault();
        bus.emit('bookmark:toggle');
        return;
      }
      // Listening mode: T key toggles TTS
      if (e.key === 't' && !e.ctrlKey && !e.altKey && !e.metaKey && !e.shiftKey) {
        e.preventDefault();
        var listenBtn = document.getElementById('booksReaderListenToggle') || document.getElementById('booksReaderListenBtn');
        if (listenBtn) listenBtn.click();
        return;
      }
      if (RS.matchShortcut(e, 'sidebarToggle')) {
        e.preventDefault();
        bus.emit('sidebar:toggle');
        return;
      }
      if (RS.matchShortcut(e, 'fullscreen')) {
        e.preventDefault();
        bus.emit('reader:fullscreen');
        return;
      }
      if (RS.matchShortcut(e, 'tocToggle')) {
        e.preventDefault();
        bus.emit('sidebar:toggle');
        return;
      }
      if (RS.matchShortcut(e, 'themeToggle')) {
        e.preventDefault();
        bus.emit('appearance:cycle-theme');
        return;
      }

      // ? or K key: open keyboard shortcuts overlay
      if ((!e.ctrlKey && !e.metaKey && !e.altKey) && (e.key === '?' || (!e.shiftKey && (e.key === 'k' || e.key === 'K')))) {
        e.preventDefault();
        bus.emit('overlay:toggle', 'keys');
        return;
      }
    });

  }

  function onOpen() {
    shortcutCaptureAction = '';
    renderShortcutEditor();
  }

  function onClose() {
    shortcutCaptureAction = '';
  }

  // -- Export -----------------------------------------------------------------------

  window.booksReaderKeyboard = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    renderShortcutEditor: renderShortcutEditor,
  };
})();
