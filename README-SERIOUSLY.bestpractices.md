# Best Practices

The purpose of this document is to define best practices when working with
Asterisk in order to minimize possible security breaches and to provide tried
examples in field deployments. This is a living document and is subject to
change over time as best practices are defined.

* [Filtering Data]:
        How to protect yourself from redial attacks
* [Proper Device Naming]:
        Why to not use numbered extensions for devices
* [Secure Passwords]:
        Secure passwords limit your risk to brute force attacks
* [Reducing Pattern Match Typos]:
        Using the 'same' prefix, or using Goto()
* [Manager Class Authorizations]:
        Recognizing potential issues with certain classes of authorization
* [Avoid Privilege Escalations]:
        Disable the ability to execute functions that may escalate privileges
* [Important Security Considerations]:
        More information on the Asterisk Wiki

## Additional Links

Additional links that contain useful information about best practices or
security are listed below.

* [Seven Steps to Better SIP Security][blog-sip-security]
* [Asterisk VoIP Security (webinar)][voip-security-webinar]


## Filtering Data

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

**NOTE**:
> We use SIP in this example, but is not limited to SIP only; protocols such as
> Jabber/XMPP or IAX2 are also susceptible to the same sort of injection problem.

```INI
[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Dial(SIP/${EXTEN})
exten => _X.,n,Hangup()
```

This dialplan may be utilized to accept calls to extensions, which then dial a
numbered device name configured in one of the channel configuration files (such
as sip.conf, iax.conf, etc...) (see [Proper Device Naming] for more information
on why this approach is flawed).

The example we've given above looks harmless enough until you take into
consideration that several channel technologies accept characters that could
be utilized in a clever attack. For example, instead of just sending a request
to dial extension 500 (which in our example above would create the string
SIP/500 and is then used by the Dial() application to place a call), someone
could potentially send a string like "500&SIP/itsp/14165551212".

The string "500&SIP/itsp/14165551212" would then be contained within the
${EXTEN} channel variable, which is then utilized by the Dial() application in
our example, thereby giving you the dialplan line of:

```INI
exten => _X.,n,Dial(SIP/500&SIP/itsp/14165551212)
```

Our example above has now provided someone with a method to place calls out of
your ITSP in a place where you didn't expect to allow it. There are a couple of
ways in which you can mitigate this impact: stricter pattern matching, or using
the FILTER() dialplan function.

The CALLERID(num) and CALLERID(name) values are other commonly used values that
are sources of data potentially supplied by outside sources.  If you use these
values as parameters to the System(), MixMonitor(), or Monitor() applications
or the SHELL() dialplan function, you can allow injection of arbitrary operating
system command execution.  The FILTER() dialplan function is available to remove
dangerous characters from untrusted strings to block the command injection.


### Strict Pattern Matching

The simple way to mitigate this problem is with a strict pattern match that does
not utilize the period (.) or bang (!) characters to match on one-or-more
characters or zero-or-more characters (respectively). To fine tune our example
to only accept three digit extensions, we could change our pattern match to
be:

```INI
exten => _XXX,n,Dial(SIP/${EXTEN})
```

In this way, we have minimized our impact because we're not allowing anything
other than the numbers zero through nine. But in some cases we really do need to
handle variable pattern matches, such as when dialing international numbers
or when we want to handle something like a SIP URI. In this case, we'll need to
utilize the FILTER() dialplan function.


### Using FILTER()

The FILTER() dialplan function is used to filter strings by only allowing
characters that you have specified. This is a perfect candidate for controlling
which characters you want to pass to the Dial() application, or any other
application which will contain dynamic information passed to Asterisk from an
external source. Lets take a look at how we can use FILTER() to control what
data we allow.

Using our previous example to accept any string length of 2 or more characters,
starting with a number of zero through nine, we can use FILTER() to limit what
we will accept to just numbers. Our example would then change to something like:

```INI
[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Dial(SIP/${FILTER(0-9,${EXTEN})})
exten => _X.,n,Hangup()
```

Note how we've wrapped the ${EXTEN} channel variable with the FILTER() function
which will then only pass back characters that fit into the numerical range that
we've defined.

Alternatively, if we didn't want to utilize the FILTER() function within the
Dial() application directly, we could save the value to a channel variable,
which has a side effect of being usable in other locations of your dialplan if
necessary, and to handle error checking in a separate location.

```INI
[incoming]
exten => _X.,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _X.,n,Set(SAFE_EXTEN=${FILTER(0-9,${EXTEN})})
exten => _X.,n,Dial(SIP/${SAFE_EXTEN})
exten => _X.,n,Hangup()
```

Now we can use the ${SAFE_EXTEN} channel variable anywhere throughout the rest
of our dialplan, knowing we've already filtered it. We could also perform an
error check to verify that what we've received in ${EXTEN} also matches the data
passed back by FILTER(), and to fail the call if things do not match.

```INI
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
```

Another example would be using FILTER() to control the characters we accept when
we're expecting to get a SIP URI for dialing.

```INI
[incoming]
exten => _[0-9a-zA-Z].,1,Verbose(2,Incoming call to extension ${EXTEN})
exten => _[0-9a-zA-Z].,n,Dial(SIP/${FILTER(.@0-9a-zA-Z,${EXTEN})
exten => _[0-9a-zA-Z].,n,Hangup()
```

Of course the FILTER() function doesn't check the formatting of the incoming
request. There is also the REGEX() dialplan function which can be used to
determine if the string passed to it matches the regular expression you've
created, and to take proper action on whether it matches or not. The creation of
regular expressions is left as an exercise for the reader.

