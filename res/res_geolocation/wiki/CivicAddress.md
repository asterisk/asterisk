{section:border=false}
{column:width=70%}

h1. Introduction
For static locations, using Civic Address location descriptions would be the easiest method.  As stated earlier though, you and your partners must agree on which description formats are acceptable.

The following tables list the IANA registered element names that are currently accepted. The complete list of codes is defined in:
[https://www.iana.org/assignments/civic-address-types-registry/civic-address-types-registry.xhtml]

These codes were originally defined in [RFC4119|Geolocation Reference Information#rfc4119] and [RFC4776|Geolocation Reference Information#rfc4776]
|| Label || Description || Example |
| country | The country is identified by the two-letter ISO 3166 code.|US|
| A1 | national subdivisions (state, region, province, prefecture)|New York|
| A2 | county, parish, gun (JP), district (IN)|King's County|
| A3 | city, township, shi (JP)|New York|
| A4 | city division, borough, city, district, ward, chou (JP)|Manhattan|
| A5 | neighborhood, block | Morningside Heights |
| A6 | street\\NOTE: This code has been deprecated in favor of {{RD}}, defined below. | Broadway |
| PRD | Leading street direction| N, W |
| POD | Trailing street direction| SW |
| STS | Street suffix | Avenue, Platz, Street|
| HNO | House number, numeric part only|123|
| HNS | House number suffix | A, 1/2 |
| LMK | Landmark or vanity address|Low Library |
| LOC | Additional location information\\NOTE: {{ROOM}} was added below.| Room 543 |
| FLR | Floor | 5 |
| NAM | Name (residence, business or office occupant)|Joe's Barbershop |
| PC | Postal code | 10027-0401 |

These codes were added in [RFC5139|Geolocation Reference Information#rfc5139]

|| Label || Description || Example |
| BLD | Building (structure) | Hope Theatre |
| UNIT | Unit (apartment, suite) | 12a |
| ROOM | Room | 450F |
| PLC | Place-type | office |
| PCN | Postal community name | Leonia |
| POBOX | Post office box (P.O. box) | U40 |
| ADDCODE | Additional Code | 13203000003 |
| SEAT | Seat (desk, cubicle, workstation) | WS 181 |
| RD | Primary road or street | Broadway |
| RDSEC | Road section | 14 |
| RDBR | Road branch | Lane 7 |
| RDSUBBR | Road sub-branch | Alley 8 |
| PRM | Road pre-modifier | Old |
| POM | Road post-modifier | Service |

These codes were added in [RFC6848|Geolocation Reference Information#rfc6848]

|| Label || Description || Example |
|PN|Post number that is attributed to a lamp post or utility pole.|21344567|
|MP|Milepost: a marker indicating distance to or from a place (often a town)
May actually be expressed in "miles" or "kilometers".|237.4|
|STP|Street Type Prefix.|Boulevard|
|HNP|House Number Prefix.|Z|

h1. Example Configurations

h2. Simple Example 1
In geolocation.conf, we can define a location that describes a building and profiles for Bob and Alice that add floor and room.  We're assuming here that Bob's and Alice's phones don't send any location information themselves.
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

h1. PIDF-LO XML Examples

Here's what Alice's PIDF-LO would look like:
{code}
<?xml version="1.0" encoding="UTF-8"?>
<presence entity="pres:alice@example.com"
	xmlns="urn:ietf:params:xml:ns:pidf"
	xmlns:ca="urn:ietf:params:xml:ns:pidf:geopriv10:civicAddr"
	xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
	xmlns:gbp="urn:ietf:params:xml:ns:pidf:geopriv10:basicPolicy"
	xmlns:gml="http://www.opengis.net/gml"
	xmlns:gp="urn:ietf:params:xml:ns:pidf:geopriv10"
	xmlns:gs="http://www.opengis.net/pidflo/1.0">
	<dm:device>
		<gp:geopriv>
			<gp:location-info>
				<ca:civicAddress xml:lang="en-AU">
					<ca:country>US</ca:country>
					<ca:A1>New York</ca:A1>
					<ca:A3>New York</ca:A3>
					<ca:HNO>1633</ca:HNO>
					<ca:PRD>W</ca:PRD>
					<ca:RD>46th</ca:RD>
					<ca:STS>Street</ca:STS>
					<ca:PC>10222</ca:PC>
					<ca:FLR>4</ca:FLR>
					<ca:ROOM>4B20</ca:ROOM>
				</ca:civicAddress>
			</gp:location-info>
			<gp:usage-rules>
			</gp:usage-rules>
			<gp:method>manual</gp:method>
		</gp:geopriv>
		<dm:deviceID>mac:1234567890ab</dm:deviceID>
		<dm:timestamp>2022-04-22T20:57:29Z</dm:timestamp>
	</dm:device>
</presence>
{code}

Here's what Bob's PIDF-LO would look like:
{code}
<?xml version="1.0" encoding="UTF-8"?>
<presence entity="pres:bob@example.com"
	xmlns="urn:ietf:params:xml:ns:pidf"
	xmlns:ca="urn:ietf:params:xml:ns:pidf:geopriv10:civicAddr"
	xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
	xmlns:gbp="urn:ietf:params:xml:ns:pidf:geopriv10:basicPolicy"
	xmlns:gml="http://www.opengis.net/gml"
	xmlns:gp="urn:ietf:params:xml:ns:pidf:geopriv10"
	xmlns:gs="http://www.opengis.net/pidflo/1.0">
	<dm:device>
		<gp:geopriv>
			<gp:location-info>
				<ca:civicAddress xml:lang="en-AU">
					<ca:country>US</ca:country>
					<ca:A1>New York</ca:A1>
					<ca:A3>New York</ca:A3>
					<ca:HNO>1633</ca:HNO>
					<ca:PRD>W</ca:PRD>
					<ca:RD>46th</ca:RD>
					<ca:STS>Street</ca:STS>
					<ca:PC>10222</ca:PC>
					<ca:FLR>32</ca:FLR>
					<ca:ROOM>32A6</ca:ROOM>
				</ca:civicAddress>
			</gp:location-info>
			<gp:usage-rules>
			</gp:usage-rules>
			<gp:method>manual</gp:method>
		</gp:geopriv>
		<dm:deviceID>mac:1234567890ab</dm:deviceID>
		<dm:timestamp>2022-04-22T20:57:29Z</dm:timestamp>
	</dm:device>
</presence>
{code}

Note that the only civicAddress difference between the two are the {{FLR}} and {{ROOM}}.

{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}
