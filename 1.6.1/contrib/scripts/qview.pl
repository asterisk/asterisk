#!/usr/bin/perl
#
# Asterisk Queue Viewer
# Uses management interface to query call queues on a machine
# (C) 2003 David C. Troy -- dave@toad.net
#
# This program is free software, distributed under the terms of the
# GNU General Public License
#

use IO::Socket;
use CGI qw(:standard);
use CGI::Carp qw/fatalsToBrowser/;

$host = "asterisk.yourdomain.com";
$port = 5038;
$user = "manager_user";
$secret = "Manager_secret";
$EOL = "\015\012";
$BLANK = $EOL x 2;
$queue = param('queue');

$remote = IO::Socket::INET->new(
		Proto	=> 'tcp',	# protocol
		PeerAddr=> $host,	# Address of server
		PeerPort=> $port,	# port of server
		Reuse   => 1
		) or die "$!";

$remote->autoflush(1);	# Send immediately

# Login and get our booty from Asterisk
$logres = send_cmd("Action: Login${EOL}Username: $user${EOL}Secret: $secret$BLANK");
$qinfo = send_cmd("Action: queues$BLANK$EOL");
$logres = send_cmd("Action: Logoff$BLANK");
close $remote;			# Close socket

my %qcalls = map { /(\S+)\s+has (\d+) calls.*?\n\n/sg; } $qinfo;
my %qmax = map { /(\S+)\s+has \d+ calls \(max (\S+)\).*?\n\n/sg; } $qinfo;
my %qstrat = map { /(\S+)\s+has \d+ calls \(max \S+\) in (\S+) strategy.*?\n\n/sg; } $qinfo;
my %qmems = map { /(\S+)\s+has \d+ calls.*?Members:.*?\s{6}(.*?)\s{3}\S*?\s*?Callers/sg; } $qinfo;
my %qcallers = map { /(\S+)\s+has \d+ calls.*?([No ]*Callers.*?)\n\n/sg; } $qinfo;

print header();
print start_html(-head=>meta({-http_equiv=>'Refresh', -content=>'120'}),
			-title=>"PBX Queue Viewer",
			-style=>{'src'=>'/pbxinfo.css'});
print "<table width=850><tr>";

$col = 0;

foreach $q (keys %qcalls) {

   $mems = $qmems{$q};
   $mems =~ s/      //g;
   $mems =~ s/\n/<br>\n/g;
   $callers = $qcallers{$q};
   $callers =~ s/      //g;
   $callers =~ s/Callers:.*\n//g;
   $callers =~ s/\n/<br>/g;

   print qq{<td valign=top width=48%><table width=100%>
<tr><th colspan=2><A HREF=/mrtg/qmon-$q.html>$q</A>&nbsp;&nbsp;$qcalls{$q} calls (max $qmax{$q}), $qstrat{$q} strategy</th></tr>
<tr><td valign=top width=55%>$mems</td><td valign=top width=45%>$callers</td></tr>
</table></td>
};

   print "</tr><tr>" if $col;
   $col = 0 if $col++;

}

print "</table>";

print end_html();

exit(0);

sub read_conn {

   my $buf="";
   while (<$remote>) {
      last if $_ eq $EOL;
      s/$EOL/\n/g;
      $buf .= $_;
   }

   return $buf
}

sub send_cmd {
   my $cmd = @_[0];

   my $buf="";
   print $remote $cmd;

   $buf = read_conn();

   return $buf;
}
