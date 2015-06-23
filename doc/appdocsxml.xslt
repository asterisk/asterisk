<xsl:stylesheet version="1.0"
 xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
    <xsl:output omit-xml-declaration="yes" indent="yes"/>

    <xsl:param name="pNewType" select="'myNewType'"/>

    <xsl:template match="node()|@*">
        <xsl:copy>
            <xsl:apply-templates select="node()|@*"/>
        </xsl:copy>
    </xsl:template>

    <xsl:template match="configOptionToEnum">
        <xsl:for-each select="configOption">
            <xsl:element name="enum">
                <xsl:attribute name="name">
                    <xsl:value-of select="@name"/>
                </xsl:attribute>
                <xsl:element name="para">
                    <xsl:value-of select="synopsis"/>
                </xsl:element>
            </xsl:element>
        </xsl:for-each>
    </xsl:template>

    <xsl:template match="channel_snapshot">
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Channel')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'ChannelState')"/>
            </xsl:attribute>
            <para>A numeric code for the channel's current state, related to <xsl:value-of select="concat(@prefix,'ChannelStateDesc')"/></para>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'ChannelStateDesc')"/>
            </xsl:attribute>
            <enumlist>
                <enum name="Down"/>
                <enum name="Rsrvd"/>
                <enum name="OffHook"/>
                <enum name="Dialing"/>
                <enum name="Ring"/>
                <enum name="Ringing"/>
                <enum name="Up"/>
                <enum name="Busy"/>
                <enum name="Dialing Offhook"/>
                <enum name="Pre-ring"/>
                <enum name="Unknown"/>
            </enumlist>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'CallerIDNum')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'CallerIDName')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'ConnectedLineNum')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'ConnectedLineName')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'AccountCode')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Context')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Exten')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Priority')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Uniqueid')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'Linkedid')"/>
            </xsl:attribute>
            <para>Uniqueid of the oldest channel associated with this channel.</para>
        </xsl:element>
    </xsl:template>

    <xsl:template match="bridge_snapshot">
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'BridgeUniqueid')"/>
            </xsl:attribute>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'BridgeType')"/>
            </xsl:attribute>
            <para>The type of bridge</para>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'BridgeTechnology')"/>
            </xsl:attribute>
            <para>Technology in use by the bridge</para>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix, 'BridgeCreator')"/>
            </xsl:attribute>
            <para>Entity that created the bridge if applicable</para>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix, 'BridgeName')"/>
            </xsl:attribute>
            <para>Name used to refer to the bridge by its BridgeCreator if applicable</para>
        </xsl:element>
        <xsl:element name="parameter">
            <xsl:attribute name="name">
                <xsl:value-of select="concat(@prefix,'BridgeNumChannels')"/>
            </xsl:attribute>
            <para>Number of channels in the bridge</para>
        </xsl:element>
    </xsl:template>
</xsl:stylesheet>
