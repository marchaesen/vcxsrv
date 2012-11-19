package xkbTestFunc;

use strict;
use warnings;

our $VERSION='1.00';

our $origXkbRules;
our $origXkbModel;
our $origXkbLayouts;
our $origXkbOptions;
our $origXkbVariants;

sub backupXkbSettings
{
  ( $origXkbRules, $origXkbModel, $origXkbLayouts, $origXkbVariants, $origXkbOptions ) = getXkbSettings();
}

sub getXkbSettings
{
  my ( $xkbRules, $xkbModel, $xkbLayouts, $xkbVariants, $xkbOptions );

  open (XPROP, "xprop -root |") or die "Could not start xprop";
  PROP: while (<XPROP>)
  {
    if (/_XKB_RULES_NAMES\(STRING\) = \"(.*)\", \"(.*)\", \"(.*)\", \"(.*)\", \"(.*)\"/)
    {
      ( $xkbRules, $xkbModel, $xkbLayouts, $xkbVariants, $xkbOptions ) = 
      ( $1, $2, $3, $4, $5 ) ;
      last PROP;
    }
  }
  close XPROP;
  
  return ( $xkbRules, $xkbModel, $xkbLayouts, $xkbVariants, $xkbOptions );
}

sub setXkbSettings
{
  my ( $xkbRules, $xkbModel, $xkbLayouts, $xkbVariants, $xkbOptions ) = @_;
  my $outfile = ".test.out.xkb";
  ( system ( "setxkbmap -rules \"$xkbRules\" " .
             "-model \"$xkbModel\" " .
             "-layout \"$xkbLayouts\" " .
             "-variant \"$xkbVariants\" " .
             "-option \"$xkbOptions\" " .
             "-print | xkbcomp - -xkb $outfile" ) == 0 ) or die "Could not set xkb configuration";
  unlink($outfile);
}

sub restoreXkbSettings
{
  setXkbSettings( $origXkbRules, $origXkbModel, $origXkbLayouts, $origXkbVariants, $origXkbOptions );
}

sub defaultXkbSettings
{
  return ( "base", "pc105", "us", "", "" );
}

sub dumpXkbSettings
{
  my ( $xkbRules, $xkbModel, $xkbLayouts, $xkbVariants, $xkbOptions ) = @_;
  print "rules: [$xkbRules]\n" ;
  print "model: [$xkbModel]\n" ;
  print "layouts: [$xkbLayouts]\n" ;
  print "variants: [$xkbVariants]\n" ;
  print "options: [$xkbOptions]\n" ;
}

sub dumpXkbSettingsBackup
{
  dumpXkbSettings( $origXkbRules, $origXkbModel, $origXkbLayouts, $origXkbVariants, $origXkbOptions );
}

sub testLevel1
{
  my ( $type, $idx ) = @_;

  open ( XSLTPROC, "xsltproc --stringparam type $type listCIs.xsl ../rules/base.xml.in |" ) or
    die ( "Could not start xsltproc" );
  while (<XSLTPROC>)
  {
    chomp();
    if (/(\S+)/)
    {
      my $paramValue=$1;
      print "--- setting $type: [$paramValue]\n";
      my @params = defaultXkbSettings();
      $params[$idx] = $paramValue;
      dumpXkbSettings ( @params );
      setXkbSettings ( @params );
      #print "--- dump:\n";
      #dumpXkbSettings( getXkbSettings() );
    }
  }
  close XSLTPROC;
}

sub testLevel2
{
  my ( $type, $subtype, $idx, $delim1, $delim2, $ifCheckLevel1, $ifAddLevel1, $ifResetToDefault ) = @_;

  open ( XSLTPROC, "xsltproc --stringparam type $type listCIs.xsl ../rules/base.xml.in |" ) or
    die ( "Could not start xsltproc" );
  while (<XSLTPROC>)
  {
    chomp();
    if (/(\S+)/)
    {
      my $paramValue=$1;
      print "--- scanning $type: [$paramValue]\n";

      if ( $ifCheckLevel1 )
      {
        my @params = defaultXkbSettings();
        if ( $ifResetToDefault )
        {
          setXkbSettings ( @params );
        }
        $params[$idx] = "$paramValue";
        dumpXkbSettings ( @params );
        setXkbSettings ( @params );
        #print "--- dump:\n";
        #dumpXkbSettings( getXkbSettings() );
      }

      open ( XSLTPROC2, "xsltproc --stringparam type $subtype --stringparam parentId $paramValue listCI2.xsl ../rules/base.xml.in |" ) or
        die ( "Could not start xsltproc" );
      while (<XSLTPROC2>)
      {
        chomp();
        if (/(\S+)/)
        {
          my $paramValue2=$1;
          print "  --- $subtype: [$paramValue2]\n";
          my @params = defaultXkbSettings();
          if ( $ifResetToDefault )
          {
            setXkbSettings ( @params );
          }
          if ( $ifAddLevel1 )
          {
            $params[$idx] = "$paramValue$delim1$paramValue2$delim2";
          }
          else
          {
            $params[$idx] = "$paramValue2";
          }
          dumpXkbSettings ( @params );
          setXkbSettings ( @params );
          #print "--- dump:\n";
          #dumpXkbSettings( getXkbSettings() );
        }
      }
      close XSLTPROC2;
    }
  }
  close XSLTPROC;
}

1;
__END__

No docs yet
