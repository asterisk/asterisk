#!/usr/bin/env bash

chmod 0750 /etc/sudoers.d
chmod 0440 /etc/sudoers.d/jenkins

chown root:root -R /root
chmod -R go-rwx /root/.ssh
chown -R jenkins:jenkins /home/jenkins
chown -R jenkins:jenkins /srv/cache
chown -R jenkins:jenkins /srv/jenkins
chown -R jenkins:jenkins /srv/git
chmod -R go-rwx /home/jenkins/.ssh
chmod -R go-rwx /home/jenkins/.ssh/authorized_keys
