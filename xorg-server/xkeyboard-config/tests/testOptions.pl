#!/usr/bin/env perl

use strict;
use warnings;
use xkbTestFunc;

xkbTestFunc::backupXkbSettings();

xkbTestFunc::dumpXkbSettingsBackup();

xkbTestFunc::testLevel2( "group", "option", 4, ":", "", 0, 0, 1 );

xkbTestFunc::restoreXkbSettings();

print "Done!\n";
