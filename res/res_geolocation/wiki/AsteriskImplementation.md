{section:border=false}
{column:width=70%}

h1. Introduction

The Geolocation capabilities are implemented in Asterisk with the res_geolocation and res_pjsip_geolocation modules and the geolocation.conf configuration file.  There are also dialplan functions which allow you to manipulate location information as it's passed through the dialplan.

h1. Location Information Flow

Location information can be supplied to Asterisk from several sources during the call flow...
* Sent by a caller in a SIP INVITE message.
* Provided by a geolocation profile attached to the caller's endpoint.
* Provided by the dialplan via the Geolocation apps and functions.
* Provided by a geolocation profile attached to the callee's endpoint.

These sources aren't mutually exclusive and may, in fact, provide conflicting information or present the same information in multiple formats.  Given that, there's no way for Asterisk to merge information nor is there a way for Asterisk to automatically determine which source should take precedence.  However, you can use the geolocation profiles and the dialplan functions to tell Asterisk what to do with the location information received from the previous step in the call flow.

h1. Core Configuration
The bulk of the geolocation support is implemented in the res_geolocation module and configured in the geolocation.conf file.  The file contains two main objects, Location and Profile.

h2. Common Behavior

h3. Sub-parameters
Some of the parameters in each object are actually lists of comma-separated name-value "sub-parameters".   For example, the {{location_info}} parameter in the Location object contains a list of sub-parameters that are specific to the location type.  For instance, a GML Circle might look like this:
{code}
location_info = shape=Circle, pos="39.12345 -105.98766", radius=100
{code}
Spaces around the equals signs and commas are ignored so you must double quote sub-parameter values with spaces or commas in them.

For readability, parameters that use sub-parameters can be split over more than one line.  For example:
{code}
location_info = country=US,A1="New York"
location_info = HNO=1633,PRD=W,RD=46th
{code}
would be equivalent to:
{code}
location_info = country=US,A1="New York",HNO=1633,PRD=W,RD=46th
{code}

h3. Variable substitution
Some of the parameters can contain references to channel variables and dialplan functions.  For example, you might have a URI location object that contains a reference to the {{EXTEN}} channel variable:
{code}
location_info = URI=http://some.example.com?key=${EXTEN}
{code}
When a call is processed that uses this location object, {{$\{EXTEN\}}} would be replaced with the channel's extension and would result in a URI such as {{http://some.example.com?key=1000}}.  You'd set up your web server to return a location document based on the value of "key".

You can also use dialplan functions such as {{CURL}} and {{ODBC_SQL}} to supply values just as you would in extensions.conf.

h2. Configuration Objects
h3. Location
The Location object defines a discrete location or defines a template that can be used to define a discrete location on a per-call basis.

h4. Parameters

* *type*: Object type. Must be "location"
** Required: yes
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{type=location}}

* *format*: Must be one of "civicAddress", "GML" or "URI" to indicate how the location is expressed.
** Required: yes
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{format=civicAddress}}

* *method*: If provided, it MUST be one of "GPS", "A-GPS", "Manual", "DHCP", "Triangulation", "Cell", "802.11"
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{method=Manual}}

* *location_source*: If provided, it MUST be a fully qualified domain name.  IP addresses are specifically not allowed. See [RFC8787|Geolocation Reference Information#rfc8787] for the exact definition of this parameter.
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{location_source=some.domain.net}}

