#!/usr/bin/perl -Tw
# Use these commands to create the appropriate tables in MySQL
# If flags is 1 then this record is not included in the output extensions file
# 
#CREATE TABLE extensions (
#	context CHAR(20) DEFAULT 'default' NOT NULL,
#	extension CHAR(20) NOT NULL,
#	priority INT(2) DEFAULT '1' NOT NULL,
#	application CHAR(20) NOT NULL,
#	args CHAR(50),
#	descr TEXT,
#	flags INT(1) DEFAULT '0' NOT NULL,
#	PRIMARY KEY(context, extension, priority)
#);
#
#CREATE TABLE globals (
#	variable CHAR(20) NOT NULL,
#	value CHAR(50) NOT NULL,
#	PRIMARY KEY(variable, value)
#);

use DBI;
################### BEGIN OF CONFIGURATION ####################

# the name of the extensions table
$table_name = "extensions";
# the name of the globals table
$global_table_name = "globals";
# the path to the extensions.conf file
# WARNING: this file will be substituted by the output of this program
$extensions_conf = "/etc/asterisk/extensions.conf";
# the name of the box the MySQL database is running on
$hostname = "localhost";
# the name of the database our tables are kept
$database = "user";
# username to connect to the database
$username = "";
# password to connect to the database
$password = "";

################### END OF CONFIGURATION #######################

open EXTEN, ">$extensions_conf" || die "Cannot create/overwrite extensions file: $extensions_conf\n";

$dbh = DBI->connect("dbi:mysql:dbname=$database;host=$hostname", "$username", "$password");
$statement = "SELECT * from $global_table_name order by variable";
my $result = $dbh->selectall_arrayref($statement);
unless ($result) {
  # check for errors after every single database call
  print "dbh->selectall_arrayref($statement) failed!\n";
  print "DBI::err=[$DBI::err]\n";
  print "DBI::errstr=[$DBI::errstr]\n";
  exit;
}
my @resultSet = @{$result};
if ( $#resultSet > -1 ) {
	print EXTEN "[globals]\n";
	foreach $row (@{ $result }) {
		my @result = @{ $row };
		print EXTEN "$result[0] = $result[1]\n";
	}
	print EXTEN "\n";
}

$statement = "SELECT context from $table_name group by context";

$result = $dbh->selectall_arrayref($statement);
unless ($result) {
  # check for errors after every single database call
  print "dbh->selectall_arrayref($statement) failed!\n";
  print "DBI::err=[$DBI::err]\n";
  print "DBI::errstr=[$DBI::errstr]\n";
}

@resultSet = @{$result};
if ( $#resultSet == -1 ) {
  print "No extensions defined in $table_name\n";
  exit;
}

foreach my $row ( @{ $result } ) {
	my $context = @{ $row }[0];
	print EXTEN "[$context]\n";
	$statement = "SELECT * from $table_name where context='$context' order by extension, priority";
	my $result = $dbh->selectall_arrayref($statement);
	unless ($result) {
		# check for errors after every single database call
		print "dbh->selectall_arrayref($statement) failed!\n";
		print "DBI::err=[$DBI::err]\n";
		print "DBI::errstr=[$DBI::errstr]\n";
		exit;
	}

	my @resSet = @{$result};
	if ( $#resSet == -1 ) {          
		print "no results\n";
		exit;
	}
	foreach my $row ( @{ $result } ) {
		my @result = @{ $row };
		if ($result[6] == 0) {
			print EXTEN "exten => $result[1],$result[2],$result[3]";
			print EXTEN "($result[4])" if defined $result[4];
			print EXTEN "\t" if not defined $result[4];
			print EXTEN "\t; $result[5]" if defined $result[5];
			print EXTEN "\n";
		}
	}                                         	
	print EXTEN "\n";
}

exit 0;

