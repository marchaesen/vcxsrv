#!/usr/bin/perl -plw
use strict;

BEGIN {
	%::const = map { $_ => 1 } (
		# constants in xcb.h
		"XCBNone",
		"XCBCopyFromParent",
		"XCBCurrentTime",
		"XCBNoSymbol",
		"XCBError",
		"XCBReply",
		# renamed constants
		"XCBButtonAny",
		"XCBButton1",
		"XCBButton2",
		"XCBButton3",
		"XCBButton4",
		"XCBButton5",
		"XCBHostInsert",
		"XCBHostDelete",
		"XCBGlxGC_GL_CURRENT_BIT",
		"XCBGlxGC_GL_POINT_BIT",
		"XCBGlxGC_GL_LINE_BIT",
		"XCBGlxGC_GL_POLYGON_BIT",
		"XCBGlxGC_GL_POLYGON_STIPPLE_BIT",
		"XCBGlxGC_GL_PIXEL_MODE_BIT",
		"XCBGlxGC_GL_LIGHTING_BIT",
		"XCBGlxGC_GL_FOG_BIT",
		"XCBGlxGC_GL_DEPTH_BUFFER_BIT",
		"XCBGlxGC_GL_ACCUM_BUFFER_BIT",
		"XCBGlxGC_GL_STENCIL_BUFFER_BIT",
		"XCBGlxGC_GL_VIEWPORT_BIT",
		"XCBGlxGC_GL_TRANSFORM_BIT",
		"XCBGlxGC_GL_ENABLE_BIT",
		"XCBGlxGC_GL_COLOR_BUFFER_BIT",
		"XCBGlxGC_GL_HINT_BIT",
		"XCBGlxGC_GL_EVAL_BIT",
		"XCBGlxGC_GL_LIST_BIT",
		"XCBGlxGC_GL_TEXTURE_BIT",
		"XCBGlxGC_GL_SCISSOR_BIT",
		"XCBGlxGC_GL_ALL_ATTRIB_BITS",
		"XCBGlxRM_GL_RENDER",
		"XCBGlxRM_GL_FEEDBACK",
		"XCBGlxRM_GL_SELECT",
	);
	open(CONST, shift) or die "failed to open constants list: $!";
	while(<CONST>)
	{
		chomp;
		die "invalid constant name: \"$_\"" unless /^XCB[A-Za-z0-9_]*$/;
		$::const{$_} = 1;
	}
	close(CONST);
}

sub convert($$)
{
	local $_ = shift;
	my ($fun) = @_;

	return "xcb_generate_id" if /^xcb_[a-z0-9_]+_new$/ or /^XCB[A-Z0-9]+New$/;
	return "uint$1_t" if /^CARD(8|16|32)$/;
	return "int$1_t" if /^INT(8|16|32)$/;
	return "uint8_t" if $_ eq 'BOOL' or $_ eq 'BYTE';
	return $_ if /^[A-Z0-9]*_[A-Z0-9_]*$/ or !/^XCB(.+)/;
	my $const = defined $::const{$_};
	$_ = $1;

	s/^(GX|RandR|XFixes|XP|XvMC|ScreenSaver)(.)/uc($1) . "_" . $2/e unless /^ScreenSaver(?:Reset|Active)$/;

	my %abbr = (
		"Iter" => "iterator",
		"Req" => "request",
		"Rep" => "reply",
	);

	my $word;
	if(/CHAR2B|INT64|FLOAT32|FLOAT64|BOOL32|STRING8/)
	{
		$word = qr/[A-Z](?:[A-Z0-9]*|[a-z]*)/;
	} else {
		$word = qr/[0-9]+|[A-Z](?:[A-Z]*|[a-z]*)/;
	}
	s/($word)_?(?=[0-9A-Z]|$)/"_" . ($abbr{$1} or lc($1))/eg;

	s/^_shape_shape_/_shape_/;
	s/^_xf_?86_dri/_xf86dri/;
	$_ = "_family_decnet" if $_ eq "_family_de_cnet";
	return "XCB" . uc($_) if $const;

	$_ .= "_t" unless $fun or /_id$/;

	return "xcb" . $_;
}

s/^(\s*#\s*include\s*<)X11\/XCB\//$1xcb\//;
s/([_A-Za-z][_A-Za-z0-9]*)([ \t]*\()?/convert($1, defined $2) . ($2 or "")/eg;
