SystemD Socket Activation for Asterisk
======================================

This folder contains sample unit files which can be used as the basis of a
socket activated Asterisk deployment.  Socket activation support currently
extends to the following listeners:

* Asterisk Command-line Interface
* Asterisk Manager Interface (clear text and TLS)
* Builtin HTTP / HTTPS server

The primary use case of this feature is to allow Asterisk to be started by
other services through use of AMI, CLI or REST API.

The examples and documentation assume that Asterisk was linked to libsystemd
when compiled.  This integration is required for `Type=notify` and socket
activation to work.

Security
========

Care must be take if enabling socket activation on any IP:PORT that is not
protected by a firewall.  Any user that can reach any socket activation
port can start Asterisk, even if they do not have valid credentials to sign
into the service in question.  Enabling HTTP socket activation on a system
which provides SIP over websockets would allow remote users to start Asterisk
any time the HTTP socket is running.

This functionality bypasses the normal restriction where only 'root' can start
a service.  Enabling AMI socket activation allows any user on the local server
to start Asterisk by running 'telnet localhost 5038'.

CLI activation is secured by the combination of SocketUser, SocketGroup and
SocketMode settings in the systemd socket.  Only local users with access will
be able to start asterisk by using CLI.


Separate .socket units or a single unit
=======================================

Asterisk is a complex system with many components which can be enabled or
disabled individually.  Using socket activation requires deciding to use
a single socket file or multiple separate socket files.

The remainder of this README assumes separate socket units are used for each
listener.


Service and Socket files
========================

All .socket and .service examples in this folder use "reasonable" default
paths for Linux.  Depending on your distribution and ./configure options
you may need to modify these before installing.  The files are meant to
be examples rather than files to be blindly installed.


Installing and enabling socket units
====================================

Modify socket files as desired.  Install them to a location where systemd
will find them.  pkg-config can be used to determine an appropriate location.

For socket files to be managed directly by the local administrator:
    pkg-config systemd --variable systemdsystemconfdir

For socket files to be deployed by package manager:
    pkg-config systemd --variable systemdsystemunitdir


After installing socket files you must run 'systemctl daemon-reload' for
systemd to read the added/modified units.  After this you can enable the
desired sockets, for example to enable AMI:
    systemctl enable asterisk-ami.socket


Socket Selection
================

Asterisk configuration is unchanged by use of socket activation.  When a
component that supports socket activation starts a listener in Asterisk,
any sockets provided by systemd are iterated.  The systemd socket is used
when the bound address configured by Asterisk is an exact match with the
address given by the ListenStream setting in the systemd socket.


Command-line Interface
======================

Symbolic links do not appear to be resolved when checking the CLI listener.
This may be of concern since /var/run is often a symbolic link to /run. Both
Asterisk and systemd must use /var/run, or both must use /run.  Mismatching
will result in service startup failure.

When socket activation is used for Asterisk CLI some asterisk.conf options
are ignored.  The following options from the [files] section are ignored
and must instead be set by the systemd socket file.
* astctlowner - use SocketUser
* astctlgroup - use SocketGroup
* astctlpermissions - use SocketMode

See asterisk-cli.socket for an example of these settings.


Stopping Asterisk
=================

Some existing asterisk.service files use CLI 'core stop now' for the ExecStop
command.  It is not recommended to use CLI to stop Asterisk on systems where
CLI socket activation is enabled.  If Asterisk fails to start systemd still
tries running the ExecStop command.  This can result in an loop where ExecStop
causes CLI socket activation to start Asterisk again.  A better way to deal
with shutdown is to use Type=notify and do not specify an ExecStop command.
See the example asterisk.service.


Unused Sockets
==============

Asterisk makes no attempt to check for sockets provided by systemd that are not
used.  It is the users responsibility to only provide sockets which Asterisk is
configured to use.
