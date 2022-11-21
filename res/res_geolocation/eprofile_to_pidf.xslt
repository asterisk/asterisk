<?xml version="1.0"?>
<xsl:stylesheet version="1.1"
	xmlns:ca="urn:ietf:params:xml:ns:pidf:geopriv10:civicAddr"
	xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
	xmlns:fn="http://www.w3.org/2005/xpath-functions"
	xmlns:gbp="urn:ietf:params:xml:ns:pidf:geopriv10:basicPolicy"
	xmlns:gml="http://www.opengis.net/gml"
	xmlns:gp="urn:ietf:params:xml:ns:pidf:geopriv10"
	xmlns:gs="http://www.opengis.net/pidflo/1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns:con="urn:ietf:params:xml:ns:geopriv:conf"
	xmlns:date="http://exslt.org/dates-and-times">

	<xsl:output method="xml" indent="yes"/>
	<xsl:strip-space elements="*"/>
	<xsl:param name="suppress_empty_ca_elements" select="false()"/>

	<!-- REMINDER:  The "match" and "select" xpaths refer to the input document,
		not the output document -->

	<xsl:template match="presence">
		<!-- xslt will take care of adding all of the namespace declarations
			from the list above -->
		<presence xmlns="urn:ietf:params:xml:ns:pidf" entity="{@entity}">
			<xsl:apply-templates/>
		</presence>
	</xsl:template>

	<xsl:template match="person|device">
		<xsl:element name="dm:{local-name(.)}">
			<xsl:if test="@id">
				<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			</xsl:if>

			<gp:geopriv>
				<xsl:apply-templates select="./location-info"/>
				<xsl:apply-templates select="./usage-rules"/>
				<xsl:apply-templates select="./method"/>
				<xsl:apply-templates select="./note-well"/>
			</gp:geopriv>
			<xsl:if test="./timestamp">
				<dm:timestamp>
					<xsl:value-of select="./timestamp"/>
				</dm:timestamp>
			</xsl:if>
			<xsl:if test="./deviceID">
				<dm:deviceID>
					<xsl:value-of select="./deviceID"/>
				</dm:deviceID>
			</xsl:if>
		</xsl:element>
	</xsl:template>

	<xsl:template match="tuple">
		<xsl:element name="tuple" namespace="urn:ietf:params:xml:ns:pidf">
			<xsl:element name="status" namespace="urn:ietf:params:xml:ns:pidf">
				<gp:geopriv>
					<xsl:apply-templates select="./location-info"/>
					<xsl:apply-templates select="./usage-rules"/>
					<xsl:apply-templates select="./method"/>
					<xsl:apply-templates select="./note-well"/>
				</gp:geopriv>
			</xsl:element>
			<xsl:if test="./timestamp">
				<xsl:element name="timestamp" namespace="urn:ietf:params:xml:ns:pidf">
					<xsl:value-of select="./timestamp"/>
				</xsl:element>
			</xsl:if>
		</xsl:element>
	</xsl:template>


	<xsl:template match="location-info">
		<gp:location-info>
			<xsl:apply-templates/>
		</gp:location-info>
	</xsl:template>

	<!-- When we're using the civicAddress format, the translation is simple.
		We add gp:location-info and ca:civicAddress, then we just copy in
		each element, adding the "ca" namespace -->

	<xsl:template match="civicAddress/*">
		<xsl:if test="not($suppress_empty_ca_elements) or boolean(node())">
			<xsl:element name="ca:{name()}">
				<xsl:value-of select="."/>
			</xsl:element>
		</xsl:if>
	</xsl:template>

	<xsl:template match="location-info/civicAddress">
		<ca:civicAddress xml:lang="{@lang}">
			<xsl:apply-templates/>
		</ca:civicAddress>
	</xsl:template>

	<!-- All GML shapes share common processing for the "srsName" attribute -->
	<xsl:template name="shape">
		<xsl:choose>
			<xsl:when test="@crs = '3d'">
				<xsl:attribute name="srsName">urn:ogc:def:crs:EPSG::4979</xsl:attribute>
			</xsl:when>
			<xsl:otherwise>
				<xsl:attribute name="srsName">urn:ogc:def:crs:EPSG::4326</xsl:attribute>
			</xsl:otherwise>
		</xsl:choose>
	</xsl:template>

	<!-- The GML shapes themselves.  They don't all have the same namespace unfortunately... -->

	<xsl:template match="Point|Circle|Ellipse|ArcBand|Sphere|Ellipsoid">
		<xsl:variable name="namespace">
			<xsl:choose>
				<xsl:when test="name() = 'Point'">
					<xsl:value-of select="'gml'"/>
				</xsl:when>
				<xsl:otherwise>
					<xsl:value-of select="'gs'"/>
				</xsl:otherwise>
			</xsl:choose>
		</xsl:variable>

		<xsl:element name="{$namespace}:{name()}">
			<xsl:call-template name="shape"/>
			<xsl:apply-templates select="./*"/>
		</xsl:element>
	</xsl:template>

	<!-- ... and some are more complex than others. -->

	<xsl:template match="Polygon">
		<gml:Polygon>
			<xsl:call-template name="shape"/>
			<gml:exterior>
				<gml:LinearRing>
					<xsl:apply-templates select="./pos|posList"/>
				</gml:LinearRing>
			</gml:exterior>
		</gml:Polygon>
	</xsl:template>

	<!-- Prism with a Polygon and height -->
	<xsl:template match="Prism">
		<gs:Prism>
			<xsl:call-template name="shape"/>
			<gs:base>
				<gml:Polygon>
					<gml:exterior>
						<gml:LinearRing>
							<xsl:apply-templates select="./pos|posList"/>
						</gml:LinearRing>
					</gml:exterior>
				</gml:Polygon>
			</gs:base>
			<xsl:apply-templates select="./height"/>
		</gs:Prism>
	</xsl:template>

	<!-- method has no children so we add the "gp" namespace and copy in the value -->
	<xsl:template match="method">
		<gp:method>
			 <xsl:value-of select="."/>
		 </gp:method>
	</xsl:template>

	<!-- note-well has no children so we add the "gp" namespace and copy in the value -->
	<xsl:template match="note-well">
		<gp:note-well>
			 <xsl:value-of select="."/>
		 </gp:note-well>
	</xsl:template>

	<!-- usage-rules does have children so we add the "gp" namespace and copy in
		the children, also adding the "gp" namespace -->
	<xsl:template match="usage-rules">
		<gp:usage-rules>
			 <xsl:for-each select="*">
				 <xsl:element name="gp:{local-name()}">
					 <xsl:value-of select="."/>
				 </xsl:element>
			 </xsl:for-each>
		</gp:usage-rules>
	</xsl:template>

	<!-- These are the GML format primitives -->

	<xsl:template name="name-value">
		<xsl:element name="gml:{name()}">
			<xsl:value-of select="."/>
		</xsl:element>
	</xsl:template>

	<xsl:template name="length">
		<xsl:element name="gs:{name()}">
			<xsl:attribute name="uom">urn:ogc:def:uom:EPSG::9001</xsl:attribute>
			<xsl:value-of select="."/>
		</xsl:element>
	</xsl:template>

	<xsl:template name="angle">
		<xsl:element name="gs:{name()}">
			<xsl:choose>
				<xsl:when test="@uom = 'radians'">
					<xsl:attribute name="uom">urn:ogc:def:uom:EPSG::9102</xsl:attribute>
				</xsl:when>
				<xsl:otherwise>
					<xsl:attribute name="uom">urn:ogc:def:uom:EPSG::9101</xsl:attribute>
				</xsl:otherwise>
			</xsl:choose>
			<xsl:value-of select="."/>
		</xsl:element>
	</xsl:template>

	<!-- These are the GML shape parameters -->

	<xsl:template match="orientation"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="radius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="height"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="semiMajorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="semiMinorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="verticalAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="innerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="outerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="startAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="openingAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="pos"><xsl:call-template name="name-value" /></xsl:template>
	<xsl:template match="posList"><xsl:call-template name="name-value" /></xsl:template>

	<xsl:template match="confidence">
		<con:confidence pdf="{@pdf}">
			<xsl:value-of select="."/>
		</con:confidence>
	</xsl:template>

</xsl:stylesheet>
