#!/usr/bin/env bash
mkdir -p /srv/cache/externals /srv/cache/sounds /srv/cache/ccache || :
chown -R jenkins:users /srv/cache
chmod g+rw /srv/cache/ccache
chmod g+s /srv/cache/ccache
mkdir -p tests/CI/output || :
chown -R jenkins:users tests/CI/output
