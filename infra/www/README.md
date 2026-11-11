# Landing page (games.inceptum.at)

Static HTML/CSS/JS, no framework, no external requests. Deploy (root step, only when told): `sudo rsync -a infra/www/ /var/www/games.inceptum.at/` (the rsync has no `--delete`, so `downloads/` and `status/` are untouched).

- `index.html` + `app.js`: Downloads block from `/downloads/latest.json`, Servers block from `/status/servers.json` (schema 1, refresh 15 s while visible). Without JavaScript the blocks show static links.
- `downloads-index.template.html`: template for `/downloads/index.html`, filled by `infra/deploy/publish-download.sh`. Placeholders: `@@LATEST_VERSION@@`, `@@GENERATED_AT@@`, `@@ROWS@@`; one row per file, current version first, then the previous one:
  `<tr><td>VER</td><td>PLATFORM</td><td><a href="/downloads/VER/FILE">FILE</a></td><td>SIZE</td><td><code>SHA256</code></td></tr>`
- Answer-dependent wording (grep to change): privacy line (q45), Windows (q46), macOS Gatekeeper (q47), Linux "tested headless only" (q48) in `index.html`, `app.js` (Windows line) and the template footer.
- Local check: `python3 -I -m http.server 8793 --directory <copy of this dir plus downloads/latest.json and status/servers.json>`.
