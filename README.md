# The Asterisk(R) Open Source PBX
```text
        By Mark Spencer <markster@digium.com> and the Asterisk.org developer community.
        Copyright (C) 2001-2021 Sangoma Technologies Corporation and other copyright holders.
```
## SECURITY

  It is imperative that you read and fully understand the contents of
the security information document before you attempt to configure and run
an Asterisk server.

See [Important Security Considerations] for more information.

## WHAT IS ASTERISK ?

  Asterisk is an Open Source PBX and telephony toolkit.  It is, in a
sense, middleware between Internet and telephony channels on the bottom,
and Internet and telephony applications at the top.  However, Asterisk supports
more telephony interfaces than just Internet telephony.  Asterisk also has a
vast amount of support for traditional PSTN telephony, as well.

  For more information on the project itself, please visit the Asterisk
[home page] and the official [wiki].  In addition you'll find lots
of information compiled by the Asterisk community at [voip-info.org].

  There is a book on Asterisk published by O'Reilly under the Creative Commons
License. It is available in book stores as well as in a downloadable version on
the [asteriskdocs.org] web site.

## SUPPORTED OPERATING SYSTEMS

### Linux

  The Asterisk Open Source PBX is developed and tested primarily on the
GNU/Linux operating system, and is supported on every major GNU/Linux
distribution.

### Others

  Asterisk has also been 'ported' and reportedly runs properly on other
operating systems as well, including Sun Solaris, Apple's Mac OS X, Cygwin,
and the BSD variants.

## GETTING STARTED

  First, be sure you've got supported hardware (but note that you don't need
ANY special hardware, not even a sound card) to install and run Asterisk.

