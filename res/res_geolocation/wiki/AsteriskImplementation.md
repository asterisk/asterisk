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
Some of the parameters in each object are actually lists of comma-separated name-value "sub-parameters".   For example, the {{location_info}} parameter in the Location object contains a list of sub-parameters that are specific to the location type.
{code}
location_info = shape=Circle, pos="39.12345 -105.98766", radius=100
{code}
Spaces around the equals signs and commas are ignored so you must double quote sub-parameter values with spaces or commas in them.

For readability, parameters that use sub-parameters can be split over more than one line.  For example:
{code}
location_info = country=US,A1="New York"
location_info = house_number=1633,PRD=W,street=46th
{code}
would be equivalent to:
{code}
location_info = country=US,A1="New York",house_number=1633,PRD=W,street=46th
{code}

h3. Variable substitution
Some of the parameters can contain references to channel variables and dialplan functions.  For example, you might have a URI location object that contains a reference to the {{EXTEN}} channel variable:
{code}
location_info = URI=http://some.example.com?key=${EXTEN}
{code}
When a call is processed that uses this location object, {{$\{EXTEN\}}} would be replaced with the channel's extension and would result in a URI such as {{http://some.example.com?key=1000}}.  You'd set up your web server to return a location document based on the value of "key".

You can also use dialplan functions such as {{CURL}} and {{ODBC_SQL}} to supply values just as you would in extensions.conf.

