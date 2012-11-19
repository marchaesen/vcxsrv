<?xml version="1.0" encoding="UTF-8"?>

<xsl:stylesheet
	version="1.0"
	xmlns:check="http://check.sourceforge.net/ns"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns="http://www.w3.org/TR/REC-html40">

<xsl:output method="html"/>

<xsl:template match="/">
<html>
 <head>
  <title>Test Suite Results</title>
 </head>

 <body>
  <xsl:apply-templates/>
 </body>
</html>
</xsl:template>

<xsl:template match="datetime">
 <xsl:apply-templates/>
</xsl:template>

<xsl:template match="duration">
 <xsl:apply-templates/>
</xsl:template>

<xsl:template match="check:suite">
 <xsl:apply-templates select="check:title"/>
 <center>
 <table width="80%" border="1">
  <thead>
   <tr>
    <td>Test Path</td>
    <td>Test Function Location</td>
    <td>C Identifier</td>
    <td>Test Case</td>
    <td>Result</td>
   </tr>
  </thead>
  <tbody>
   <xsl:apply-templates select="check:test"/>
  </tbody>
 </table>
 </center>
</xsl:template>

<xsl:template match="check:testsuites">
 <xsl:apply-templates select="check:suite"/>
 <h3>Unit Test Statistics</h3>
 <ul>
  <li>date/time: <xsl:apply-templates select="check:datetime"/></li>
  <li>duration: <xsl:apply-templates select="check:duration"/></li>
 </ul>
 <hr></hr>
</xsl:template>

<xsl:template match="check:title">
 <h2>Test Suite: <xsl:apply-templates/></h2>
</xsl:template>

<xsl:template match="check:test[@result='success']">
 <tr bgcolor="lime">
  <xsl:apply-templates/>
 </tr>
</xsl:template>

<xsl:template match="check:test[@result='failure']">
 <tr bgcolor="red">
  <xsl:apply-templates/>
 </tr>
</xsl:template>

<xsl:template match="check:test[@result='error']">
 <tr bgcolor="yellow">
  <xsl:apply-templates/>
 </tr>
</xsl:template>

<xsl:template match="check:path">
 <td><xsl:apply-templates/></td>
</xsl:template>

<xsl:template match="check:fn">
 <td><xsl:apply-templates/></td>
</xsl:template>

<xsl:template match="check:id">
 <td><xsl:apply-templates/></td>
</xsl:template>

<xsl:template match="check:description">
 <td><xsl:apply-templates/></td>
</xsl:template>

<xsl:template match="check:message">
 <td><xsl:apply-templates/></td>
</xsl:template>

</xsl:stylesheet>

