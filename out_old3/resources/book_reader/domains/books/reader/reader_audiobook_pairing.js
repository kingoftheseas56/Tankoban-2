// FEAT-AUDIOBOOK: Reader sidebar "Audio" tab — chapter pairing UI
// Allows manual pairing of book chapters (from TOC) to audiobook chapter files.
// Paired audiobooks load only when the user explicitly triggers playback.
(function () {
  'use strict';

  if (window.__booksReaderAudiobookPairingBound) return;
  window.__booksReaderAudiobookPairingBound = true;

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var api = window.Tanko && window.Tanko.api;

  // ── State ──────────────────────────────────────────────────────────────────
  var _audiobooks = [];       // all scanned audiobooks
  var _selectedAbId = '';     // currently selected audiobook id in the dropdown
  var _selectedAb = null;     // the audiobook record
  var _mappings = [];         // current chapter mappings
  var _bookId = '';           // current book id
  var _savedPairing = null;   // pairing loaded from backend

  // ── DOM refs ───────────────────────────────────────────────────────────────
  var el = {};
  function qs(id) { return document.getElementById(id); }

  function ensureEls() {
    el.status     = qs('abPairStatus');
    el.select     = qs('abPairSelect');
    el.autoBtn    = qs('abPairAutoBtn');
    el.saveBtn    = qs('abPairSaveBtn');
    el.unlinkBtn  = qs('abPairUnlinkBtn');
    el.list       = qs('abPairList');
  }

  // ── Helpers ─────────────────────────────────────────────────────────────────
  function getBookId() {
    var state = RS.state;
    if (!state || !state.book) return '';
    return state.book.id || state.book.path || '';
  }

  function getBookToc() {
    var state = RS.state;
    return (state && Array.isArray(state.tocItems)) ? state.tocItems : [];
  }

  function findAudiobook(id) {
    for (var i = 0; i < _audiobooks.length; i++) {
      if (_audiobooks[i].id === id) return _audiobooks[i];
    }
    return null;
  }


  function normalizeHref(href) {
    var h = String(href || '');
    h = h.replace(/^\.\//, '');
    var hashIdx = h.indexOf('#');
    if (hashIdx >= 0) h = h.substring(0, hashIdx);
    try { h = decodeURIComponent(h); } catch (_) {}
    return h.toLowerCase().trim();
  }

  function buildMappingIndex() {
    var out = Object.create(null);
    for (var i = 0; i < _mappings.length; i++) {
      var m = _mappings[i] || {};
      var key = normalizeHref(m.bookChapterHref || '');
      if (!key) continue;
      var idx = parseInt(m.abChapterIndex, 10);
      if (!isFinite(idx) || idx < 0) continue;
      out[key] = idx;
    }
    return out;
  }

  function getCurrentReaderChapterHref() {
    try {
      var eng = RS && RS.state && RS.state.engine;
      var detail = eng && eng.lastRelocate ? eng.lastRelocate : null;
      var href = detail && detail.tocItem && detail.tocItem.href ? detail.tocItem.href : '';
      return normalizeHref(href);
    } catch (_) { return ''; }
  }

  function getMappedAudiobookChapterIndexForCurrentReaderChapter() {
    var href = getCurrentReaderChapterHref();
    if (!href) return null;
    var mappingIdx = buildMappingIndex();
    return Object.prototype.hasOwnProperty.call(mappingIdx, href) ? mappingIdx[href] : null;
  }

  function syncAudiobookToCurrentReaderChapter(opts) {
    opts = (opts && typeof opts === 'object') ? opts : {};
    if (!_savedPairing || !_selectedAb || !_selectedAbId) return;
    var abPlayer = window.booksReaderAudiobook;
    if (!abPlayer) return;

    var mappedIndex = getMappedAudiobookChapterIndexForCurrentReaderChapter();
    if (mappedIndex == null) return;

    var currentAb = (typeof abPlayer.getAudiobook === 'function') ? abPlayer.getAudiobook() : null;
    var sameAudiobookLoaded = !!(currentAb && currentAb.id === _selectedAbId && abPlayer.isLoaded && abPlayer.isLoaded());

    if (!sameAudiobookLoaded) {
      return;
    }

    var currentChapter = (typeof abPlayer.getCurrentChapterIndex === 'function')
      ? abPlayer.getCurrentChapterIndex()
      : (((abPlayer.getProgress && abPlayer.getProgress()) || {}).chapterIndex);

    if (currentChapter === mappedIndex && !opts.force) return;

    if (typeof abPlayer.setChapterIndex === 'function') {
      abPlayer.setChapterIndex(mappedIndex, { preservePlayState: true });
    } else if (typeof abPlayer.loadAudiobook === 'function') {
      abPlayer.loadAudiobook(_selectedAb, { chapterIndex: mappedIndex, position: 0 });
    }
  }

  // ── Load audiobooks list ───────────────────────────────────────────────────
  function loadAudiobooksList() {
    if (!api || !api.audiobooks) return Promise.resolve();
    return api.audiobooks.getState().then(function (snap) {
      _audiobooks = (snap && Array.isArray(snap.audiobooks)) ? snap.audiobooks : [];
      populateSelect();
    }).catch(function () { _audiobooks = []; });
  }

  function populateSelect() {
    if (!el.select) return;
    // Keep the first default option, clear the rest
    while (el.select.options.length > 1) el.select.remove(1);
    for (var i = 0; i < _audiobooks.length; i++) {
      var ab = _audiobooks[i];
      var opt = document.createElement('option');
      opt.value = ab.id;
      opt.textContent = ab.title + ' (' + ab.chapters.length + ' ch)';
      el.select.appendChild(opt);
    }
    // Restore selection if we have a saved pairing
    if (_selectedAbId) el.select.value = _selectedAbId;
  }

  // ── Load saved pairing ─────────────────────────────────────────────────────
  function loadSavedPairing() {
    _bookId = getBookId();
    if (!_bookId || !api || !api.audiobooks) return Promise.resolve();
    return api.audiobooks.getPairing(_bookId).then(function (pairing) {
      _savedPairing = pairing || null;
      if (_savedPairing && _savedPairing.audiobookId) {
        _selectedAbId = _savedPairing.audiobookId;
        _selectedAb = findAudiobook(_selectedAbId);
        _mappings = (_savedPairing.mappings && Array.isArray(_savedPairing.mappings)) ? _savedPairing.mappings : [];
        if (el.select) el.select.value = _selectedAbId;
        updateStatus('Linked: ' + (_selectedAb ? _selectedAb.title : _selectedAbId));
      } else {
        _selectedAbId = '';
        _selectedAb = null;
        _mappings = [];
        updateStatus('No audiobook linked');
      }
      renderMappings();
    }).catch(function () {
      _savedPairing = null;
      updateStatus('No audiobook linked');
    });
  }

  // ── Auto-load paired audiobook into the reader player ──────────────────────
  function autoLoadPairedAudiobook(options) {
    if (!_savedPairing || !_selectedAb) return;
    var abPlayer = window.booksReaderAudiobook;
    if (!abPlayer || !abPlayer.loadAudiobook) return;
    var optsIn = (options && typeof options === 'object') ? options : {};
    var forcedChapterIndex = (typeof optsIn.forceChapterIndex === 'number' && isFinite(optsIn.forceChapterIndex))
      ? Math.max(0, Math.floor(optsIn.forceChapterIndex))
      : null;

    // Load with resume from saved audiobook progress, but let book↔audio pairing win on chapter selection.
    if (api && api.audiobooks) {
      api.audiobooks.getProgress(_selectedAb.id).then(function (prog) {
        var resume = null;
        var progressChapter = (prog && prog.chapterIndex != null) ? parseInt(prog.chapterIndex, 10) : null;
        var progressPos = (prog && prog.position != null) ? (prog.position || 0) : 0;
        var startChapter = (forcedChapterIndex != null) ? forcedChapterIndex : progressChapter;
        if (startChapter != null && isFinite(startChapter)) {
          resume = { chapterIndex: startChapter, position: (progressChapter === startChapter ? progressPos : 0) };
        }
        abPlayer.loadAudiobook(_selectedAb, resume);
      }).catch(function () {
        var fallback = (forcedChapterIndex != null) ? { chapterIndex: forcedChapterIndex, position: 0 } : null;
        abPlayer.loadAudiobook(_selectedAb, fallback);
      });
    } else {
      var fallback2 = (forcedChapterIndex != null) ? { chapterIndex: forcedChapterIndex, position: 0 } : null;
      abPlayer.loadAudiobook(_selectedAb, fallback2);
    }
  }

  // ── Render chapter mapping list ────────────────────────────────────────────
  function renderMappings() {
    if (!el.list) return;
    el.list.innerHTML = '';
    var toc = getBookToc();
    if (!toc.length) {
      el.list.innerHTML = '<div class="ab-pair-empty muted tiny">No book chapters found (TOC not loaded yet)</div>';
      return;
    }
    if (!_selectedAb) {
      el.list.innerHTML = '<div class="ab-pair-empty muted tiny">Select an audiobook to pair chapters</div>';
      return;
    }

    var abChapters = _selectedAb.chapters || [];

    // Build a mapping lookup by book chapter href
    var mappingByHref = {};
    for (var m = 0; m < _mappings.length; m++) {
      if (_mappings[m].bookChapterHref) {
        mappingByHref[_mappings[m].bookChapterHref] = _mappings[m].abChapterIndex;
      }
    }

    for (var i = 0; i < toc.length; i++) {
      var ch = toc[i];
      var row = document.createElement('div');
      row.className = 'ab-pair-row';

      var bookLabel = document.createElement('span');
      bookLabel.className = 'ab-pair-book-ch';
      bookLabel.textContent = ch.label || ch.title || ('Chapter ' + (i + 1));
      bookLabel.title = bookLabel.textContent;
      row.appendChild(bookLabel);

      var arrow = document.createElement('span');
      arrow.className = 'ab-pair-arrow';
      arrow.textContent = '\u2192';
      row.appendChild(arrow);

      var sel = document.createElement('select');
      sel.className = 'ab-pair-ch-select';
      sel.dataset.tocIndex = i;
      sel.dataset.href = ch.href || '';

      var noneOpt = document.createElement('option');
      noneOpt.value = '-1';
      noneOpt.textContent = '-- none --';
      sel.appendChild(noneOpt);

      for (var j = 0; j < abChapters.length; j++) {
        var abOpt = document.createElement('option');
        abOpt.value = j;
        abOpt.textContent = (j + 1) + '. ' + abChapters[j].title;
        sel.appendChild(abOpt);
      }

      // Restore saved mapping
      var href = ch.href || '';
      if (href in mappingByHref) {
        sel.value = mappingByHref[href];
      }

      sel.addEventListener('change', onMappingChanged);
      row.appendChild(sel);
      el.list.appendChild(row);
    }
  }

  function onMappingChanged() {
    // Rebuild _mappings from all selects
    rebuildMappingsFromUI();
  }

  function rebuildMappingsFromUI() {
    if (!el.list) return;
    var toc = getBookToc();
    var selects = el.list.querySelectorAll('.ab-pair-ch-select');
    _mappings = [];
    for (var i = 0; i < selects.length; i++) {
      var sel = selects[i];
      var abIdx = parseInt(sel.value, 10);
      if (abIdx < 0) continue;
      var tocIdx = parseInt(sel.dataset.tocIndex, 10);
      var href = sel.dataset.href || '';
      var bookLabel = (toc[tocIdx] && (toc[tocIdx].label || toc[tocIdx].title)) || '';
      var abTitle = (_selectedAb && _selectedAb.chapters[abIdx]) ? _selectedAb.chapters[abIdx].title : '';
      _mappings.push({
        bookChapterHref: href,
        bookChapterLabel: bookLabel,
        abChapterIndex: abIdx,
        abChapterTitle: abTitle
      });
    }
  }

  // ── Auto-pair (by index) ───────────────────────────────────────────────────
  function autoPair() {
    if (!_selectedAb) return;
    var toc = getBookToc();
    var abChapters = _selectedAb.chapters || [];
    _mappings = [];
    var count = Math.min(toc.length, abChapters.length);
    for (var i = 0; i < count; i++) {
      _mappings.push({
        bookChapterHref: toc[i].href || '',
        bookChapterLabel: toc[i].label || toc[i].title || '',
        abChapterIndex: i,
        abChapterTitle: abChapters[i].title || ''
      });
    }
    renderMappings();
  }

  // ── Save pairing ──────────────────────────────────────────────────────────
  function savePairing() {
    if (!_bookId || !_selectedAbId || !api || !api.audiobooks) return;
    rebuildMappingsFromUI();
    var pairing = {
      bookId: _bookId,
      audiobookId: _selectedAbId,
      mappings: _mappings,
      updatedAt: Date.now()
    };
    api.audiobooks.savePairing(_bookId, pairing).then(function () {
      _savedPairing = pairing;
      updateStatus('Saved: ' + (_selectedAb ? _selectedAb.title : ''));
      // Keep current playback aligned with mapping only if user already loaded audiobook.
      syncAudiobookToCurrentReaderChapter({ force: true });
    }).catch(function (err) {
      console.error('[AB Pairing] save error:', err);
    });
  }

  // ── Unlink pairing ────────────────────────────────────────────────────────
  function unlinkPairing() {
    if (!_bookId || !api || !api.audiobooks) return;
    api.audiobooks.deletePairing(_bookId).then(function () {
      _savedPairing = null;
      _selectedAbId = '';
      _selectedAb = null;
      _mappings = [];
      if (el.select) el.select.value = '';
      updateStatus('No audiobook linked');
      renderMappings();
      // Close the audiobook player if one is loaded
      var abPlayer = window.booksReaderAudiobook;
      if (abPlayer && abPlayer.isLoaded && abPlayer.isLoaded()) {
        try { abPlayer.closeAudiobook(); } catch (_) {}
      }
    }).catch(function (err) {
      console.error('[AB Pairing] unlink error:', err);
    });
  }

  // ── UI helpers ─────────────────────────────────────────────────────────────
  function updateStatus(text) {
    if (el.status) el.status.textContent = text;
  }

  // ── Module lifecycle ───────────────────────────────────────────────────────
  function bind() {
    ensureEls();
    if (!el.select) return;

    el.select.addEventListener('change', function () {
      _selectedAbId = el.select.value;
      _selectedAb = findAudiobook(_selectedAbId);
      if (_selectedAb) {
        updateStatus('Selected: ' + _selectedAb.title);
      } else {
        updateStatus('No audiobook linked');
      }
      _mappings = [];
      renderMappings();
    });

    if (el.autoBtn) el.autoBtn.addEventListener('click', autoPair);
    if (el.saveBtn) el.saveBtn.addEventListener('click', savePairing);
    if (el.unlinkBtn) el.unlinkBtn.addEventListener('click', unlinkPairing);

    // Re-render mappings when TOC loads/changes
    if (bus) {
      bus.on('toc:updated', function () {
        renderMappings();
        // If the paired audiobook opened before TOC/relocate settled, sync now.
        syncAudiobookToCurrentReaderChapter();
      });
      bus.on('reader:relocated', function () {
        syncAudiobookToCurrentReaderChapter();
      });
    }
  }

  function onOpen() {
    ensureEls();
    _bookId = getBookId();
    _selectedAbId = '';
    _selectedAb = null;
    _mappings = [];
    _savedPairing = null;
    updateStatus('Loading...');

    // Load audiobook list, then restore saved pairing metadata only.
    loadAudiobooksList().then(function () {
      return loadSavedPairing();
    }).then(function () {
      // Keep chapter sync behavior for already-loaded player sessions only.
      syncAudiobookToCurrentReaderChapter();
    }).catch(function () {
      updateStatus('No audiobook linked');
    });
  }

  function onClose() {
    _audiobooks = [];
    _selectedAbId = '';
    _selectedAb = null;
    _mappings = [];
    _savedPairing = null;
    _bookId = '';
    if (el.list) el.list.innerHTML = '';
    if (el.select) {
      while (el.select.options.length > 1) el.select.remove(1);
      el.select.value = '';
    }
    updateStatus('No audiobook linked');
  }

  // ── Export ──────────────────────────────────────────────────────────────────
  window.booksReaderAudiobookPairing = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    hasSavedPairing: function () { return !!_savedPairing; },
    triggerAutoLoad: function () {
      var mappedIndex = getMappedAudiobookChapterIndexForCurrentReaderChapter();
      if (mappedIndex != null) autoLoadPairedAudiobook({ forceChapterIndex: mappedIndex });
      else autoLoadPairedAudiobook();
    },
  };

})();
