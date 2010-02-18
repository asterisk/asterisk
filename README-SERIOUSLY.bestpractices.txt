==================
| Best Practices |
==================

The purpose of this document is to define best practices when working with
Asterisk in order to minimize possible security breaches and to provide tried
examples in field deployments. This is a living document and is subject to 
change over time as best practices are defined.

--------
Sections
--------

* Filtering Data: 
        How to protect yourself from redial attacks

* Proper Device Naming: 
        Why to not use numbered extensions for devices

* Secure Passwords: 
        Secure passwords limit your risk to brute force attacks

* Reducing Pattern Match Typos: 
        Using the 'same' prefix, or using Goto()

----------------
Additional Links
----------------

Additional links that contain useful information about best practices or
security are listed below.

* Seven Steps to Better SIP Security:
        http://blogs.digium.com/2009/03/28/sip-security/

* Asterisk VoIP Security (webinar):
        http://www.asterisk.org/security/webinar/


==============
Filtering Data
==============

In the Asterisk dialplan, several channel variables contain data potentially 
supplied by outside sources. This could lead to a potential security concern 
where those outside sources may send cleverly crafted strings of data which 
could be utilized, e.g. to place calls to unexpected locations.

An example of this can be found in the use of pattern matching and the ${EXTEN}
channel variable. Note that ${EXTEN} is not the only system created channel
variable, so it is important to be aware of where the data you're using is
coming from.

For example, this common dialplan takes 2 or more characters of data, starting 
with a number 0-9, and then accepts any additional information supplied by the
request.

[NOTE: We use SIP in this example, but is not limited to SIP only; protocols
       such as Jabber/XMPP or IAX2 are also susceptible to the same sort of
       injection problem.]
       