h2. Location
The Location object defines a discrete location or defines a template that can be used to define a discrete location on a per-call basis.
||Parameter||Required?||Uses Channel\\Variables?||Uses Sub\\Parameters?||Usage||
|type|yes|no|no|Must be "location"|
|format|yes|no|no|"civicAddress", "GML" or "URI" to indicate how the location is expressed.|
|method|no|no|no|If provided, it MUST be one of "GPS", "A-GPS", "Manual", "DHCP", "Triangulation", "Cell", "802.11"|
|location_source|no|no|no|If provided, it MUST be a fully qualified domain name.  IP addresses are specifically not allowed.
See [RFC8787|Geolocation Reference Information#rfc8787] for the exact definition of this parameter.|
|location_info|yes|yes|yes|The sub-parameters of location_info are dependent on the location's format:
* URI: A single {{URI}} sub-parameter with the URI.
Example: {{location_info = URI=http://some.example.com}}
See the [URI] page for more info.
* civicAddress: A set of sub-parameters that describe the location.
Example:
{code}
location_info = country=US,A1="New York",A3="New York"
location_info = house_number=1633,PRD=W,street=46th
location_info = street_suffix = Street,postal_code=10222
{code}
See the [Civic Address] page for more info.
* GML: A set of sub-parameters that describe the location.
Example: {{location_info = shape=Circle, pos="39.12345 -105.98766", radius=100}}
See the [GML] page for more info.|
|confidence|no|no|yes|This is a rarely used field in the specification that would indicate the confidence in the location specified.  See [RFC7459|https://www.rfc-editor.org/rfc/rfc7459] for exact details.
Sub-parameters:
* {{pdf}}: One of: "unknown", "normal", "rectangular".
* {{value}}: A percentage indicating the confidence.
|


h2. Profile
The Profile object defines how a location is used and is referenced by channel drivers.

||Parameter||Required?||Uses Channel\\Variables?||Uses Sub\\Parameters?||Usage||
|type|yes|no|no|Must be "profile"|
|location_reference|no|no|no|Specifies the id of a Location object to use.|
|pidf_element|no|no|no|For Civic Address and GML location formats, this parameter specifies the PIDF element that will carry the location description on outgoing SIP requests.  Must be one of "tuple", "device" or "person".  The default is "device".|
|allow_routing_use|no|no|no|This value controls the value of the {{Geolocation-Routing}} header sent on SIP requests,  Must be "yes" or "no".  The default is "no".
See [RFC6442|Geolocation Reference Information#rfc6442] for more information.|
|profile_precedence|no|no|no|Specifies which of the available profiles (configured or incoming) takes precedence.\\
NOTE: On an incoming call leg/channel, the "incoming" profile is the one received by the channel driver from the calling party in the SIP INVITE and the "configured" profile is the one attached to the calling party's pjsip endpoint.  On an outgoing call segment/channel, the "incoming" profile is the one received by the channel driver from the Asterisk core/dialplan and the "configured" profile one is the one attached to the called party's pjsip endpoint.
* {{prefer_incoming}}: Use the incoming profile if it exists and has location information, otherwise use the	configured profile if it has location information. If neither profile has location information, nothing is sent.
* {{force_incoming}}: Discard any configured profile and use the incoming profile if it exists and it has location information.  If the incoming profile doesn't exist or has no location information, nothing is sent.
* {{prefer_config}}: Use the configured profile if it exists and has location information, otherwise use the	incoming profile if it exists and has location information. If neither profile has location 							information, nothing is sent.
* {{force_config}}: Discard any incoming profile and use the configured profile if it exists and it has location information.  If the configured profile doesn't exist or has no location information, nothing is sent.
|
|usage_rules|no|yes|yes|For Civic Address and GML location formats, this parameter specifies the contents of the {{usage-rules}} PIDF-LO element.\\
* {{retransmission-allowed}}: Must be "yes" or "no".  The default is "no".\\
* {{retention-expires}}: An ISO-format timestamp after which the recipient MUST discard and location information associated with this request.  The default is 24 hours after the request was sent.  You can use dialplan functions to create a timestamp yourself if needed.  For example, to set the timestamp to 1 hour after the request is sent, use:
{{retention-expires="$\{STRFTIME($[$\{EPOCH\}+3600],UTC,%FT%TZ)\}"}}\\
See [RFC4119|Geolocation Reference Information#rfc4119] for the exact definition of this parameter.
|
|location_info_refinement|no|yes|yes|This parameter can be used to refine referenced location by adding these sub-parameters to the {{location_info}} parameter of the referenced location object.  For example, you could have Civic Address referenced object describe a building, then have this profile refine it by adding floor, room, etc.  Another profile could then also reference the same location object and refine it by adding a different floor, room, etc.
|location_variables|no|yes|yes|Any parameter than can use channel variables can also use the arbitrary variables defined in this parameter.  For example {{location_variables = MYVAR1=something, MYVAR2="something else"}} would allow you to use {{$\{MYVAR1\}}} and {{$\{MYVAR2\}}} in any other parameter that can accept channel variables|
|notes|no|no|no|The specifications allow a free-form "note-well" element to be added to the location description.  Any text entered here will be present on all outgoing Civic Address and GML requests.|

h1. chan_pjsip Configuration
Two new parameters have been added to pjsip endpoints:
||Parameter||Usage||
|geoloc_incoming_call_profile|Should be set to the name of a geolocation profile to use for calls coming into Asterisk from this remote endpoint.  If not set, no geolocation processing will occur and any location descriptions present on the incoming request will be silently dropped.|
|geoloc_outgoing_call_profile|Should be set to the name of a geolocation profile to use for calls Asterisk sends to this remote endpoint.  If not set, no geolocation processing will occur and any location descriptions coming from the associated incoming channel or the dialplan will be silently dropped and not conveyed to the endpoint.|

h1. Dialplan Applications and Functions
Two new dialplan applications and one dialplan function have been added to allow a dialplan author to manipulate geolocation information.

h2. GeolocProfileCreate
This application creates a new Geolocation profile on the channel in addition to any others that may already exist.  It tasks a profile name and an index as its arguments.  Callers must use the {{GEOLOC_PROFILE}} function to set its actual location description.

h2. GeolocProfileDelete
This application deletes the existing Geolocation profile at the specified index from the channel's list of profiles.

h2. GEOLOC_PROFILE
This function can get or set any of the fields in a specific profile.  The available fields are those in _both_ the Location and Profile configuration objects.

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