* *location_info*: Sub-parameters that describe the location.  Dependent on the format selected.
** Required: yes
** Uses channel variables: yes
** Sub-parameters: yes
** Default: none
** Examples:
*** [URI] format: (see the [URI] page for more info)
	{{location_info = URI=http://some.example.com}}
*** [civicAddress|Civic Address] format: ( See the [civicAddress|Civic Address] page for more info)
	{{location_info = country=US, A1="New York", A3="New York", ...}}
*** [GML|Geography Markup Language] format: (See the [GML|Geography Markup Language] page for more info)
	{{location_info = shape=Circle, pos="39.12345 -105.98766", radius=100}}

* *confidence*: This is a rarely used field in the specification that would indicate the confidence in the location specified.  See [RFC7459|https://www.rfc-editor.org/rfc/rfc7459] for exact details.
** Required: no
** Uses channel variables: no
** Sub-parameters: yes
*** *pdf*: One of: "unknown", "normal", "rectangular".
*** *value*: 0-100 percent indicating the confidence level.
** Default: none
** Example:
	{{confidence = pdf=normal, confidence=95

h4. Example

{code}
[mylocation]
type = location
format = civicAddress
method = Manual
location_info = country=US, A1="New York", A3="New York",
location_info = HNO=1633, PRD=W, RD=46th, STS=Street, PC=10222
{code}


h3. Profile
The Profile object defines how a location is used and is referenced by channel drivers.

h4. Parameters

* *type*: Object type. Must be "profile"
** Required: yes
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{type=profile}}

* *location_reference*: Specifies the id of a Location object to use.
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: none
** Example:
	{{location_reference=mylocation}}

* *pidf_element*: For Civic Address and GML location formats, this parameter specifies the PIDF element that will carry the location description on outgoing SIP requests.  Must be one of "tuple", "device" or "person".
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: device
** Example:
	{{pidf_element = tuple}}

* *allow_routing_use*: This value controls the value of the {{Geolocation-Routing}} header sent on SIP requests,  Must be "yes" or "no".  See [RFC6442|Geolocation Reference Information#rfc6442] for more information.
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: no
** Example:
	{{allow_routing_use = yes}}

* *profile_precedence*: Specifies which of the available profiles (configured or incoming) takes precedence. NOTE: On an incoming call leg/channel, the "incoming" profile is the one received by the channel driver from the calling party in the SIP INVITE and the "configured" profile is the one attached to the calling party's pjsip endpoint.  On an outgoing call segment/channel, the "incoming" profile is the one received by the channel driver from the Asterisk core/dialplan and the "configured" profile one is the one attached to the called party's pjsip endpoint.
** Valid values:
*** {{prefer_incoming}}: Use the incoming profile if it exists and has location information, otherwise use the	configured profile if it has location information. If neither profile has location information, nothing is passed on.
*** {{prefer_config}}: Use the configured profile if it exists and has location information, otherwise use the incoming profile if it has location information. If neither profile has location information, nothing is passed on.
*** {{discard_incoming}}: Discard the incoming profile and use the configured profile if it has location information. If it doesn't, nothing is passed on.
*** {{discard_config}}: Discard the configured profile and use the incoming profile if it has location information. If it doesn't, nothing is passed on.
** Required: no
** Uses channel variables: no
** Sub-parameters: none
** Default: discard_incoming
** Example:
	{{profile_precedence = prefer_incoming}}

* *usage_rules*: For Civic Address and GML location formats, this parameter specifies the contents of the {{usage-rules}} PIDF-LO element. See [RFC4119|Geolocation Reference Information#rfc4119] for the exact definition of this parameter.
** Required: no
** Uses channel variables: yes
** Sub-parameters: yes
*** *retransmission-allowed*: Must be "yes" or "no".
*** *retention-expires*: An ISO-format timestamp after which the recipient MUST discard and location information associated with this request.  The default is 24 hours after the request was sent.  You can use dialplan functions to create a timestamp yourself if needed.
** Default: retransmission-allowed=no, retention-expires=<current time + 24 hours>
** Example:
	{{usage_rules = retransmission-allowed=yes,retention-expires="$\{STRFTIME($[$\{EPOCH\}+3600],UTC,%FT%TZ)\}"}}

* *suppress_empty_ca_elements*: For Civic Address outgoing PIDF-LO documents, don't output empty elements.  This can be useful when you dynamically set values of elements in the dialplan that could evaluate to an empty string.  For instance, if you set the street suffix STS element from a dialplan variable and it happens to be empty, the default behavior would be to send an empty {{<STS/>}} element.  If this parameter is set to "yes" however, we'd just not print the element at all.
** Required: no
** Uses channel variables: no
** Sub-parameters: no
** Default: no
** Example
	{{suppress_empty_ca_elements = yes}}

* *location_info_refinement*: This parameter can be used to refine referenced location by adding these sub-parameters to the {{location_info}} parameter of the referenced location object.  For example, you could have Civic Address referenced object describe a building, then have this profile refine it by adding floor, room, etc.  Another profile could then also reference the same location object and refine it by adding a different floor, room, etc.
** Required: no
** Uses channel variables: yes
** Sub-parameters: yes (any that can appear in a location's location_info parameter)
** Default: none
** Example:
	Add a room to the civicAddress specified by location_reference.
	{{location_reference = myCivicAddress = ROOM=23A4}}
	{{location_info_refinement = ROOM=23A4}}

* *location_variables*: Any parameter than can use channel variables can also use the arbitrary variables defined in this parameter.  For example {{location_variables = MYVAR1=something, MYVAR2="something else"}} would allow you to use {{$\{MYVAR1\}}} and {{$\{MYVAR2\}}} in any other parameter that can accept channel variables.
** Required: no
** Uses channel variables: yes
** Sub-parameters: yes (one or more name=value pairs)
** Default: none
** Example:
	{{location_variables = MYVAR1=something, MYVAR2="something else"}}

* *notes*: The specifications allow a free-form "note-well" element to be added to the location description.  Any text entered here will be present on all outgoing Civic Address and GML requests.
** Required: no
** Uses channel variables: no
** Sub-parameters: no
** Default: none
** Example:
	{{notes = "anything you want"}}

h4. Additional Parameters
In addition to the profile-specific parameters defined above, any location-object parameters can be specified as well.  This is a convenient shortcut if you have a 1<>1 relationship between profile and location.

h4. Built-in Profiles
In addition to the profiles you define in geolocation.conf, 4 built-in profiles are also available They're named after their profile_precedence setting:
* *<prefer_incoming>*
* *<prefer_config>*
* *<discard_incoming>*
* *<discard_config>*

The rest of the profile parameters are set to their defaults.

h1. chan_pjsip Configuration
Two new parameters have been added to pjsip endpoints:

h2. Parameters

* *geoloc_incoming_call_profile*: Should be set to the name of a geolocation profile to use for calls coming into Asterisk from this remote endpoint.  If not set, no geolocation processing will occur and any location descriptions present on the incoming request will be silently dropped.  Any of the 4 built-in profiles can be used.

* *geoloc_outgoing_call_profile*: Should be set to the name of a geolocation profile to use for calls Asterisk sends to this remote endpoint.  If not set, no geolocation processing will occur and any location descriptions coming from the associated incoming channel or the dialplan will be silently dropped and not conveyed to the endpoint. Any of the 4 built-in profiles can be used.

Example:
{code}
[myendpoint]
type = endpoint
...
geoloc_incoming_call_profile = <discard_incoming>
geoloc_outgoing_call_profile = myendpoint_profile
{code}

h1. Dialplan Function
A new dialplan function has been added to allow a dialplan author to manipulate geolocation information.

h2. GEOLOC_PROFILE
This function can get or set any of the fields in a specific profile.  The available fields are those in _both_ the Location and Profile configuration objects.  See the fuinction help for more information.

h1. Example Call Flows

h2. Simple Example 1
Alice and Bob work in the same building so in geolocation.conf, we can define a location that describes the building and profiles for Bob and Alice that add floor and room.  We're assuming here that Bob's and Alice's phones don't send any location information themselves.
{code}
[building1]
type = location
format = civicAddress
location_info = country=US, A1="New York", A3="New York",
location_info = HNO=1633, PRD=W, RD=46th, STS=Street, PC=10222
method = Manual

[alice]
type = profile
location_reference = building1
location_refinement = FLR=4, ROOM=4B20

[bob]
type = profile
location_reference = building1
location_refinement = FLR=32, ROOM=32A6
{code}

In pjsip.conf, we can now associate those profiles to endpoints.
{code}
[bob]
type = endpoint
geoloc_incoming_call_profile = bob

[alice]
type = endpoint
geoloc_incoming_call_profile = alice
{code}
You'll notice that neither bob nor alice set {{geoloc_outgoing_call_profile}} because we never want to send location information _to_ them.

Now when Alice makes a call, Asterisk will construct an effective profile (including any defaults and variable substitutions) that looks like this...
{code}
format = civicAddress
location_info = country=US, A1="New York", A3="New York",
location_info = HNO=1633, RD=46th, STS=Street, PC=10222, FLR=4, ROOM=4B20
method = Manual
usage_rules = retransmission-allowed=no
usage_rules = retention-expires="${STRFTIME($[${EPOCH}+86400],UTC,%FT%TZ)}"
allow_routing = no
pidf_element = device
{code}

Bob's effective profile would be exactly the same except for {{FLR}} and {{ROOM}}

This effective profile will then be forwarded to the dialplan.  The dialplan application can then use GEOLOC_PROFILE to make changes before the effective profile is forwarded to the outgoing channel.  It can also use GeolocProfileDelete to just delete the effective profile and pass nothing.

{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}
