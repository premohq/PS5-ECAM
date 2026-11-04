// Playtendoh's site: the latest release from GitHub, the theme button, and sections appearing.
//
// The release in the page (#release-data) is the one that was latest when the site was built
// (site/build.py); the page shows it at once, then asks GitHub's API for the latest, which is what a
// reader sees unless the API turns them away (60 requests an hour for each address).
(function () {
  'use strict';

  var REPO = 'premohq/PlayTendoh';
  var API = 'https://api.github.com/repos/' + REPO + '/releases/latest';
  var root = document.documentElement;
  var $ = function (id) { return document.getElementById(id); };
  var each = function (selector, fn) { Array.prototype.forEach.call(document.querySelectorAll(selector), fn); };

  // The release notes open with a Download section (the ZIP, its size, its SHA-256 and how to update):
  // the card shows those itself, so the section is taken out of the notes.
  function splitDownload(body) {
    var lines = (body || '').replace(/\r\n?/g, '\n').split('\n');
    var i = 0;
    while (i < lines.length && !lines[i].trim()) i++;
    if (i >= lines.length || !/^#{1,3}\s+download\b/i.test(lines[i])) return { download: '', rest: lines.join('\n') };
    var j = i + 1;
    while (j < lines.length && !/^#{1,2}\s/.test(lines[j])) j++;
    return { download: lines.slice(i + 1, j).join('\n'), rest: lines.slice(j).join('\n') };
  }

  function facts(rel) {
    var tag = rel.tag_name || '';
    var version = tag.replace(/^v/i, '');
    var parts = splitDownload(rel.body);
    var section = parts.download;
    var zip = 'Playtendoh-' + tag + '.zip';
    var assets = Array.isArray(rel.assets) ? rel.assets : [];
    var asset = assets.filter(function (a) { return a.name === zip; })[0] ||
      assets.filter(function (a) { return /\.zip$/i.test(a.name || ''); })[0];
    var linked = section.match(/\]\((https:\/\/github\.com\/[^)\s]+\.zip)\)/i);
    var url = linked ? linked[1] : asset ? asset.browser_download_url : 'https://github.com/' + REPO + '/raw/releases/' + zip;
    var size = '';
    var match;
    if (asset && asset.size) size = (asset.size / 1e6).toFixed(1) + ' MB';
    else if ((match = section.match(/\((\d+(?:\.\d+)?)\s*MB\)/i))) size = match[1] + ' MB';
    var sha = '';
    if (asset && /^sha256:[0-9a-f]{64}$/i.test(asset.digest || '')) sha = asset.digest.slice(7);
    else if ((match = section.match(/\b[0-9a-f]{64}\b/i))) sha = match[0];
    var note = section.split(/\n\s*\n/).map(function (p) { return p.trim(); })
      .filter(function (p) { return p && !/\.zip\b/i.test(p) && !/sha-?256/i.test(p); }).join('\n\n');
    return {
      tag: tag,
      version: version,
      name: rel.name || 'Playtendoh ' + version,
      url: url,
      file: decodeURIComponent(url.split('/').pop()),
      size: size,
      sha: sha,
      note: note,
      notes: parts.rest,
      page: rel.html_url || 'https://github.com/' + REPO + '/releases/tag/' + tag,
      published: rel.published_at || ''
    };
  }

  // #12 in the notes is an issue or a pull request, as GitHub shows it
  function linkIssues(el) {
    var walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT, {
      acceptNode: function (node) {
        if (node.parentElement.closest('a, code, pre')) return NodeFilter.FILTER_REJECT;
        return /#\d/.test(node.nodeValue) ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_SKIP;
      }
    });
    var nodes = [];
    while (walker.nextNode()) nodes.push(walker.currentNode);
    nodes.forEach(function (node) {
      var text = node.nodeValue;
      var out = document.createDocumentFragment();
      var last = 0;
      var re = /(^|[^\w&/#])#(\d{1,6})\b/g;
      var m;
      while ((m = re.exec(text))) {
        var start = m.index + m[1].length;
        out.append(text.slice(last, start));
        var a = document.createElement('a');
        a.href = 'https://github.com/' + REPO + '/issues/' + m[2];
        a.textContent = '#' + m[2];
        out.append(a);
        last = start + 1 + m[2].length;
      }
      out.append(text.slice(last));
      node.replaceWith(out);
    });
  }

  function fill(el, md) {
    md = (md || '').trim();
    if (!md) { el.textContent = ''; return; }
    var html = null;
    if (window.marked && window.DOMPurify) {
      try { html = window.DOMPurify.sanitize(window.marked.parse(md, { gfm: true, async: false })); } catch (e) { html = null; }
    }
    if (html === null) {
      // the renderer didn't load: the notes as GitHub has them, as text
      el.textContent = md;
      el.classList.add('plain');
      return;
    }
    el.classList.remove('plain');
    el.innerHTML = html;
    linkIssues(el);
    Array.prototype.forEach.call(el.querySelectorAll('table'), function (table) {
      var wrap = document.createElement('div');
      wrap.className = 'table-wrap';
      table.replaceWith(wrap);
      wrap.append(table);
    });
    Array.prototype.forEach.call(el.querySelectorAll('img'), function (img) { img.loading = 'lazy'; img.decoding = 'async'; });
  }

  function fold(notes) {
    var more = $('notes-more');
    var button = $('notes-toggle');
    notes.classList.remove('collapsed');
    var long = notes.scrollHeight > 1000;
    more.hidden = !long;
    if (!long) return;
    notes.classList.add('collapsed');
    button.setAttribute('aria-expanded', 'false');
    button.textContent = 'Show all the notes';
  }

  function setSource(kind) {
    var el = $('release-source');
    el.classList.remove('live', 'baked');
    if (kind === 'live') { el.classList.add('live'); el.textContent = 'Live from GitHub Releases'; }
    else if (kind === 'baked') { el.classList.add('baked'); el.textContent = 'As of the last deploy'; }
  }

  var shown = '';

  function render(rel) {
    var f = facts(rel);
    if (!f.tag) return false;
    var key = JSON.stringify(f);
    if (key === shown) return true;
    shown = key;
    each('[data-version]', function (el) { el.textContent = f.version; });
    each('[data-zip]', function (el) { el.href = f.url; });
    each('[data-notes]', function (el) { el.href = f.page; });
    each('[data-size]', function (el) { el.textContent = f.size; });
    each('[data-size-line]', function (el) { el.textContent = f.size ? 'ZIP, ' + f.size : ''; });
    $('release-title').textContent = f.name;
    $('release-file').textContent = f.file;
    $('release-sha').textContent = f.sha;
    $('copy-sha').hidden = !f.sha;
    var time = $('release-date');
    var date = new Date(f.published);
    if (f.published && !isNaN(date)) {
      time.dateTime = f.published;
      time.textContent = 'Published ' + date.toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric', timeZone: 'UTC' });
    }
    fill($('release-note'), f.note);
    fill($('release-notes'), f.notes);
    fold($('release-notes'));
    return true;
  }

  function baked() {
    try { return JSON.parse($('release-data').textContent); } catch (e) { return null; }
  }

  function live() {
    var controller = 'AbortController' in window ? new AbortController() : null;
    var timer = controller && setTimeout(function () { controller.abort(); }, 10000);
    return fetch(API, { headers: { Accept: 'application/vnd.github+json' }, signal: controller && controller.signal })
      .then(function (res) { if (!res.ok) throw new Error('HTTP ' + res.status); return res.json(); })
      .then(function (rel) { return rel && rel.tag_name ? rel : null; })
      .catch(function () { return null; })
      .then(function (rel) { clearTimeout(timer); return rel; });
  }

  function release() {
    var saved = baked();
    var hasSaved = !!(saved && saved.tag_name && render(saved));
    live().then(function (rel) {
      if (rel && render(rel)) { setSource('live'); return; }
      if (hasSaved) { setSource('baked'); return; }
      var notes = $('release-notes');
      notes.innerHTML = '';
      var p = document.createElement('p');
      p.append("GitHub's API didn't answer just now. ");
      var a = document.createElement('a');
      a.href = 'https://github.com/' + REPO + '/releases/latest';
      a.textContent = 'Read the latest release on GitHub';
      p.append(a, '.');
      notes.append(p);
    });

    $('notes-toggle').addEventListener('click', function () {
      var notes = $('release-notes');
      var open = !notes.classList.toggle('collapsed');
      this.setAttribute('aria-expanded', String(open));
      this.textContent = open ? 'Show less' : 'Show all the notes';
      if (!open) $('release').scrollIntoView({ block: 'start' });
    });

    $('copy-sha').addEventListener('click', function () {
      var button = this;
      var sha = $('release-sha').textContent.trim();
      var done = function (text) { button.textContent = text; setTimeout(function () { button.textContent = 'Copy'; }, 1600); };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(sha).then(function () { done('Copied'); }, function () { done('Select it'); });
      } else {
        var range = document.createRange();
        range.selectNodeContents($('release-sha'));
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      }
    });
  }

  // Light or dark: the system's, until the reader picks the other one
  function theme() {
    var button = $('theme-toggle');
    var media = window.matchMedia('(prefers-color-scheme: dark)');
    var system = function () { return media.matches ? 'dark' : 'light'; };
    var current = function () { return root.getAttribute('data-theme') || system(); };
    var paint = function () {
      var dark = current() === 'dark';
      button.querySelector('use').setAttribute('href', dark ? '#i-sun' : '#i-moon');
      button.setAttribute('aria-label', dark ? 'Use the light theme' : 'Use the dark theme');
      button.title = dark ? 'Light theme' : 'Dark theme';
    };
    button.hidden = false;
    paint();
    button.addEventListener('click', function () {
      var next = current() === 'dark' ? 'light' : 'dark';
      try {
        if (next === system()) { root.removeAttribute('data-theme'); localStorage.removeItem('ps5-ecam-theme'); }
        else { root.setAttribute('data-theme', next); localStorage.setItem('ps5-ecam-theme', next); }
      } catch (e) {
        root.setAttribute('data-theme', next);
      }
      paint();
    });
    if (media.addEventListener) media.addEventListener('change', paint);
  }

  function reveal() {
    var items = document.querySelectorAll('.reveal');
    var still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (still || !('IntersectionObserver' in window)) {
      Array.prototype.forEach.call(items, function (el) { el.classList.add('in'); });
      return;
    }
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('in');
        observer.unobserve(entry.target);
      });
    }, { rootMargin: '0px 0px -6% 0px' });
    Array.prototype.forEach.call(items, function (el) {
      var siblings = Array.prototype.filter.call(el.parentElement.children, function (c) { return c.classList.contains('reveal'); });
      el.style.transitionDelay = Math.min(siblings.indexOf(el), 5) * 60 + 'ms';
      observer.observe(el);
    });
  }

  theme();
  reveal();
  release();
})();
