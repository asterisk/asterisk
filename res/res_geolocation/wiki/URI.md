{section:border=false}
{column:width=70%}

h1. Introduction

As mentioned in other pages, Geolocation descriptions can be passed "by-value" using a GML or Civic Address XML document, or "by-reference" using a URI.  This page discusses the latter.

h1. Concepts

h2.  Outgoing Calls
Passing location descriptions using URIs is fairly simple from an Asterisk perspective.  It does however, require the implementer to establish and maintain infrastructure to handle the serving of those URIs.  Given the critical nature of the information, setting up such infrastructure is not trivial and is beyond the scope of Asterisk and this documentation.

h2. Incoming calls
On incoming calls, Asterisk will make any "pass-by-reference" URIs available to the dialplan via the {{GEOLOC_PROFILE}} function but will NOT attempt to retrieve any documents from that URI.  It's the dialplan author's responsibility to retrieve, interpret and process such documents.

h1. Example 1

Let's say that every extension in your organization has a public DID associated with it, you have a database that cross references DIDs and office locations, and you have a web server that can be queried with a "GET" request and an "DID" query parameter ({{https://my.company.com/location_query?DID=<did>}}) to get the DID's location.  When someone in your organization dials 911, you want a link sent in the outgoing SIP INVITE that the recipient can call to get the caller's location.

In geolocation.conf, you'd create Location and Profile objects as follows:
{code}
[did-xref]
type = location
format = URI
location = URI='https://my.company.com/location_query?DID=${CALLERID(num)}'

[employees-outbound]
type = profile
location_reference = did-xref
{code}

In pjsip.conf, you'd add a {{geoloc_outgoing_call_profile}} parameter to your _outgoing_ endpoint definition:
{code}
[my-provider]
type = endpoint
...
geoloc_outgoing_call_profile = employees-outbound
{code}

Now let's say that Bob has DID {{12125551212}} assigned to him and he makes an outgoing call which is routed to "my-provider".  Asterisk would automatically add the following header to the INVITE:
{code}
Geolocation: <https://my.company.com/location_query?DID=12125551212>
{code}
The recipient could then make a simply query using that URI and get Bob's location in whatever format was agreed upon with you and them.

Of course, this is a _very_ simple example that would add the Geolocation header to _all_ calls made via "my-provider".  If you only routed emergency calls to "my-provider" this would work fine but you probably don't want to leak location information on non-emergency calls.

h1. Example 2

In this example, we'll use the dialplan apps and functions to decide if we want to send location information to the recipient or not.  In fact, we're not going to use geolocation.conf at all.

In extensions.conf:

{code}
; The pre dial handler adds a new profile with a URI location to
; the outgoing channel when 911 is dialed and does nothing if another number is dialed.
[pre-dial-handler]
exten = 911,1,NoOp(Entering PDH for Outgoing Channel)
same  = n,Set(GEOLOC_PROFILE(format)=URI)
same  = n,Set(GEOLOC_PROFILE(location_info)=URI=https://my.company.com/location_query?DID=${CALLERID(num)})
same  = n,Return(0)
exten = _X.,1,Return(0)

[default]
exten = _X.,1,NoOp(Outgoing call)
; 'b' will run the pre-dial-handler on the outgoing channel.
same  = n,Dial(PJSIP/${EXTEN},5,b(pre-dial-handler))

{code}

{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}





