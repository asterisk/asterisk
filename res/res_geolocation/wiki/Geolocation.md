{section:border=false}
{column:width=70%}

{warning:title=Please Read!}
Before you go off on a geolocation configuration spree, you'll need to understand a few things about Geolocation itself.
* It's not a single specification.
While a good part of the implementation is covered in RFCs, some of it is documented in the Geography Markup Language Specification, the 3GPP Technical Specifications, national organizations like the FCC and National Emergency Number Association in the US, and probably your interfacing carriers.  The last is the most important as you don't want emergency calls dropped or routed to incorrect emergency service centers because of a configuration incompatibility.

* It's been around a while.
The first references I could find date back to 2002.  Since then there have been innumerable changes including IETF drafts that expired 15 years ago that are still being returned by Google searches.

With that in mind, please do your own research and coordinate closely with your partners to validate your configuration.
{warning}

h1. Introduction

As it applies to Asterisk, Geolocation is the process of...
* A channel driver accepting location information in an incoming SIP INVITE, either by reference or by value, then using a geolocation profile to determine the disposition of that information and/or possibly add or delete information.
* Passing the resulting information (if any) to the dialplan which can also determine the disposition of that information and/or possibly add or delete information.
* Passing the information from the dialplan to the outgoing channel driver which can also use a geolocation profile to determine the disposition of that information and/or possibly add or delete information.
* Finally sending the information to another party, either by reference or by value.


h1. What's a "location"?

h2. Describing a Location
There are currently two ways to describe a location.

h3. Geography Markup Language (GML)
GML allows you to express a location in terms of shapes, coordinates, lengths, angles, etc.  For example, a Point with a latitude, longitude and altitude, or a Sphere with a latitude, longitude, altitude and radius. Other shapes include, Circle, Polygon, Ellipse, Ellipsoid, and Prism.  See [GeoShape|Geolocation Reference Information#geoshape].

GML would most often be used by mobile systems where the originator's location is determined dynamically such as base station, sector antenna, distance, etc.  According to [RFC4119|Geolocation Reference Information#rfc4119] GML is considered to be the "baseline" format and MUST be supported by all implementations.  The _level_ of support is not well defined however.  For instance, a specific implementation may only support a subset of shapes.

h3. Civic Address
For fixed locations, Civic Address is probably the most used location description method.  It's described with terms like Country, State/Province, City, Neighborhood, Street, House Number, Floor, Room, etc.  Oddly enough, support for Civic Address is NOT required by [RFC4119|Geolocation Reference Information#rfc4119].

Both methods are expressed in XML but which location description method you use is entirely between you and your partners.

h3. Encapsulation
The IETF chose the "Presence Information Data Format" (PIDF) as the wrapper document for location information which can be placed in {{<tuple>}}, {{<device>}}, or {{<person>}} sub-elements.  BTW, this is the same PIDF used to convey SIP subscription information but Asterisk is only supporting PIDF-LO in INVITE requests at this time.

The specification allows multiple locations in each element, multiple elements in a single PIDF-LO document, _and_ multiple PIDF-LO documents in a single request.  Dealing with multiple locations however is such an extraordinarily complex process that it's not support by Asterisk at this time.  Please read the reference information for the applicable rules.  [RFC5491|Geolocation Reference Information#rfc5491] is a good starting point.

h2. Conveying a Location via SIP
There are currently two ways to convey a location description regardless of which description method you use.  Both use the {{Geolocation}} SIP message header to indicate where to get the location description document.

h3. By Reference
This one's simple.  The "reference" is actually URI that the recipient can access that will return an XML document containing the description.  "http" and "https" are the most common URI schemes but there are others.  See [RFC6442|Geolocation Reference Information#rfc6442] above.  An example {{Geolocation}} header might look like: {{Geolocation: <https://geoloc.example.com?location=some_location_reference>}}.

With this method, you are entirely responsible for retrieving location descriptions from URIs you receive and for serving location descriptions for URIs you send.  Asterisk does not attempt to retrieve any information from those URIs.

When sending information to an upstream carrier, it's possible they may give _you_ special URIs to place in Geolocation headers you send them.

h3. By Value
This method involves sending or receiving a PIDF-LO document attached to a SIP message. For details on how this works generally, See [RFC6442|Geolocation Reference Information#rfc6442] and [RFC5491|Geolocation Reference Information#rfc5491].  An example {{Geolocation}} header might look like: {{Geolocation: <cid:gyytfr@your.pbx.com>}}.  The {{cid}} scheme indicates that the recipient should look in the SIP message body (or bodies since there could also be an SDP for example) for the location document.

h3. Multiple URIs
Technically, the {{Geolocation}} header can contain multiple URIs and they can be a mix of "by-reference" and "by-value".  The process of dealing with multiple location references is _very_ complex however and should be avoided.

h3. Geolocation-Routing
[RFC6442|Geolocation Reference Information#rfc6442] also defines the {{Geolocation-Routing}} header which indicates to a recipient that the location information may or may not be used for call routing purposes.  If set to "no" (the default if absent), the recipient MUST NOT use the location information for routing purposes.  If set to "yes", the recipient MAY use the location information for routing purposes and may also reset the value to "no" to prevent downstream systems from using the location information for routing.

Some carriers ignore this header altogether.

{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}
