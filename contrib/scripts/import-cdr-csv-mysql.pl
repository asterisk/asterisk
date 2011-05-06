#! /usr/bin/perl

# Copyright (c) 2010 by Precise Networks, Inc.  All rights reserved.  http://precisenetworksinc.com

# See http://www.asterisk.org for more information about
# the Asterisk project. Please do not directly contact
# any of the maintainers of this project for assistance;
# the project provides a web site, mailing lists and IRC
# channels for your use.
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.

# 2010-01-30 by Patrick Bennett Hagen: original version.

use strict;
use DBI();

# main
    !$ARGV[6] && die "Required arguments: <cdr_log_file> <mysql_hostname> <database> <table> <username> <password> <preview|import>\n";

    open(cdr_csv, "<$ARGV[0]" || die "Unable to open $ARGV[0]\n");
    my @cdr = <cdr_csv>;
    close(cdr_csv);

    my $dbh = DBI->connect("DBI:mysql:database=$ARGV[2];host=$ARGV[1]","$ARGV[4]","$ARGV[5]");
    my $sth = undef;
    my ($accountcode, $src, $dst, $dcontext, $clid, $channel, $dstchannel, $lastapp, $lastdata, $start, $answer, $end, $duration, $billsec, $disposition, $amastr, $amaflags, $uniqueid, $userfield) = undef;

    foreach (@cdr) {
        ($accountcode, $_) = getNextField($_);
        ($src        , $_) = getNextField($_);
        ($dst        , $_) = getNextField($_);
        ($dcontext   , $_) = getNextField($_);
        ($clid       , $_) = getNextField($_);
        $clid =~ s/\\\"\\\"/\\\"/g;
        ($channel    , $_) = getNextField($_);
        ($dstchannel , $_) = getNextField($_);
        ($lastapp    , $_) = getNextField($_);
        ($lastdata   , $_) = getNextField($_);
        ($start      , $_) = getNextField($_);
        ($answer     , $_) = getNextField($_);
        ($end        , $_) = getNextField($_, ",");
        ($duration   , $_) = getNextField($_, ",");
        ($billsec    , $_) = getNextField($_, ",");
        ($disposition, $_) = getNextField($_);
        ($amastr     , $_) = getNextField($_);
        ($amastr eq "'DEFAULT'")       && ($amaflags="'0'");
        ($amastr eq "'OMIT'")          && ($amaflags="'1'");
        ($amastr eq "'BILLING'")       && ($amaflags="'2'");
        ($amastr eq "'DOCUMENTATION'") && ($amaflags="'3'");
        ($uniqueid   , $_) = getNextField($_);
        ($userfield  , $_) = getNextField($_, "\n");

        my $s = "insert into $ARGV[3] (accountcode, src, dst, dcontext, clid, channel, dstchannel, lastapp, lastdata, calldate, duration, billsec, disposition, amaflags, uniqueid, userfield) values ($accountcode, $src, $dst, $dcontext, $clid, $channel, $dstchannel, $lastapp, $lastdata, $start, $duration, $billsec, $disposition, $amaflags, $uniqueid, $userfield)";
        $sth = $dbh->prepare($s);
        if ($ARGV[6] eq "import") {
            $sth->execute();
            print ".";
        }
        ($ARGV[6] eq "preview") && (print "$s\n\n");
    }
    $dbh->disconnect(); print "done.\n";
# main

sub getNextField {
    my $s = shift;
    my $delimiter = shift;
    (!$delimiter) && ($delimiter = "\",\"");
    my $endPos = index $s, "$delimiter";
    ($delimiter eq ",")     && ($endPos++);
    ($delimiter eq "\n")    && ($endPos++);
    ($delimiter eq "\",\"") && ($endPos+=2);
    my $field = substr $s, 0, $endPos, "";
    $field = substr $field, 0, (length $field) - 1;
    ((substr $field, -1) eq "\"")   && ($field = substr $field, 0, (length $field) - 1);
    ((substr $field, 0, 1) eq "\"") && ($field = substr $field, 1, length $field);
    $field = $dbh->quote($field);
    return $field, $s;
} # getNextField
