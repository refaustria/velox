# nginx vhost games.inceptum.at

Install (this host uses sites-available/sites-enabled):

    sudo cp infra/nginx/games.inceptum.at.conf /etc/nginx/sites-available/games.inceptum.at
    sudo ln -s /etc/nginx/sites-available/games.inceptum.at /etc/nginx/sites-enabled/
    sudo nginx -t && sudo systemctl reload nginx

Webroot sync:

    sudo mkdir -p /var/www/games.inceptum.at/{downloads,status}
    sudo rsync -a infra/www/ /var/www/games.inceptum.at/

TLS (first time; certbot rewrites the installed copy to add 443 and the redirect):

    sudo certbot --nginx -d games.inceptum.at --non-interactive --agree-tos -m bojan@referee.at --redirect

Renewal is automatic via `certbot.timer` (`systemctl list-timers | grep certbot`);
test with `sudo certbot renew --dry-run`. Note: certbot modifies the installed
file, so the repo copy is the pre-certbot template.

Status directory ownership (ADR 0007 section 2/3): `/var/www/games.inceptum.at/status/`
is owned by the `veloxmaster` system user (`chown veloxmaster:www-data`, mode 2755),
which is the only writer of `servers.json` (see `infra/master/README.md`). nginx only
reads it; the existing `/status/` location (JSON, `no-cache`) needs no change and the
heartbeat port (127.0.0.1:8790) is not proxied.
