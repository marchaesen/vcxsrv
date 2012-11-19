<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" 
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  >
<xsl:output method="text"/>

  <xsl:param name="type"/>
  <xsl:param name="parentId"/>
  
  <xsl:template match="xkbConfigRegistry">
    <xsl:apply-templates select=".//configItem[(name(..) = $type) and (../../../configItem/name = $parentId or ../../configItem/name = $parentId )]">
      <xsl:sort select="name"/>
    </xsl:apply-templates>
  </xsl:template>

  <xsl:template match="configItem">
    <xsl:value-of select="./name"/><xsl:text>
</xsl:text>
  </xsl:template>

</xsl:stylesheet>
