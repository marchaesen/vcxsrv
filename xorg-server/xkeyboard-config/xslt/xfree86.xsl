<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" 
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  >
<xsl:output method="xml"
            encoding="UTF-8"
            doctype-system="xkb.dtd"
            indent="yes"/>
  
  <!-- Transform all "simple" elements as they are -->
  <xsl:template match="@*|xkbConfigRegistry|layout|layoutList|model|modelList|group|option|optionList|variant|variantList">
    <xsl:copy>
      <xsl:apply-templates select="@*|*"/>
    </xsl:copy>
  </xsl:template>

  <!-- Tricky business: configItem -->
  <xsl:template match="configItem">
    <configItem xsl:space="preserve">
      <name><xsl:value-of select="./name"/></name>
      <!-- If there are some shortDescriptions -->
      <xsl:if test="count(./shortDescription)!=0">
        <!-- First, put the non-translated version -->
        <shortDescription><xsl:value-of select="./shortDescription[not(@xml:lang)]"/></shortDescription>
        <!-- For all translated versions ... -->
        <xsl:for-each select="./shortDescription[@xml:lang]">
          <!-- ... which are different from non-translated one ... -->
          <xsl:if test="../shortDescription[not(@xml:lang)]/text() != ./text()">
            <!-- ... - output! -->
            <shortDescription xml:lang="{./@xml:lang}"><xsl:value-of select="./text()"/></shortDescription>
          </xsl:if>
        </xsl:for-each>
      </xsl:if>
      <!-- If there are some descriptions -->
      <xsl:if test="count(./description)!=0">
        <!-- First, put the non-translated version -->
        <description><xsl:value-of select="./description[not(@xml:lang)]"/></description>
        <!-- For all translated versions ... -->
        <xsl:for-each select="./description[@xml:lang]">
          <!-- ... which are different from non-translated one ... -->
          <xsl:if test="../description[not(@xml:lang)]/text() != ./text()">
            <!-- ... - output! -->
            <description xml:lang="{./@xml:lang}"><xsl:value-of select="./text()"/></description>
          </xsl:if>
        </xsl:for-each>
      </xsl:if>
    </configItem>
  </xsl:template>
  
</xsl:stylesheet>
