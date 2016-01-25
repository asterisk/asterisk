<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" omit-xml-declaration="yes" indent="no"/>
<xsl:template match="/">
<xsl:variable name="cats" select="'ADDONS APPS BRIDGES CDR CEL CHANNELS CODECS FORMATS FUNCS PBX RES TESTS'"/>
<xsl:text disable-output-escaping="yes">

%:
	@echo -e '\t"$@.so",'

header:
	@echo "/* include/asterisk/module_loadorder.h.  Generated from make */"
	@echo "/* This file must be included only by main/loader.c */"
	@echo ""
	@echo "#ifndef MODULE_LOADORDER_H"
	@echo "#define MODULE_LOADORDER_H"
	@echo ""
	@echo "static const char *module_loadorder[] = {"

footer:
	@echo '};'
	@echo "#endif"

</xsl:text>

<xsl:for-each select="menu/category[contains($cats, substring(@name, 12))]/member">
<xsl:value-of select="@name"/>:<xsl:for-each select="depend[/menu/category[contains(@name, 'CFLAGS') != 1]/member/@name = .]"><xsl:text> </xsl:text><xsl:value-of select="."/></xsl:for-each>
<xsl:for-each select="use[/menu/category[contains(@name, 'CFLAGS') != 1]/member/@name = .]"><xsl:text> </xsl:text><xsl:value-of select="."/></xsl:for-each>
<xsl:text>
</xsl:text>
</xsl:for-each>
include menuselect.makeopts

ALLMODS = <xsl:for-each select="menu/category[contains($cats, substring(@name, 12))]/member"> <xsl:value-of select="@name"/><xsl:text> </xsl:text></xsl:for-each>

NOTNEEDED = $(foreach cat,<xsl:value-of select="$cats"/>,$(MENUSELECT_$(cat)))

all: header $(filter-out $(NOTNEEDED),$(ALLMODS)) footer
	@:
</xsl:template>
</xsl:stylesheet>