More information about the FILTER() and REGEX() dialplan functions can be found
by typing "core show function FILTER" and "core show function REGEX" from your
Asterisk console.


## Proper Device Naming

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

```INI
[1000]
type=friend
context=international_dialing
secret=1000
```

As implied by the context, we've permitted a device named 1000 with a password
of 1000 to place calls internationally. If your PBX system is accessible via
the internet, then your system will be vulnerable to expensive international
calls. Even if your system is not accessible via the internet, people within
your organization could get access to dialing rules you'd prefer to reserve only
for certain people.

A more secure example for the device would be to use something like the MAC
address of the device, along with a strong password (see the section Secure
Passwords). The following example would be more secure:

```INI
[0004f2040001]
type=friend
context=international_dialing
secret=aE3%B8*$jk^G
```

Then in your dialplan, you would reference the device via the MAC address of the
device (or if using the softphone, a MAC address of a network interface on the
computer).

Also note that you should NOT use this password, as it will likely be one of the
first ones added to the dictionary for brute force attacks.


## Secure Passwords

Secure passwords are necessary in many (if not all) environments, and Asterisk
is certainly no exception, especially when it comes to expensive long distance
calls that could potentially cost your company hundreds or thousands of dollars
on an expensive monthly phone bill, with little to no recourse to fight the
charges.

Whenever you are positioned to add a password to your system, whether that is
for a device configuration, a database connection, or any other secure
connection, be sure to use a secure password. A good example of a secure
password would be something like:

```
aE3%B8*$jk^G
```

Our password also contains 12 characters with a mixture of upper and
lower case characters, numbers, and symbols. Because these passwords are likely
to only be entered once, or loaded via a configuration file, there is
no need to create simple passwords, even in testing. Some of the holes found in
production systems used for exploitations involve finding the one test extension
that contains a weak password that was forgotten prior to putting a system into
production.

Using a web search you can find several online password generators such as
[Strong Password Generator] or there are several scripts that can be
used to generate a strong password.


## Reducing Pattern Match Typos

As of Asterisk 1.6.2, a new method for reducing the number of complex pattern
matches you need to enter, which can reduce typos in your dialplan, has been
implemented. Traditionally, a dialplan with a complex pattern match would look
something like:

```INI
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
```

Of course there exists the possibility for a typo when retyping the pattern
match _\[3-5\]XXX which will match on extensions 3000 through 5999. We can
minimize this error by utilizing the same => prefix on all lines beyond the
first one. Our same dialplan with using same => would look like the following:

```INI
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
```


## Manager Class Authorizations

Manager accounts have associated class authorizations that define what actions
and events that account can execute/receive.  In order to run Asterisk commands
or dialplan applications that affect the system Asterisk executes on, the
"system" class authorization should be set on the account.

However, Manager commands that originate new calls into the Asterisk dialplan
have the potential to alter or affect the system as well, even though the
class authorization for origination commands is "originate".  Take, for example,
the Originate manager command:

```
Action: Originate
Channel: SIP/foo
Exten: s
Context: default
Priority: 1
Application: System
Data: echo hello world!
```

This manager command will attempt to execute an Asterisk application, System,
which is normally associated with the "system" class authorization.  While some
checks have been put into Asterisk to take this into account, certain dialplan
configurations and/or clever manipulation of the Originate manager action can
circumvent these checks.  For example, take the following dialplan:

```INI
exten => s,1,Verbose(Incoming call)
same => n,MixMonitor(foo.wav,,${EXEC_COMMAND})
same => n,Dial(SIP/bar)
same => n,Hangup()
```

Whatever has been defined in the variable EXEC_COMMAND will be executed after
MixMonitor has finished recording the call.  The dialplan writer may have
intended that this variable to be set by some other location in the dialplan;
however, the Manager action Originate allows for channel variables to be set by
the account initiating the new call.  This could allow the Originate action to
execute some command on the system by setting the EXEC_COMMAND dialplan variable
in the Variable: header.

In general, you should treat the Manager class authorization "originate" the
same as the class authorization "system".  Good system configuration, such as
not running Asterisk as root, can prevent serious problems from arising when
allowing external connections to originate calls into Asterisk.


## Avoid Privilege Escalations

External control protocols, such as Manager, often have the ability to get and
set channel variables; which allows the execution of dialplan functions.

Dialplan functions within Asterisk are incredibly powerful, which is wonderful
for building applications using Asterisk. But during the read or write
execution, certain dialplan functions do much more. For example, reading the
SHELL() function can execute arbitrary commands on the system Asterisk is
running on. Writing to the FILE() function can change any file that Asterisk has
write access to.

When these functions are executed from an external protocol, that execution
could result in a privilege escalation. Asterisk can inhibit the execution of
these functions, if live_dangerously in the \[options\] section of asterisk.conf
is set to no.

In Asterisk 12 and later, live_dangerously defaults to no.


[voip-security-webinar]: https://www.asterisk.org/security/webinar/
[blog-sip-security]: http://blogs.digium.com/2009/03/28/sip-security/
[Strong Password Generator]: https://www.strongpasswordgenerator.com
[Filtering Data]: #filtering-data
[Proper Device Naming]: #proper-device-naming
[Secure Passwords]: #secure-passwords
[Reducing Pattern Match Typos]: #reducing-pattern-match-typos
[Manager Class Authorizations]: #manager-class-authorizations
[Avoid Privilege Escalations]: #avoid-privilege-escalations
[Important Security Considerations]: https://wiki.asterisk.org/wiki/display/AST/Important+Security+Considerations
