// FEAT-AUDIOBOOK: In-reader audiobook playback module
// Registered in reader_core.js module array.
// Provides transport bar inside the reading area + TTS mutual exclusion.
(function () {
  'use strict';

  if (window.__booksReaderAudiobookBound) return;
  window.__booksReaderAudiobookBound = true;

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var api = window.Tanko && window.Tanko.api;

  // ── State ──────────────────────────────────────────────────────────────────
  var _audiobook = null;
  var _chapterIndex = 0;
  var _playing = false;
  var _audio = null;
  var _playbackRate = 1.0;
  var _volume = 1.0;
  var _loaded = false;       // true once an audiobook is loaded in the reader session
  var _seekDragging = false;
  var _saveTimer = null;
  var _perChapterListenedMs = {};
  var _lastListenPositionMs = 0;

  // Auto-hide state
  var _barHideTimer = null;
  var _barHoverBar = false;
  var _barHoverBottom = false;
  var _barVisible = false;
  var BAR_AUTO_HIDE_MS = 3000;

  // ── DOM refs ───────────────────────────────────────────────────────────────
  var el = {};
  function qs(id) { return document.getElementById(id); }

  function ensureEls() {
    el.bar         = qs('abPlayerBar');
    el.chLabel     = qs('abChapterLabel');
    el.prevCh      = qs('abPrevCh');
    el.rew15       = qs('abRew15');
    el.playPause   = qs('abPlayPause');
    el.fwd15       = qs('abFwd15');
    el.nextCh      = qs('abNextCh');
    el.time        = qs('abTime');
    el.seek        = qs('abSeek');
    el.speed       = qs('abSpeed');
    el.volume      = qs('abVolume');
    el.close       = qs('abClose');
  }

  // ── SVG icons ──────────────────────────────────────────────────────────────
  var SVG_PLAY = '<svg viewBox="0 0 24 24" width="18" height="18"><path d="M8 5v14l11-7z" fill="currentColor"/></svg>';
  var SVG_PAUSE = '<svg viewBox="0 0 24 24" width="18" height="18"><rect x="6" y="4" width="4" height="16" rx="1" fill="currentColor"/><rect x="14" y="4" width="4" height="16" rx="1" fill="currentColor"/></svg>';

  // ── Helpers ─────────────────────────────────────────────────────────────────
  function fmt(secs) {
    if (!secs || !isFinite(secs)) return '0:00';
    var s = Math.round(secs);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    if (h > 0) return h + ':' + String(m).padStart(2, '0') + ':' + String(sec).padStart(2, '0');
    return m + ':' + String(sec).padStart(2, '0');
  }

  // ── TTS mutual exclusion ───────────────────────────────────────────────────
  function stopTTS() {
    var tts = window.booksTTS;
    if (!tts) return;
    try { if (typeof tts.stop === 'function') tts.stop(); } catch (_) {}
    try { if (typeof tts.destroy === 'function') tts.destroy(); } catch (_) {}
  }

  function isTTSActive() {
    try {
      var tts = window.booksTTS;
      if (!tts || typeof tts.getState !== 'function') return false;
      var s = String(tts.getState() || '');
      return s === 'playing' || s === 'paused' || /-paused$/.test(s);
    } catch (_) { return false; }
  }

  // ── Audio engine ───────────────────────────────────────────────────────────
  function ensureAudio() {
    if (_audio) return;
    _audio = new Audio();
    _audio.addEventListener('timeupdate', onTimeUpdate);
    _audio.addEventListener('ended', onChapterEnded);
    _audio.addEventListener('loadedmetadata', onMetaLoaded);
    _audio.addEventListener('error', onAudioError);
    _audio.addEventListener('play', function () {
      _lastListenPositionMs = Math.max(0, Math.round((_audio.currentTime || 0) * 1000));
      _playing = true;
      updatePlayBtn();
      showBar();
      startAutoHide();
    });
    _audio.addEventListener('pause', function () {
      trackListenedDelta();
      _playing = false;
      updatePlayBtn();
      showBar();
      clearAutoHide();
      if (_loaded) saveProgress();
    });
  }

  function loadChapter(index) {
    if (!_audiobook || index < 0 || index >= _audiobook.chapters.length) return;
    _chapterIndex = index;
    var ch = _audiobook.chapters[index];
    ensureAudio();
    _audio.src = 'file://' + ch.path.replace(/\\/g, '/').replace(/#/g, '%23');
    _audio.playbackRate = _playbackRate;
    _audio.volume = _volume;
    _lastListenPositionMs = 0;
    updateChapterLabel();
    updateTimeDisplay(0, 0);
    if (el.seek) { el.seek.value = '0'; el.seek.max = '100'; el.seek.disabled = true; }
    bus.emit('audiobook:chapter-changed', { index: index, title: ch.title, total: _audiobook.chapters.length });
  }

  function setChapterIndex(index, opts) {
    if (!_audiobook || !_audiobook.chapters || !_audiobook.chapters.length) return false;
    var nextIdx = parseInt(index, 10);
    if (!isFinite(nextIdx) || nextIdx < 0 || nextIdx >= _audiobook.chapters.length) return false;

    opts = (opts && typeof opts === 'object') ? opts : {};
    var sameChapter = (_chapterIndex === nextIdx);
    var wasPlaying = !!_playing;
    var shouldPlay = (typeof opts.autoplay === 'boolean') ? opts.autoplay : ((opts.preservePlayState !== false) ? wasPlaying : false);
    var seekPos = (typeof opts.position === 'number' && isFinite(opts.position) && opts.position >= 0) ? opts.position : null;

    if (!sameChapter) {
      loadChapter(nextIdx);
    }

    if (_audio) {
      var applySeek = function () {
        try {
          if (seekPos != null) _audio.currentTime = seekPos;
        } catch (_) {}
        if (shouldPlay) play();
        else pause();
      };

      if (seekPos != null) {
        if (isFinite(_audio.duration) && _audio.duration > 0) applySeek();
        else {
          var onMetaForSeek = function () {
            try { _audio.removeEventListener('loadedmetadata', onMetaForSeek); } catch (_) {}
            applySeek();
          };
          _audio.addEventListener('loadedmetadata', onMetaForSeek);
        }
      } else if (shouldPlay) {
        play();
      } else if (!sameChapter) {
        pause();
      }
    }

    return true;
  }

  function getCurrentChapterIndex() {
    return _chapterIndex;
  }

  function play() {
    if (!_audio || !_audio.src) return;
    // Mutual exclusion: stop TTS before playing audiobook
    if (isTTSActive()) stopTTS();
    _audio.play().catch(function (e) { console.warn('[AB Reader] play error:', e); });
  }

  function pause() {
    if (_audio) _audio.pause();
  }

  function togglePlayPause() {
    if (_playing) pause();
    else play();
  }

  function seekRelative(delta) {
    if (!_audio || !isFinite(_audio.duration)) return;
    var t = Math.max(0, Math.min(_audio.duration, _audio.currentTime + delta));
    _audio.currentTime = t;
    _lastListenPositionMs = Math.max(0, Math.round(t * 1000));
    saveProgress();
  }

  function nextChapter() {
    if (!_audiobook) return;
    if (_chapterIndex + 1 < _audiobook.chapters.length) {
      saveProgress();
      loadChapter(_chapterIndex + 1);
      play();
    }
  }

  function prevChapter() {
    if (!_audiobook) return;
    if (_audio && _audio.currentTime > 3) {
      _audio.currentTime = 0;
      return;
    }
    if (_chapterIndex > 0) {
      saveProgress();
      loadChapter(_chapterIndex - 1);
      play();
    }
  }

  function setRate(rate) {
    var nextRate = Number(rate);
    if (!isFinite(nextRate)) return;
    _playbackRate = Math.max(0.5, Math.min(3.0, nextRate));
    if (_audio) _audio.playbackRate = _playbackRate;
    if (el.speed) el.speed.value = String(_playbackRate);
    if (_loaded) saveProgress();
  }

  function setVolume(vol) {
    _volume = Math.max(0, Math.min(1, vol));
    if (_audio) _audio.volume = _volume;
    if (el.volume) el.volume.value = _volume;
    if (_loaded) saveProgress();
  }

  function trackListenedDelta() {
    if (!_audio || !_audiobook) return;
    var currentMs = Math.max(0, Math.round((_audio.currentTime || 0) * 1000));
    var deltaMs = currentMs - _lastListenPositionMs;
    _lastListenPositionMs = currentMs;
    if (!isFinite(deltaMs) || deltaMs <= 0 || deltaMs > 10000) return;
    var key = String(_chapterIndex);
    var prior = Number(_perChapterListenedMs[key] || 0);
    _perChapterListenedMs[key] = prior + deltaMs;
  }

  // ── Sleep timer ────────────────────────────────────────────────────────────
  // ── Audio event handlers ───────────────────────────────────────────────────
  function onTimeUpdate() {
    if (_seekDragging || !_audio) return;
    trackListenedDelta();
    var cur = _audio.currentTime || 0;
    var dur = _audio.duration || 0;
    updateTimeDisplay(cur, dur);
    bus.emit('audiobook:progress', {
      chapterIndex: _chapterIndex,
      position: cur,
      duration: dur,
      chapterTitle: _audiobook ? _audiobook.chapters[_chapterIndex].title : ''
    });
    if (_playing) scheduleSave();
  }

  function onMetaLoaded() {
    if (!_audio) return;
    _lastListenPositionMs = Math.max(0, Math.round((_audio.currentTime || 0) * 1000));
    updateTimeDisplay(_audio.currentTime || 0, _audio.duration || 0);
  }

  function onChapterEnded() {
    saveProgress();
    if (_audiobook && _chapterIndex + 1 < _audiobook.chapters.length) {
      loadChapter(_chapterIndex + 1);
      play();
    } else {
      _playing = false;
      updatePlayBtn();
      saveProgress(true);
      bus.emit('audiobook:state', 'idle');
    }
  }

  function onAudioError(e) {
    console.error('[AB Reader] Audio error:', e);
    _playing = false;
    updatePlayBtn();
    bus.emit('audiobook:state', 'error');
  }

  // ── MediaSession ───────────────────────────────────────────────────────────
  // ── Progress ───────────────────────────────────────────────────────────────
  function scheduleSave() {
    if (_saveTimer) return;
    _saveTimer = setTimeout(function () {
      _saveTimer = null;
      saveProgress();
    }, 5000);
  }

  function saveProgress(finished) {
    if (!_audiobook || !api) return;
    if (_saveTimer) {
      clearTimeout(_saveTimer);
      _saveTimer = null;
    }
    trackListenedDelta();
    var posMs = _audio ? Math.max(0, Math.round((_audio.currentTime || 0) * 1000)) : 0;
    var data = {
      chapterIndex: _chapterIndex,
      positionMs: posMs,
      speed: _audio ? _audio.playbackRate : _playbackRate,
      volume: _audio ? _audio.volume : _volume,
      perChapterListenedMs: _perChapterListenedMs,
      updatedAt: Date.now(),
    };
    try { api.audiobooks.saveProgress(_audiobook.id, data); } catch (_) {}
  }

  // ── UI updates ─────────────────────────────────────────────────────────────
  function updatePlayBtn() {
    if (el.playPause) el.playPause.innerHTML = _playing ? SVG_PAUSE : SVG_PLAY;
  }

  function updateChapterLabel() {
    if (!el.chLabel || !_audiobook) return;
    var ch = _audiobook.chapters[_chapterIndex];
    var label = 'Ch.' + (_chapterIndex + 1);
    if (ch && ch.title) label = ch.title;
    el.chLabel.textContent = label;
    el.chLabel.title = label;
  }

  function updateTimeDisplay(cur, dur) {
    if (el.time) el.time.textContent = fmt(cur) + ' / ' + fmt(dur);
    if (el.seek && !_seekDragging) {
      var safeDur = (dur && isFinite(dur) && dur > 0) ? dur : 0;
      el.seek.max = String(safeDur || 100);
      el.seek.value = String(Math.max(0, Math.min(safeDur || 100, cur || 0)));
      el.seek.disabled = !safeDur;
    }
  }

  function onSeekInput() {
    if (!el.seek) return;
    _seekDragging = true;
    clearAutoHide();
    showBar();
    var preview = parseFloat(el.seek.value || '0');
    var dur = (_audio && isFinite(_audio.duration)) ? (_audio.duration || 0) : 0;
    if (isFinite(preview)) updateTimeDisplay(preview, dur);
  }

  function commitSeekFromUi() {
    if (!_audio || !el.seek) { _seekDragging = false; return; }
    var next = parseFloat(el.seek.value || '0');
    if (isFinite(next)) {
      try { _audio.currentTime = Math.max(0, next); } catch (_) {}
    }
    _seekDragging = false;
    _lastListenPositionMs = Math.max(0, Math.round((_audio.currentTime || 0) * 1000));
    updateTimeDisplay(_audio.currentTime || 0, _audio.duration || 0);
    saveProgress();
    if (_playing) startAutoHide();
  }

  // ── Bar visibility / auto-hide ─────────────────────────────────────────────
  function showBar() {
    if (!el.bar || !_loaded) return;
    el.bar.classList.remove('hidden');
    el.bar.classList.remove('ab-bar-autohide');
    _barVisible = true;
  }

  function hideBar() {
    if (!el.bar) return;
    el.bar.classList.add('ab-bar-autohide');
    _barVisible = false;
  }

  function startAutoHide() {
    clearAutoHide();
    if (!_playing) return;
    _barHideTimer = setTimeout(function () {
      if (_playing && !_seekDragging) hideBar();
    }, BAR_AUTO_HIDE_MS);
  }

  function clearAutoHide() {
    if (_barHideTimer) { clearTimeout(_barHideTimer); _barHideTimer = null; }
  }

  function resetAutoHide() {
    showBar();
    startAutoHide();
  }

  // ── Public API: loadAudiobook ──────────────────────────────────────────────
  function loadAudiobook(audiobook, resumeOpts) {
    if (!audiobook || !audiobook.chapters || !audiobook.chapters.length) return;
    ensureEls();

    // Mutual exclusion: stop TTS
    if (isTTSActive()) stopTTS();

    _audiobook = audiobook;
    _loaded = true;
    _perChapterListenedMs = (resumeOpts && resumeOpts.perChapterListenedMs && typeof resumeOpts.perChapterListenedMs === 'object')
      ? Object.assign({}, resumeOpts.perChapterListenedMs)
      : {};
    var startIdx = (resumeOpts && resumeOpts.chapterIndex) || 0;
    var startPos = 0;
    if (resumeOpts && isFinite(Number(resumeOpts.positionMs))) {
      startPos = Math.max(0, Number(resumeOpts.positionMs) / 1000);
    } else {
      startPos = (resumeOpts && resumeOpts.position) || 0;
    }
    var autoplay = !!(resumeOpts && resumeOpts.autoplay);

    loadChapter(startIdx);
    if (resumeOpts && isFinite(Number(resumeOpts.speed))) {
      _playbackRate = Math.max(0.5, Math.min(3.0, Number(resumeOpts.speed)));
    }
    if (resumeOpts && isFinite(Number(resumeOpts.volume))) {
      _volume = Math.max(0, Math.min(1, Number(resumeOpts.volume)));
    }
    if (_audio) {
      _audio.playbackRate = _playbackRate;
      _audio.volume = _volume;
    }
    if (el.speed) el.speed.value = String(_playbackRate);
    if (el.volume) el.volume.value = String(_volume);
    if (autoplay) showBar();
    else if (el.bar) {
      el.bar.classList.add('hidden');
      el.bar.classList.remove('ab-bar-autohide');
    }
    _playing = false;
    updatePlayBtn();

    if (startPos > 0) {
      var onMeta = function () {
        _audio.removeEventListener('loadedmetadata', onMeta);
        _audio.currentTime = startPos;
        if (autoplay) play();
      };
      _audio.addEventListener('loadedmetadata', onMeta);
    } else if (autoplay) {
      play();
    }

    bus.emit('audiobook:state', autoplay ? 'playing' : 'paused');
  }

  function closeAudiobook() {
    if (_audio) {
      _audio.pause();
      saveProgress();
    }
    _playing = false;
    _audiobook = null;
    _loaded = false;
    if (el.bar) { el.bar.classList.add('hidden'); el.bar.classList.remove('ab-bar-autohide'); }
    bus.emit('audiobook:state', 'idle');
  }

  // ── Module lifecycle (reader_core.js integration) ──────────────────────────

  function bind() {
    ensureEls();
    if (!el.bar) return;

    // Transport controls
    if (el.playPause) el.playPause.addEventListener('click', togglePlayPause);
    if (el.prevCh) el.prevCh.addEventListener('click', prevChapter);
    if (el.nextCh) el.nextCh.addEventListener('click', nextChapter);
    if (el.rew15) el.rew15.addEventListener('click', function () { seekRelative(-15); });
    if (el.fwd15) el.fwd15.addEventListener('click', function () { seekRelative(15); });
    if (el.close) el.close.addEventListener('click', closeAudiobook);

    // Sleep timer — cycle through modes on click
    // Volume slider
    if (el.volume) {
      el.volume.addEventListener('input', function () { setVolume(parseFloat(el.volume.value)); });
    }
    if (el.speed) {
      el.speed.addEventListener('change', function () { setRate(parseFloat(el.speed.value)); });
    }

    // Scrub bar
    if (el.seek) {
      el.seek.addEventListener('input', onSeekInput);
      el.seek.addEventListener('change', commitSeekFromUi);
      el.seek.addEventListener('pointerdown', function () { _seekDragging = true; clearAutoHide(); showBar(); });
      el.seek.addEventListener('pointerup', commitSeekFromUi);
      el.seek.addEventListener('mousedown', function () { _seekDragging = true; clearAutoHide(); showBar(); });
      el.seek.addEventListener('mouseup', commitSeekFromUi);
      el.seek.addEventListener('touchstart', function () { _seekDragging = true; clearAutoHide(); showBar(); }, { passive: true });
      el.seek.addEventListener('touchend', commitSeekFromUi, { passive: true });
    }

    // Auto-hide: hover detection on bar
    el.bar.addEventListener('mouseenter', function () { _barHoverBar = true; resetAutoHide(); });
    el.bar.addEventListener('mousemove', function () { if (_loaded && _playing) resetAutoHide(); });
    el.bar.addEventListener('mouseleave', function () { _barHoverBar = false; startAutoHide(); });

    // Auto-hide: movement-driven reveal on the reading area
    var readingArea = el.bar.parentElement;
    if (readingArea) {
      readingArea.addEventListener('mousemove', function () {
        if (!_loaded || !_playing) return;
        _barHoverBottom = false;
        resetAutoHide();
      }, { passive: true });
    }

    // Listen for TTS starting — close audiobook
    if (bus) {
      bus.on('reader:tts-state', function (status) {
        if (status === 'playing' && _loaded) {
          closeAudiobook();
        }
      });
    }
  }

  function onOpen() {
    // When a book opens, check if it has a paired audiobook and auto-load it
    // (Phase 4 will implement pairing — for now, just ensure bar state is clean)
    ensureEls();
  }

  function onClose() {
    // Reader closing — save progress and clean up
    if (_loaded) {
      if (_audio) { _audio.pause(); saveProgress(); }
      _playing = false;
      _audiobook = null;
      _loaded = false;
    }
    if (el.bar) { el.bar.classList.add('hidden'); el.bar.classList.remove('ab-bar-autohide'); }
  }

  // beforeunload save
  window.addEventListener('beforeunload', function () {
    if (_audiobook && _audio) saveProgress();
  });

  // ── Export ──────────────────────────────────────────────────────────────────
  window.booksReaderAudiobook = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    loadAudiobook: loadAudiobook,
    closeAudiobook: closeAudiobook,
    isLoaded: function () { return _loaded; },
    isPlaying: function () { return _playing; },
    getAudiobook: function () { return _audiobook; },
    getProgress: function () {
      return {
        chapterIndex: _chapterIndex,
        positionMs: _audio ? Math.max(0, Math.round((_audio.currentTime || 0) * 1000)) : 0,
        duration: _audio ? _audio.duration : 0,
        totalChapters: _audiobook ? _audiobook.chapters.length : 0,
        chapterTitle: _audiobook ? _audiobook.chapters[_chapterIndex].title : '',
        speed: _playbackRate,
        volume: _volume,
        perChapterListenedMs: _perChapterListenedMs
      };
    },
    play: play,
    pause: pause,
    togglePlayPause: togglePlayPause,
    seekRelative: seekRelative,
    nextChapter: nextChapter,
    prevChapter: prevChapter,
    setRate: setRate,
    setVolume: setVolume,
    setChapterIndex: setChapterIndex,
    getCurrentChapterIndex: getCurrentChapterIndex,
  };

})();