Supported telephony hardware includes:
* All Analog and Digital Interface cards from [Sangoma]
* QuickNet Internet PhoneJack and LineJack (http://www.quicknet.net)
* any full duplex sound card supported by ALSA, OSS, or PortAudio
* any ISDN card supported by mISDN on Linux
* The Xorcom Astribank channel bank
* VoiceTronix OpenLine products

### UPGRADING FROM AN EARLIER VERSION

  If you are updating from a previous version of Asterisk, make sure you
read the [UPGRADE.txt] file in the source directory. There are some files
and configuration options that you will have to change, even though we
made every effort possible to maintain backwards compatibility.

  In order to discover new features to use, please check the configuration
examples in the [configs] directory of the source code distribution.  For a
list of new features in this version of Asterisk, see the [CHANGES] file.

### NEW INSTALLATIONS

  Ensure that your system contains a compatible compiler and development
libraries.  Asterisk requires either the GNU Compiler Collection (GCC) version
4.1 or higher, or a compiler that supports the C99 specification and some of
the gcc language extensions.  In addition, your system needs to have the C
library headers available, and the headers and libraries for ncurses.

  There are many modules that have additional dependencies.  To see what
libraries are being looked for, see `./configure --help`, or run
`make menuselect` to view the dependencies for specific modules.

  On many distributions, these dependencies are installed by packages with names
like 'glibc-devel', 'ncurses-devel', 'openssl-devel' and 'zlib-devel'
or similar.

So, let's proceed:
1. Read this file.

  There are more documents than this one in the [doc] directory.  You may also
want to check the configuration files that contain examples and reference
guides in the [configs] directory.

2. Run `./configure`

  Execute the configure script to guess values for system-dependent
variables used during compilation. If the script indicates that some required 
components are missing, you can run `./contrib/scripts/install_prereq install`
to install the necessary components. Note that this will install all dependencies for every functionality of Asterisk. After running the script, you will need
to rerun `./configure`.

3. Run `make menuselect` _\[optional]_

  This is needed if you want to select the modules that will be compiled and to
check dependencies for various optional modules.

4. Run `make`

Assuming the build completes successfully:

5. Run `make install`

  If this is your first time working with Asterisk, you may wish to install
the sample PBX, with demonstration extensions, etc.  If so, run:

6. Run `make samples`

  Doing so will overwrite any existing configuration files you have installed.

7. Finally, you can launch Asterisk in the foreground mode (not a daemon) with:
```
        # asterisk -vvvc
```
  You'll see a bunch of verbose messages fly by your screen as Asterisk
initializes (that's the "very very verbose" mode).  When it's ready, if
you specified the "c" then you'll get a command line console, that looks
like this:
```
        *CLI>
```
  You can type "core show help" at any time to get help with the system.  For help
with a specific command, type "core show help <command>".  To start the PBX using
your sound card, you can type "console dial" to dial the PBX.  Then you can use
"console answer", "console hangup", and "console dial" to simulate the actions
of a telephone.  Remember that if you don't have a full duplex sound card
(and Asterisk will tell you somewhere in its verbose messages if you do/don't)
then it won't work right (not yet).

  "man asterisk" at the Unix/Linux command prompt will give you detailed
information on how to start and stop Asterisk, as well as all the command
line options for starting Asterisk.

  Feel free to look over the configuration files in `/etc/asterisk`, where you
will find a lot of information about what you can do with Asterisk.

### ABOUT CONFIGURATION FILES

  All Asterisk configuration files share a common format.  Comments are
delimited by ';' (since '#' of course, being a DTMF digit, may occur in
many places).  A configuration file is divided into sections whose names
appear in []'s.  Each section typically contains two types of statements,
those of the form 'variable = value', and those of the form 'object =>
parameters'.  Internally the use of '=' and '=>' is exactly the same, so
they're used only to help make the configuration file easier to
understand, and do not affect how it is actually parsed.

  Entries of the form 'variable=value' set the value of some parameter in
asterisk.  For example, in [chan_dahdi.conf], one might specify:
```
	switchtype=national
```
  In order to indicate to Asterisk that the switch they are connecting to is
of the type "national".  In general, the parameter will apply to
instantiations which occur below its specification.  For example, if the
configuration file read:
```
	switchtype = national
	channel => 1-4
	channel => 10-12
	switchtype = dms100
	channel => 25-47
```

  The "national" switchtype would be applied to channels one through
four and channels 10 through 12, whereas the "dms100" switchtype would
apply to channels 25 through 47.

  The "object => parameters" instantiates an object with the given
parameters.  For example, the line "channel => 25-47" creates objects for
the channels 25 through 47 of the card, obtaining the settings
from the variables specified above.

### SPECIAL NOTE ON TIME

  Those using SIP phones should be aware that Asterisk is sensitive to
large jumps in time.  Manually changing the system time using date(1)
(or other similar commands) may cause SIP registrations and other
internal processes to fail.  If your system cannot keep accurate time
by itself use [NTP] to keep the system clock
synchronized to "real time".  NTP is designed to keep the system clock
synchronized by speeding up or slowing down the system clock until it
is synchronized to "real time" rather than by jumping the time and
causing discontinuities. Most Linux distributions include precompiled
versions of NTP.  Beware of some time synchronization methods that get
the correct real time periodically and then manually set the system
clock.

  Apparent time changes due to daylight savings time are just that,
apparent.  The use of daylight savings time in a Linux system is
purely a user interface issue and does not affect the operation of the
Linux kernel or Asterisk.  The system clock on Linux kernels operates
on UTC.  UTC does not use daylight savings time.

  Also note that this issue is separate from the clocking of TDM
channels, and is known to at least affect SIP registrations.

### FILE DESCRIPTORS

  Depending on the size of your system and your configuration,
Asterisk can consume a large number of file descriptors.  In UNIX,
file descriptors are used for more than just files on disk.  File
descriptors are also used for handling network communication
(e.g. SIP, IAX2, or H.323 calls) and hardware access (e.g. analog and
digital trunk hardware).  Asterisk accesses many on-disk files for
everything from configuration information to voicemail storage.

  Most systems limit the number of file descriptors that Asterisk can
have open at one time.  This can limit the number of simultaneous
calls that your system can handle.  For example, if the limit is set
at 1024 (a common default value) Asterisk can handle approximately 150
SIP calls simultaneously.  To change the number of file descriptors
follow the instructions for your system below:

#### PAM-BASED LINUX SYSTEM

  If your system uses PAM (Pluggable Authentication Modules) edit
`/etc/security/limits.conf`.  Add these lines to the bottom of the file:
```text
root            soft    nofile          4096
root            hard    nofile          8196
asterisk        soft    nofile          4096
asterisk        hard    nofile          8196
```

(adjust the numbers to taste).  You may need to reboot the system for
these changes to take effect.

#### GENERIC UNIX SYSTEM

  If there are no instructions specifically adapted to your system
above you can try adding the command `ulimit -n 8192` to the script
that starts Asterisk.

## MORE INFORMATION

  See the [doc] directory for more documentation on various features.
Again, please read all the configuration samples that include documentation
on the configuration options.

  Finally, you may wish to visit the [support] site and join the [mailing
list] if you're interested in getting more information.

Welcome to the growing worldwide community of Asterisk users!
```
        Mark Spencer, and the Asterisk.org development community
```

---

Asterisk is a trademark of Sangoma Technologies Corporation

[home page]: https://www.asterisk.org
[support]: https://www.asterisk.org/support
[wiki]: https://wiki.asterisk.org/
[mailing list]: http://lists.digium.com/mailman/listinfo/asterisk-users
[chan_dahdi.conf]: configs/samples/chan_dahdi.conf.sample
[voip-info.org]: http://www.voip-info.org/wiki-Asterisk
[asteriskdocs.org]: http://www.asteriskdocs.org
[NTP]: http://www.ntp.org/
[Sangoma]: https://www.sangoma.com/
[UPGRADE.txt]: UPGRADE.txt
[CHANGES]: CHANGES
[configs]: configs
[doc]: doc
[Important Security Considerations]: https://wiki.asterisk.org/wiki/display/AST/Important+Security+Considerations
