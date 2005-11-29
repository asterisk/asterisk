Summary: Asterisk PBX
Name: asterisk
Distribution: RedHat
Version: CVS
Release: 1
Copyright: Linux Support Services, inc.
Group: Utilities/System
Vendor: Linux Support Services, inc.
Packager: Robert Vojta <vojta@ipex.cz>
BuildRoot: /tmp/asterisk

%description
Asterisk is an Open Source PBX and telephony development platform that
can both replace a conventional PBX and act as a platform for developing
custom telephony applications for delivering dynamic content over a
telephone similarly to how one can deliver dynamic content through a
web browser using CGI and a web server.

Asterisk talks to a variety of telephony hardware including BRI, PRI, 
POTS, and IP telephony clients using the Inter-Asterisk eXchange
protocol (e.g. gnophone or miniphone).  For more information and a
current list of supported hardware, see www.asteriskpbx.com.

%package        devel
Summary:        Header files for building Asterisk modules
Group:          Development/Libraries

%description devel
This package contains the development  header files that are needed
to compile 3rd party modules.

%post
ln -sf /var/spool/asterisk/vm /var/lib/asterisk/sounds/vm

%files
#
# Configuration files
#
%attr(0755,root,root) %dir    %{_sysconfdir}/asterisk
%config(noreplace) %attr(0640,root,root) %{_sysconfdir}/asterisk/*.conf
%config(noreplace) %attr(0640,root,root) %{_sysconfdir}/asterisk/*.adsi

#
# RedHat specific init script file
#
%attr(0755,root,root)       /etc/rc.d/init.d/asterisk

#
# Modules
#
%attr(0755,root,root) %dir /usr/lib/asterisk
%attr(0755,root,root) %dir /usr/lib/asterisk/modules
%attr(0755,root,root)      /usr/lib/asterisk/modules/*.so

#
# Asterisk
#
%attr(0755,root,root)      /usr/sbin/asterisk
%attr(0755,root,root)      /usr/sbin/safe_asterisk
%attr(0755,root,root)      /usr/sbin/astgenkey
%attr(0755,root,root)      /usr/sbin/astman

#
# Sound files
#
%attr(0755,root,root) %dir /var/lib/asterisk
%attr(0755,root,root) %dir /var/lib/asterisk/sounds
%attr(0644,root,root)      /var/lib/asterisk/sounds/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/sounds/digits
%attr(0644,root,root)      /var/lib/asterisk/sounds/digits/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/sounds/letters
%attr(0644,root,root)      /var/lib/asterisk/sounds/letters/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/sounds/phonetic
%attr(0644,root,root)      /var/lib/asterisk/sounds/phonetic/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/mohmp3
%attr(0644,root,root)      /var/lib/asterisk/mohmp3/*
%attr(0755,root,root) %dir /var/lib/asterisk/images
%attr(0644,root,root)      /var/lib/asterisk/images/*
%attr(0755,root,root) %dir /var/lib/asterisk/keys
%attr(0644,root,root)      /var/lib/asterisk/keys/*
%attr(0755,root,root) %dir /var/lib/asterisk/agi-bin
%attr(0755,root,root) %dir /var/lib/asterisk/agi-bin/*
#
# Man page
#
%attr(0644,root,root)      /usr/share/man/man8/asterisk.8.gz
#
# Firmware
#
%attr(0755,root,root) %dir /var/lib/asterisk/firmware
%attr(0755,root,root) %dir /var/lib/asterisk/firmware/iax
%attr(0755,root,root)      /var/lib/asterisk/firmware/iax/*.bin

#
# Example voicemail files
#
%attr(0755,root,root) %dir /var/spool/asterisk
%attr(0755,root,root) %dir /var/spool/asterisk/voicemail
%attr(0755,root,root) %dir /var/spool/asterisk/voicemail/default
%attr(0755,root,root) %dir /var/spool/asterisk/voicemail/default/1234
%attr(0755,root,root) %dir /var/spool/asterisk/voicemail/default/1234/INBOX
%attr(0644,root,root)      /var/spool/asterisk/voicemail/default/1234/*.gsm

%files devel
#
# Include files
#
%attr(0755,root,root) %dir %{_includedir}/asterisk
%attr(0644,root,root) %{_includedir}/asterisk/*.h
