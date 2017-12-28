#!/usr/bin/perl
#
# Copyright (c) 2008 Digium, Inc.
#
# Tilghman Lesher <dbsep.cgi@the-tilghman.com>
#
# See http://www.asterisk.org for more information about
# the Asterisk project. Please do not directly contact
# any of the maintainers of this project for assistance;
# the project provides a web site, mailing lists and IRC
# channels for your use.
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.
#
# $Id$
#

use CGI;
use DBI;
use strict;

my ($cgi, $dbh, %cfg, $table, $mode);

# The following settings are expected:
#
# dsn=<some valid dsn>
# dbuser=<user>
# dbpass=<passwd>
# dbschema=<dbname>
# backslash_is_escape={yes|no}
#
open CFG, "</etc/asterisk/dbsep.conf";
while (<CFG>) {
	chomp;
	next if (m/^[#;]/);
	next if (m/^\s*$/);
	my ($name,@value) = split '=';
	$cfg{lc($name)} = join('=', @value);
}
close CFG;

$cgi = new CGI;

$ENV{PATH_INFO} =~ m/\/([^\/]*)\/([^\/]*)$/;
($table, $mode) = ($1, lc($2));

#print STDERR "PATH_INFO=$ENV{PATH_INFO}, table=$table, mode=$mode\n";

if ($mode eq 'single') {
	# All parameters as POST
	my ($sql, $sth, $row, @answer);
	$sql = "SELECT * FROM $table WHERE " . join(" AND ", cgi_to_where_clause($cgi, \%cfg));
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$sth = $dbh->prepare($sql) || throw_error("Invalid query: $sql");
	$sth->execute() || throw_error("Invalid query: $sql");
	$row = $sth->fetchrow_hashref();
	foreach (keys %$row) {
		foreach my $item (split /\;/, $row->{$_}) {
			push @answer, encode($_) . "=" . encode($item);
		}
	}
	$sth->finish();
	$dbh->disconnect();
	print "Content-type: text/plain\n\n";
	print join("&", @answer) . "\n";
} elsif ($ENV{PATH_INFO} =~ m/multi$/) {
	# All parameters as POST
	my ($sql, $sth, @answer);
	$sql = "SELECT * FROM $table WHERE " . join(" AND ", cgi_to_where_clause($cgi, \%cfg));
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$sth = $dbh->prepare($sql) || throw_error("Invalid query: $sql");
	$sth->execute() || throw_error("Invalid query: $sql");
	print "Content-type: text/plain\n\n";
	while (my $row = $sth->fetchrow_hashref()) {
		@answer = ();
		foreach (keys %$row) {
			foreach my $item (split /\;/, $row->{$_}) {
				push @answer, encode($_) . "=" . encode($item);
			}
		}
		print join("&", @answer) . "\n";
	}
	$sth->finish();
	$dbh->disconnect();
} elsif ($ENV{PATH_INFO} =~ m/update$/) {
	# where clause in GET, update parameters in POST
	my (%get, @get, $sql, $name, $value, $affected);
	foreach (split '&', $ENV{QUERY_STRING}) {
		($name, $value) = split '=';
		$name = decode($name);
		next if (!isname($name));
		$value = escape_value(decode($value));
		if ($name =~ m/ /) {
			push @get, "$name '$value'";
		} else {
			push @get, "$name='$value'";
		}
		$get{$name}++;
	}
	$sql = "UPDATE $table SET " . join(",", cgi_to_where_clause($cgi, \%cfg, \%get)) . " WHERE " . join(" AND ", @get);
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$affected = $dbh->do($sql);
	$dbh->disconnect();
	print "Content-type: text/html\n\n$affected\n";
} elsif ($ENV{PATH_INFO} =~ m/store$/) {
	# All parameters as POST
	my (@param, $sql, @fields, @values, $affected);
	foreach my $param (cgi_to_where_clause($cgi, \%cfg)) {
		my ($name, $value) = split /=/, $param;
		push @fields, $name;
		push @values, $value;
	}
	$sql = "INSERT INTO $table (" . join(",", @fields) . ") VALUES (" . join(",", @values) . ")";
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$affected = $dbh->do($sql);
	$dbh->disconnect();
	print "Content-type: text/html\n\n$affected\n";
} elsif ($ENV{PATH_INFO} =~ m/destroy$/) {
	# All parameters as POST
	my ($sql, $affected);
	$sql = "DELETE FROM $table WHERE " . join(" AND ", cgi_to_where_clause($cgi, \%cfg));
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$affected = $dbh->do($sql);
	$dbh->disconnect();
	print "Content-type: text/html\n\n$affected\n";
} elsif ($ENV{PATH_INFO} =~ m/require$/) {
	my $result = 0;
	my $dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	my $sql = "SELECT data_type, character_maximum_length FROM information_schema.tables AS t " .
			"JOIN information_schema.columns AS c " .
			"ON t.table_catalog=c.table_catalog AND " .
			"t.table_schema=c.table_schema AND " .
			"t.table_name=c.table_name " .
			"WHERE c.table_schema='$cfg{dbschema}' AND " .
			"c.table_name=? AND c.column_name=?";
	my $sth = $dbh->prepare($sql);
	foreach my $param (cgi_to_where_clause($cgi, \%cfg)) {
		my ($colname, $value) = split /=/, $param;
		my ($type, $size) = split /:/, $value;
		$sth->execute($table, $colname);
		my ($dbtype, $dblen) = $sth->fetchrow_array();
		$sth->finish();
		if ($type eq 'char') {
			if ($dbtype !~ m#char#i) {
				print STDERR "REQUIRE: $table: Type of column $colname requires char($size), but column is of type $dbtype instead!\n";
				$result = -1;
			} elsif ($dblen < $size) {
				print STDERR "REQUIRE: $table: Size of column $colname requires $size, but column is only $dblen long!\n";
				$result = -1;
			}
		} elsif ($type eq 'integer') {
			if ($dbtype =~ m#char#i and $dblen < $size) {
				print STDERR "REQUIRE: $table: Size of column $colname requires $size, but column is only $dblen long!\n";
				$result = -1;
			} elsif ($dbtype !~ m#int|float|double|dec|num#i) {
				print STDERR "REQUIRE: $table: Type of column $colname requires integer($size), but column is of type $dbtype instead!\n";
				$result = -1;
			}
		} # TODO More type checks
	}
	$dbh->disconnect();
	print "Content-type: text/html\n\n$result\n";
} elsif ($ENV{PATH_INFO} =~ m/static$/) {
	# file parameter in GET, no POST
	my (@get, $filename, $sql, $sth);
	@get = split '&', $ENV{QUERY_STRING};
	foreach (@get) {
		my ($name, $value) = split '=';
		if (decode($name) eq 'file') {
			$filename = decode($value);
			last;
		}
	}
	$sql = "SELECT cat_metric, category, var_name, var_val FROM $table WHERE filename=" . escape_value($filename) . " AND commented=0 ORDER BY cat_metric DESC, var_metric ASC, category, var_name";
	$dbh = DBI->connect($cfg{dsn}, $cfg{dbuser}, $cfg{dbpass});
	$sth = $dbh->prepare($sql) || throw_error("Invalid query: $sql");
	$sth->execute() || throw_error("Invalid query: $sql");
	print "Content-type: text/plain\n\n";
	while (my $row = $sth->fetchrow_hashref()) {
		my @answer = ();
		foreach (keys %$row) {
			push @answer, encode($_) . "=" . encode($row->{$_});
		}
		print join("&", @answer) . "\n";
	}
	$sth->finish();
	$dbh->disconnect();
} else {
	print "Content-type: text/plain\n\nUnknown query\n";
}

sub encode {
	my ($stuff) = @_;
	$stuff =~ s/([^a-zA-Z0-9_\.])/uc sprintf("%%%02x",ord($1))/eg;
	return $stuff;
}

sub decode {
	my ($stuff) = @_;
	$stuff =~ s/%([a-fA-F0-9][a-fA-F0-9])/pack("C", hex($1))/eg;
	return $stuff;
}

sub isname {
	my ($name) = @_;
	if ($name =~ m#[^A-Za-z0-9_ ]#) {
		return 0;
	} else {
		return 1;
	}
}

sub escape_value {
	my ($value, $cfg) = @_;
	if ($cfg->{backslash_is_escape} =~ m/^(no|0|false)$/i) {
		$value =~ s#'#''#g;
	} else {
		$value =~ s#(['\\])#$1$1#g;
	}
	return $value;
}

sub cgi_to_where_clause {
	my ($cgi, $cfg, $get) = @_;
	my @param = ();

	foreach my $name ($cgi->param()) {
		my $value = escape_value($cgi->param($name), $cfg);

		# Ensure name isn't funny-like
		next if (!isname($name));
		next if ($get->{$name});

		if ($name =~ m# #) {
			push @param, "$name '$value'";
		} else {
			push @param, "$name='$value'";
		}
	}
	return @param;
}

sub throw_error {
	my ($msg) = @_;
	print "Content-type: text/plain\n\n$msg\n";
	print STDERR $msg . "\n";
	exit;
}
