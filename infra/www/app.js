(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  function el(tag, text, cls) {
    var e = document.createElement(tag);
    if (text != null) e.textContent = text;
    if (cls) e.className = cls;
    return e;
  }
  function put(box, nodes) {
    while (box.firstChild) box.removeChild(box.firstChild);
    nodes.forEach(function (n) { box.appendChild(n); });
  }
  function getJson(url) {
    return fetch(url, { cache: "no-cache" }).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    });
  }
  function size(n) {
    return n >= 1048576 ? (n / 1048576).toFixed(1) + " MB" : Math.round(n / 1024) + " KB";
  }
  var NAMES = { linux: "Linux", macos: "macOS", windows: "Windows" };

  function renderDownloads(d) {
    var nodes = [];
    if (!d || typeof d.version !== "string" || !d.files) throw new Error("bad");
    nodes.push(el("p", "Latest version: " + d.version));
    Object.keys(d.files).forEach(function (k) {
      var f = d.files[k];
      if (!f || typeof f.url !== "string" || f.url.charAt(0) !== "/") return;
      var row = el("div"), a = el("a", "Download for " + (NAMES[k] || k) + " (" + size(f.size) + ")", "btn");
      a.href = f.url;
      var det = el("details");
      det.appendChild(el("summary", "sha256"));
      det.appendChild(el("code", String(f.sha256)));
      row.appendChild(a);
      row.appendChild(det);
      nodes.push(row);
    });
    if (!d.files.windows) nodes.push(el("p", "Windows: not yet available (planned for M7).", "muted"));
    var all = el("p"), l = el("a", "All versions and checksums");
    l.href = "/downloads/";
    all.appendChild(l);
    nodes.push(all);
    put($("dl-body"), nodes);
  }
  function downloadsFailed() {
    var p = el("p", "Could not load the download list. "), a = el("a", "Open the downloads page");
    a.href = "/downloads/";
    p.appendChild(a);
    put($("dl-body"), [p]);
  }

  function copyBtn(text) {
    var b = el("button", "Copy", "small");
    b.type = "button";
    b.setAttribute("aria-label", "Copy address " + text);
    b.onclick = function () {
      if (!(navigator.clipboard && navigator.clipboard.writeText)) return;
      navigator.clipboard.writeText(text).then(function () {
        b.textContent = "Copied";
        setTimeout(function () { b.textContent = "Copy"; }, 1500);
      }, function () {});
    };
    return b;
  }
  function renderServers(s) {
    var box = $("sv-body");
    if (!s || s.schema !== 1 || !Array.isArray(s.servers)) {
      put(box, [el("p", "Unsupported status format.", "err")]);
      return;
    }
    if (!s.servers.length) { put(box, [el("p", "No servers reported.")]); return; }
    var wrap = el("div", null, "table-wrap"), t = el("table"), tr = el("tr"), head = el("thead");
    ["Server", "Map", "Mode", "Players", "Phase", "Updated", "Address"].forEach(function (h) {
      var th = el("th", h);
      th.scope = "col";
      tr.appendChild(th);
    });
    head.appendChild(tr);
    t.appendChild(head);
    var body = el("tbody");
    s.servers.forEach(function (v) {
      var r = el("tr"), p = v.players + "/" + v.max_players;
      if (v.bots) p += " (+" + v.bots + " bots)";
      [v.name, v.map_display_name || v.map, v.mode, p, v.phase, v.updated_s_ago + " s ago"].forEach(function (x) {
        r.appendChild(el("td", x == null ? "" : String(x)));
      });
      var td = el("td");
      if (v.address) {
        td.appendChild(el("code", String(v.address)));
        td.appendChild(copyBtn(String(v.address)));
      }
      r.appendChild(td);
      body.appendChild(r);
    });
    t.appendChild(body);
    wrap.appendChild(t);
    put(box, [wrap]);
  }
  function loadServers() {
    getJson("/status/servers.json").then(renderServers, function () {
      put($("sv-body"), [el("p", "Could not load the server list.", "err")]);
    });
  }

  getJson("/downloads/latest.json").then(function (d) {
    try { renderDownloads(d); } catch (e) { downloadsFailed(); }
  }, downloadsFailed);
  loadServers();
  setInterval(function () { if (!document.hidden) loadServers(); }, 15000);
  document.addEventListener("visibilitychange", function () { if (!document.hidden) loadServers(); });
})();
