#!/usr/bin/env perl

use strict;
use warnings;
use xkbTestFunc;

xkbTestFunc::backupXkbSettings();

xkbTestFunc::dumpXkbSettingsBackup();

xkbTestFunc::testLevel1( "model", 1 );

xkbTestFunc::restoreXkbSettings();

print "Done!\n";