[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Dial(SIP/${EXTEN})
exten => _X.,n,Hangup()

This dialplan may be utilized to accept calls to extensions, which then dial a
numbered device name configured in one of the channel configuration files (such
as sip.conf, iax.conf, etc...) (see the section Proper Device Naming for more
information on why this approach is flawed).

The example we've given above looks harmless enough until you take into
consideration that several channel technologies accept characters that could
be utilized in a clever attack. For example, instead of just sending a request
to dial extension 500 (which in our example above would create the string
SIP/500 and is then used by the Dial() application to place a call), someone
could potentially send a string like "500&SIP/itsp/14165551212".

The string "500&SIP/itsp/14165551212" would then be contained within the 
${EXTEN} channel variable, which is then utilized by the Dial() application in
our example, thereby giving you the dialplan line of:

exten => _X.,n,Dial(SIP/500&SIP/itsp/14165551212)

Our example above has now provided someone with a method to place calls out of
your ITSP in a place where you didn't expect to allow it. There are a couple of
ways in which you can mitigate this impact: stricter pattern matching, or using
the FILTER() dialplan function.

Strict Pattern Matching
-----------------------

The simple way to mitigate this problem is with a strict pattern match that does
not utilize the period (.) or bang (!) characters to match on one-or-more 
characters or zero-or-more characters (respectively). To fine tune our example
to only accept three digit extensions, we could change our pattern match to
be:

exten => _XXX,n,Dial(SIP/${EXTEN})

In this way, we have minimized our impact because we're not allowing anything
other than the numbers zero through nine. But in some cases we really do need to
handle variable pattern matches, such as when dialing international numbers
or when we want to handle something like a SIP URI. In this case, we'll need to
utilize the FILTER() dialplan function.

Using FILTER()
--------------

The FILTER() dialplan function is used to filter strings by only allowing
characters that you have specified. This is a perfect candidate for controlling
which characters you want to pass to the Dial() application, or any other
application which will contain dynamic information passed to Asterisk from an
external source. Lets take a look at how we can use FILTER() to control what
data we allow.

Using our previous example to accept any string length of 2 or more characters, 
starting with a number of zero through nine, we can use FILTER() to limit what 
we will accept to just numbers. Our example would then change to something like:

[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Dial(SIP/${FILTER(0-9,${EXTEN})})
exten => _X.,n,Hangup()

Note how we've wrapped the ${EXTEN} channel variable with the FILTER() function
which will then only pass back characters that fit into the numerical range that
we've defined.

Alternatively, if we didn't want to utilize the FILTER() function within the
Dial() application directly, we could save the value to a channel variable,
which has a side effect of being usable in other locations of your dialplan if
necessary, and to handle error checking in a separate location.

[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Set(SAFE_EXTEN=${FILTER(0-9,${EXTEN})})
exten => _X.,n,Dial(SIP/${SAFE_EXTEN})
exten => _X.,n,Hangup()

Now we can use the ${SAFE_EXTEN} channel variable anywhere throughout the rest
of our dialplan, knowing we've already filtered it. We could also perform an
error check to verify that what we've received in ${EXTEN} also matches the data
passed back by FILTER(), and to fail the call if things do not match.

[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Set(SAFE_EXTEN=${FILTER(0-9,${EXTEN})})
exten => _X.,n,GotoIf($[${EXTEN} != ${SAFE_EXTEN}]?error,1)
exten => _X.,n,Dial(SIP/${SAFE_EXTEN})
exten => _X.,n,Hangup()

exten => error,1,Verbose(2,Values of EXTEN and SAFE_EXTEN did not match.)
exten => error,n,Verbose(2,EXTEN: "${EXTEN}" -- SAFE_EXTEN: "${SAFE_EXTEN}")
exten => error,n,Playback(silence/1&invalid)
exten => error,n,Hangup()

Another example would be using FILTER() to control the characters we accept when
we're expecting to get a SIP URI for dialing.

[incoming]
exten => _[0-9a-zA-Z].,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _[0-9a-zA-Z].,n,Dial(SIP/${FILTER(.@0-9a-zA-Z,${EXTEN})
exten => _[0-9a-zA-Z].,n,Hangup()

Of course the FILTER() function doesn't check the formatting of the incoming
request. There is also the REGEX() dialplan function which can be used to
determine if the string passed to it matches the regular expression you've
created, and to take proper action on whether it matches or not. The creation of
regular expressions is left as an exercise for the reader.

More information about the FILTER() and REGEX() dialplan functions can be found
by typing "core show function FILTER" and "core show function REGEX" from your
Asterisk console.


====================
Proper Device Naming
====================

In Asterisk, the concept of an extension number being tied to a specific device
does not exist. Asterisk is aware of devices it can call or receive calls from,
and how you define in your dialplan how to reach those devices is up to you.

Because it has become common practice to think of a specific device as having an
extension number associated with it, it only becomes natural to think about
naming your devices the same as the extension number you're providing it. But
by doing this, you're limiting the powerful concept of separating user from
extensions, and extensions from devices.

It can also be a security hazard to name your devices with a number, as this can
open you up to brute force attacks. Many of the current exploits deal with
device configurations which utilize a number, and even worse, a password that
matches the devices name. For example, take a look at this poorly created device
in sip.conf:

[1000]
type=friend
context=international_dialing
secret=1000

As implied by the context, we've permitted a device named 1000 with a password
of 1000 to place calls internationally. If your PBX system is accessible via
the internet, then your system will be vulnerable to expensive international
calls. Even if your system is not accessible via the internet, people within
your organization could get access to dialing rules you'd prefer to reserve only
for certain people.

A more secure example for the device would be to use something like the MAC
address of the device, along with a strong password (see the section Secure
Passwords). The following example would be more secure:

[0004f2040001]
type=friend
context=international_dialing
secret=aE3%B8*$jk^G

Then in your dialplan, you would reference the device via the MAC address of the
device (or if using the softphone, a MAC address of a network interface on the
computer).

Also note that you should NOT use this password, as it will likely be one of the
first ones added to the dictionary for brute force attacks.


================
Secure Passwords
================

Secure passwords are necessary in many (if not all) environments, and Asterisk 
is certainly no exception, especially when it comes to expensive long distance
calls that could potentially cost your company hundreds or thousands of dollars
on an expensive monthly phone bill, with little to no recourse to fight the
charges.

Whenever you are positioned to add a password to your system, whether that is
for a device configuration, a database connection, or any other secure 
connection, be sure to use a secure password. A good example of a secure
password would be something like:

aE3%B8*$jk^G

Our password also contains 12 characters with a mixture of upper and
lower case characters, numbers, and symbols. Because these passwords are likely 
to only be entered once, or loaded via a configuration file, there is
no need to create simple passwords, even in testing. Some of the holes found in
production systems used for exploitations involve finding the one test extension
that contains a weak password that was forgotten prior to putting a system into
production.

Using a web search you can find several online password generators such as
http://www.strongpasswordgenerator.com or there are several scripts that can be
used to generate a strong password.


============================
Reducing Pattern Match Typos
============================

As of Asterisk 1.6.2, a new method for reducing the number of complex pattern
matches you need to enter, which can reduce typos in your dialplan, has been
implemented. Traditionally, a dialplan with a complex pattern match would look
something like:

exten => _[3-5]XXX,1,Verbose(Incoming call to ${EXTEN})
exten => _[3-5]XXX,n,Set(DEVICE=${DB(device/mac_address/${EXTEN})})
exten => _[3-5]XXX,n,Set(TECHNOLOGY=${DB(device/technology/${EXTEN})})
exten => _[3-5]XXX,n,GotoIf($[${ISNULL(${TECHNOLOGY})} | ${ISNULL(${DEVICE})}]?error,1)
exten => _[3-5]XXX,n,Dial(${TECHNOLOGY}/${DEVICE},${GLOBAL(TIMEOUT)})
exten => _[3-5]XXX,n,Set(vmFlag=${IF($[${DIALSTATUS} = BUSY]?b:u)})
exten => _[3-5]XXX,n,Voicemail(${EXTEN}@${GLOBAL(VOICEMAIL_CONTEXT)},${vmFlag})
exten => _[3-5]XXX,n,Hangup()

exten => error,1,Verbose(2,Unable to lookup technology or device for extension)
exten => error,n,Playback(silence/1&num-not-in-db)
exten => error,n,Hangup()

Of course there exists the possibility for a typo when retyping the pattern
match _[3-5]XXX which will match on extensions 3000 through 5999. We can
minimize this error by utilizing the same => prefix on all lines beyond the
first one. Our same dialplan with using same => would look like the following:

exten => _[3-5]XXX,1,Verbose(Incoming call to ${EXTEN})
same => n,Set(DEVICE=${DB(device/mac_address/${EXTEN})})
same => n,Set(TECHNOLOGY=${DB(device/technology/${EXTEN})})
same => n,GotoIf($[${ISNULL(${TECHNOLOGY})} | ${ISNULL(${DEVICE})}]?error,1)
same => n,Dial(${TECHNOLOGY}/${DEVICE},${GLOBAL(TIMEOUT)})
same => n,Set(vmFlag=${IF($[${DIALSTATUS} = BUSY]?b:u)})
same => n,Voicemail(${EXTEN}@${GLOBAL(VOICEMAIL_CONTEXT)},${vmFlag})
same => n,Hangup()

exten => error,1,Verbose(2,Unable to lookup technology or device for extension)
same => n,Playback(silence/1&num-not-in-db)
same => n,Hangup()
