Summary: Asterisk PBX
Name: asterisk
Distribution: RedHat
Version: 
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

%post
ln -s /var/spool/asterisk/vm /var/lib/asterisk/sounds/vm

%files
#
# Configuration files
#
%attr(0755,root,root) %dir    /etc/asterisk
%attr(0640,root,root) %config /etc/asterisk/*.conf

#
# RedHat specific init script file
#
%attr(0755,root,root)       /etc/rc.d/init.d/asterisk

#
# Include files
#
%attr(0755,root,root) %dir /usr/include/asterisk
%attr(0644,root,root)      /usr/include/asterisk/*.h

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

#
# Sound files
#
%attr(0755,root,root) %dir /var/lib/asterisk
%attr(0755,root,root) %dir /var/lib/asterisk/sounds
%attr(0644,root,root)      /var/lib/asterisk/sounds/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/sounds/digits
%attr(0644,root,root)      /var/lib/asterisk/sounds/digits/*.gsm
%attr(0755,root,root) %dir /var/lib/asterisk/images
%attr(0644,root,root)      /var/lib/asterisk/images/*

#
# Example voicemail files
#
%attr(0755,root,root) %dir /var/spool/asterisk
%attr(0755,root,root) %dir /var/spool/asterisk/vm
%attr(0755,root,root) %dir /var/spool/asterisk/vm/1234
%attr(0755,root,root) %dir /var/spool/asterisk/vm/1234/INBOX
%attr(0644,root,root)      /var/spool/asterisk/vm/1234/*.gsm
