<?xml version="1.0"?>
<xsl:stylesheet version="1.0"
	xmlns:ca="urn:ietf:params:xml:ns:pidf:geopriv10:civicAddr"
	xmlns:def="urn:ietf:params:xml:ns:pidf"
	xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
	xmlns:fn="http://www.w3.org/2005/xpath-functions"
	xmlns:gbp="urn:ietf:params:xml:ns:pidf:geopriv10:basicPolicy"
	xmlns:gml="http://www.opengis.net/gml"
	xmlns:gp="urn:ietf:params:xml:ns:pidf:geopriv10"
	xmlns:gs="http://www.opengis.net/pidflo/1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform">


<!--
	The whole purpose of this stylesheet is to convert a PIDF-LO document into a simple,
	common XML document that is easily parsable by geoloc_eprofile into an eprofile.

	For example:

	<presence>
		<device>
			<location-info format="GML">shape="Point", crs="2d", pos="38.456 -105.678"</location-info>
			<usage-rules>retransmission-allowed=no</usage-rules>
			<method>GPS</method>
		</device>
	</presence>

	WARNING:  Don't mess with this stylesheet before brushing up your
	XPath and XSLT expertise.
-->


<!--
	All of the namespaces that could be in the incoming PIDF-LO document
	have to be declared above.  All matching is done based on the URI, not
	the prefix so we can use whatever prefixes we want.  For instance,
	even if "urn:ietf:params:xml:ns:pidf:data-model" were declared with
	the "pdm" prefix in the incoming document and with "dm" here,
	"dm:device" would match "pdm:device" in the document.
-->

	<xsl:output method="xml" indent="yes"/>
	<xsl:strip-space elements="*"/>
	<xsl:param name="path"/>

	<!--
		Even though the "presence", "tuple", and "status" elements won't have namespaces in the
		incoming PIDF document, we have to use the pseudo-namespace "def" here because of namespace
		processing quirks in libxml2 and libxslt.

		We don't use namespace prefixes in the output document at all.
	-->
	<xsl:template match="/def:presence">
		<xsl:element name="presence">
			<xsl:attribute name="entity"><xsl:value-of select="@entity"/></xsl:attribute>
			<xsl:apply-templates select="$path"/>
		</xsl:element>
	</xsl:template>

	<xsl:template match="dm:device">
		<xsl:element name="device">
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:apply-templates select=".//gp:location-info"/>
			<xsl:apply-templates select=".//gp:usage-rules"/>
			<xsl:apply-templates select=".//gp:method"/>
			<xsl:apply-templates select=".//gp:note-well"/>
			<xsl:if test="./dm:timestamp">
				<timestamp>
					<xsl:value-of select="./dm:timestamp"/>
				</timestamp>
			</xsl:if>
			<xsl:if test="./dm:deviceID">
				<deviceID>
					<xsl:value-of select="./dm:deviceID"/>
				</deviceID>
			</xsl:if>
		</xsl:element>
	</xsl:template>

	<xsl:template match="def:tuple">
		<xsl:element name="tuple">
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:apply-templates select=".//gp:location-info"/>
			<xsl:apply-templates select=".//gp:usage-rules"/>
			<xsl:apply-templates select=".//gp:method"/>
			<xsl:apply-templates select=".//gp:note-well"/>
			<xsl:if test="./timestamp">
				<timestamp>
					<xsl:value-of select="./timestamp"/>
				</timestamp>
			</xsl:if>
		</xsl:element>
	</xsl:template>

	<xsl:template match="dm:person">
		<xsl:element name="person">
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:apply-templates select=".//gp:location-info"/>
			<xsl:apply-templates select=".//gp:usage-rules"/>
			<xsl:apply-templates select=".//gp:method"/>
			<xsl:apply-templates select=".//gp:note-well"/>
			<xsl:if test="./dm:timestamp">
				<timestamp>
					<xsl:value-of select="./dm:timestamp"/>
				</timestamp>
			</xsl:if>
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:location-info/gml:*">
		<xsl:element name="location-info">
			<xsl:attribute name="format">gml</xsl:attribute>
			<xsl:call-template name="shape" />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:location-info/gs:*">
		<xsl:element name="location-info">
			<xsl:attribute name="format">gml</xsl:attribute>
			<xsl:call-template name="shape" />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:location-info/ca:civicAddress">
		<xsl:element name="location-info">
			<xsl:attribute name="format">civicAddress</xsl:attribute>
			<xsl:call-template name="civicAddress" />
		</xsl:element>
	</xsl:template>

	<!--
		All of the "following-sibling" things just stick a comma after the value if there's another
		element after it.  The result should be...

		name1="value1", name2="value2"
	-->
	<xsl:template name="name-value">
		<xsl:element name="{local-name(.)}">
			<xsl:value-of select="normalize-space(.)"/>
		</xsl:element>
	</xsl:template>

	<xsl:template name="length"><xsl:call-template name="name-value" /></xsl:template>

	<xsl:template name="angle">
		<xsl:element name="{local-name(.)}">
			<xsl:choose>
				<xsl:when test="@uom = 'urn:ogc:def:uom:EPSG::9102'">
					<xsl:attribute name="uom">radians</xsl:attribute></xsl:when>
				<xsl:otherwise>
					<xsl:attribute name="uom">degrees</xsl:attribute></xsl:otherwise>
			</xsl:choose>
			<xsl:value-of select="normalize-space(.)"/>
		</xsl:element>
	</xsl:template>

	<xsl:template match="gs:orientation"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gs:radius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:height"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:semiMajorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:semiMinorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:verticalAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:innerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:outerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:startAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gs:openingAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gml:pos"><xsl:call-template name="name-value" /></xsl:template>
	<xsl:template match="gml:posList"><xsl:call-template name="name-value" /></xsl:template>

	<xsl:template name="shape">
		<xsl:element name="{local-name(.)}">
			<xsl:choose>
			<xsl:when test="@srsName = 'urn:ogc:def:crs:EPSG::4326'">
				<xsl:attribute name="srsName">2d</xsl:attribute>
			</xsl:when>
			<xsl:when test="@srsName = 'urn:ogc:def:crs:EPSG::4979'">
				<xsl:attribute name="srsName">3d</xsl:attribute>
			</xsl:when>
			<xsl:otherwise>
				<xsl:attribute name="srsName">unknown</xsl:attribute>
			</xsl:otherwise>
			</xsl:choose>
			<xsl:apply-templates />
		</xsl:element>
	</xsl:template>

	<xsl:template match="ca:civicAddress/*"><xsl:call-template name="name-value" /></xsl:template>
	<xsl:template name="civicAddress">
		<xsl:element name="{local-name(.)}">
			<xsl:attribute name="lang"><xsl:value-of select="@xml:lang"/></xsl:attribute>
			<xsl:apply-templates select="./*"/>
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:usage-rules/*">
		<xsl:call-template name="name-value" />
	</xsl:template>

	<xsl:template match="gp:usage-rules">
		<xsl:element name="usage-rules">
			<xsl:apply-templates />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:method">
		<xsl:element name="method">
		<xsl:value-of select="normalize-space(.)" />
		</xsl:element>
	</xsl:template>


</xsl:stylesheet>
