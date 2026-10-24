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
