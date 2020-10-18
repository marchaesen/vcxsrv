<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" 
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  >
<xsl:output method="text"
            encoding="UTF-8"
            doctype-system="xkb.dtd"
            indent="no"/>

<xsl:template match="modelList|optionList|name|description|shortDescription|configItem"/>
<xsl:strip-space elements="*"/>

<xsl:template match="layoutList"><xsl:apply-templates select="./layout"/></xsl:template>

<xsl:template match="variantList"><xsl:apply-templates select="./variant"/></xsl:template>

<xsl:template match="layout"><xsl:text>
</xsl:text><xsl:value-of select="./configItem/name"/>:"<xsl:value-of select="./configItem/description"/>"<xsl:apply-templates match="./variantList/variant"/></xsl:template>
  
<xsl:template match="variant"><xsl:text>
</xsl:text><xsl:value-of select="../../configItem/name"/>(<xsl:value-of select="./configItem/name"/>):"<xsl:value-of select="./configItem/description"/>"</xsl:template>

</xsl:stylesheet>
