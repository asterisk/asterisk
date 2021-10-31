#!/usr/bin/perl -Tw
# Author:       Peter Nixon <codemonkey@peternixon.net>
# Date:         April 2004
# Copy Policy:  GNU Public Licence Version 2 or later
# URL:          http://www.peternixon.net/code/
# Supported:    PostgreSQL, Oracle, MySQL
# Copyright:    2004 Peter Nixon <codemonkey@peternixon.net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# $Id$
#
# Use these commands to create the appropriate SQL tables
# If flags is 1 then the record is not included in the output extensions file
#
#CREATE TABLE extensions (
#        context VARCHAR(20) DEFAULT 'default' NOT NULL,
#        extension VARCHAR(20) NOT NULL,
#        priority INTEGER DEFAULT '1' NOT NULL,
#        application VARCHAR(20) NOT NULL,
#        args VARCHAR(50),
#        descr TEXT,
#        flags BOOLEAN DEFAULT '0' NOT NULL,
#        PRIMARY KEY(context, extension, priority)
#);

#CREATE TABLE globals (
#        variable VARCHAR(20) NOT NULL,
#        value VARCHAR(50) NOT NULL,
#        PRIMARY KEY(variable, value)
#);

use strict;	# Make sure we write decent perl code

require DBI;	# We need database drivers for this thing to work

################### BEGIN OF CONFIGURATION ####################

my $table_name = "extensions";		# name of the extensions table
my $global_table_name = "globals";	# name of the globals table
my $extensions_conf = "/etc/asterisk/extensions.conf";	# path to extensions.conf
#	 WARNING: this file will be substituted by the output of this program
my $dbbrand = "Pg"; 		# Hint: "mysql" or any other Perl DBI driver.
my $hostname = "localhost";	# The SQL server's hostname or IP
my $database = "peter";		# the name of the database our tables are kept
my $username = "peter";		# username to connect to the database
my $password = "";		# password to connect to the database
my $verbose = 1; 		# Verbosity Level (0 - 2)

################### END OF CONFIGURATION #######################

# You should not need to edit anything below here
my $dbh;

sub db_connect {
        if ($verbose > 1) { print "DEBUG: Connecting to Database Host: $hostname\n" }
        if ($hostname eq 'localhost') {
        if ($verbose > 1) { print "DEBUG: SQL server is on localhost so using UNIX socket instead of network socket.\n" }
                $dbh = DBI->connect("DBI:$dbbrand:dbname=$database", "$username", "$password")
                        or die "Couldn't connect to database: " . DBI->errstr;
        }
        else {
                $dbh = DBI->connect("DBI:$dbbrand:dbname=$database;host=$hostname", "$username", "$password")
                        or die "Couldn't connect to database: " . DBI->errstr;
        }
}

sub db_disconnect {
        if ($verbose > 1) { print "DEBUG: Disconnecting from Database Host: $hostname\n" }
        $dbh->disconnect
            or warn "Disconnection failed: $DBI::errstr\n";
}

sub get_globals {
        if ($verbose > 0) { print "Checking Database for [global] variables\n"; }
        my $sth = $dbh->prepare("SELECT variable, value FROM $global_table_name ORDER BY variable")
                or die "Couldn't prepare statement: " . $dbh->errstr;

        $sth->execute()             # Execute the query
            or die "Couldn't execute SELECT statement: " . $sth->errstr;

        if ($sth->rows > 0) {
		print EXTEN "[globals]\n";
	        while (my @global = $sth->fetchrow_array()) {
			print EXTEN "$global[0] = $global[1]\n";
        	}
		print EXTEN "\n";
        } else {
		print "WARNING: You have no global variables set\n";
	}
        $sth->finish;
}

sub get_contexts {
        if ($verbose > 0) { print "Checking Database for contexts\n"; }
        my $sth = $dbh->prepare("SELECT context FROM $table_name GROUP BY context")
                or die "Couldn't prepare statement: " . $dbh->errstr;

        $sth->execute()             # Execute the query
            or die "Couldn't execute SELECT statement: " . $sth->errstr;

        if ($sth->rows > 0) {
	        while (my @context = $sth->fetchrow_array()) {
			print EXTEN "[$context[0]]\n";
			&get_extensions($context[0]);
			print EXTEN "\n";
        	}
		print EXTEN "\n";
        } else {
		print "WARNING: You have no contexts defined in the $table_name table\n";
	}
        $sth->finish;
}

sub get_extensions {
	my $context = $_[0]; my @extension;
        if ($verbose > 0) { print " Checking Database for [$context] extensions\n"; }
        my $sth = $dbh->prepare("SELECT extension, priority, application, args, descr FROM $table_name WHERE context='$context' AND flags = '0' ORDER BY extension, priority")
                or die "Couldn't prepare statement: " . $dbh->errstr;

        $sth->execute()             # Execute the query
            or die "Couldn't execute SELECT statement: " . $sth->errstr;

        if ($sth->rows > 0) {
	        while (@extension = $sth->fetchrow_array()) {
			print EXTEN "exten => $extension[0],$extension[1],$extension[2]";
			print EXTEN "($extension[3])" if defined $extension[3];
			print EXTEN "  ; $extension[4]" if defined $extension[4];
			print EXTEN "\n";
        	}
        } else {
		print "WARNING: You have no extensions for [$context]\n";
	}
        $sth->finish;
}


sub main {
	open EXTEN, ">$extensions_conf" || die "Cannot create/overwrite extensions file: $extensions_conf\n";
	&db_connect;
	&get_globals;
	&get_contexts;
	&db_disconnect;
	close EXTEN;	# Close the file handle
        if ($verbose > 0) { print "New $extensions_conf successfully written.\n"; }
	return 1;
}


exit &main();
