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
	xmlns:con="urn:ietf:params:xml:ns:geopriv:conf"
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

	<!--
		Even though the "presence", "tuple", and "status" elements won't have namespaces in the
		incoming PIDF document, we have to use the pseudo-namespace "def" here because of namespace
		processing quirks in libxml2 and libxslt.  We don't use namespace prefixes in the output
		document at all.
	-->
	<xsl:template match="/def:presence">
		<xsl:element name="presence">
			<xsl:attribute name="entity"><xsl:value-of select="@entity"/></xsl:attribute>
			<!--
				We only want devices, tuples and persons (in that order) that
				have location-info elements.
			 -->
			<xsl:apply-templates select="dm:device[./gp:geopriv/gp:location-info]"/>
			<xsl:apply-templates select="def:tuple[./def:status/gp:geopriv/gp:location-info]"/>
			<xsl:apply-templates select="dm:person[.//gp:geopriv/gp:location-info]"/>
		</xsl:element>
	</xsl:template>

	<xsl:template name="geopriv">
			<xsl:apply-templates select=".//gp:geopriv/gp:location-info"/>
			<xsl:apply-templates select=".//gp:geopriv/gp:usage-rules"/>
			<xsl:apply-templates select=".//gp:geopriv/gp:method"/>
			<xsl:apply-templates select=".//gp:geopriv/gp:note-well"/>
	</xsl:template>

	<xsl:template match="def:tuple">
		<xsl:element name="tuple">
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:call-template name="geopriv"/>
			<xsl:apply-templates select="./def:timestamp"/>
		</xsl:element>
	</xsl:template>

	<xsl:template match="dm:device|dm:person">
		<xsl:element name="{local-name(.)}">
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:call-template name="geopriv"/>
			<xsl:apply-templates select="./dm:timestamp"/>
			<!-- deviceID should only apply to devices -->
			<xsl:if test="./dm:deviceID">
				<deviceID>
					<xsl:value-of select="./dm:deviceID"/>
				</deviceID>
			</xsl:if>
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:geopriv/gp:location-info">
		<xsl:element name="location-info">
			<xsl:choose>
				<xsl:when test="ca:civicAddress">
					<xsl:attribute name="format">civicAddress</xsl:attribute>
				</xsl:when>
				<xsl:when test="gml:*">
					<xsl:attribute name="format">gml</xsl:attribute>
				</xsl:when>
				<xsl:when test="gs:*">
					<xsl:attribute name="format">gml</xsl:attribute>
				</xsl:when>
			</xsl:choose>
			<xsl:apply-templates/>  <!-- Down we go! -->
		</xsl:element>
	</xsl:template>

	<!-- Civic Address -->
	<xsl:template match="gp:location-info/ca:civicAddress">
		<xsl:element name="civicAddress">
			<xsl:attribute name="lang"><xsl:value-of select="@xml:lang"/></xsl:attribute>
			<!-- The for-each seems to be slightly faster than applying another template -->
			<xsl:for-each select="./*">
				<xsl:call-template name="name-value" />
			</xsl:for-each>
		</xsl:element>
	</xsl:template>

	<!-- End of Civic Address.  Back up to location-info. -->

	<!-- The GML shapes:  gml:Point, gs:Circle, etc. -->
	<xsl:template match="gp:location-info/gml:*|gp:location-info/gs:*">
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
			<xsl:apply-templates />  <!-- Down we go! -->
		</xsl:element>
	</xsl:template>

	<!-- The supported GML attributes -->
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

	<!-- The GML attribute types -->
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

	<!-- End of GML.  Back up to location-info -->

	<xsl:template match="gp:location-info/con:confidence">
		<xsl:element name="{local-name(.)}">
			<xsl:attribute name="pdf"><xsl:value-of select="@pdf"/></xsl:attribute>
			<xsl:value-of select="normalize-space(.)" />
		</xsl:element>
	</xsl:template>

	<!-- End of location-info.  Back up to geopriv -->

	<xsl:template match="gp:geopriv/gp:usage-rules">
		<xsl:element name="usage-rules">
			<xsl:for-each select="./*">
				<xsl:call-template name="name-value" />
			</xsl:for-each>
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:geopriv/gp:method">
		<xsl:call-template name="name-value" />
	</xsl:template>

	<xsl:template match="gp:geopriv/gp:note-well">
		<xsl:element name="note-well">
			<xsl:value-of select="." />
		</xsl:element>
	</xsl:template>

	<!-- End of geopriv.  Back up to device/tuple/person -->

	<xsl:template match="def:timestamp|dm:timestamp">
		<xsl:call-template name="name-value" />
	</xsl:template>


</xsl:stylesheet>
