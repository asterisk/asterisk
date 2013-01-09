#!/usr/bin/perl
#
# Web based Voicemail for Asterisk
#
# Copyright (C) 2002, Linux Support Services, Inc.
#
# Distributed under the terms of the GNU General Public License
#
# Written by Mark Spencer <markster@linux-support.net>
#
# (icky, I know....  if you know better perl please help!)
#
#
# Synchronization added by GDS Partners (www.gdspartners.com)
#			 Stojan Sljivic (stojan.sljivic@gdspartners.com)
#
use CGI qw/:standard/;
use Carp::Heavy;
use CGI::Carp qw(fatalsToBrowser);
use DBI;
use Fcntl qw ( O_WRONLY O_CREAT O_EXCL );
use Time::HiRes qw ( usleep );

$context=""; # Define here your by default context (so you dont need to put voicemail@context in the login)

@validfolders = ( "INBOX", "Old", "Work", "Family", "Friends", "Cust1", "Cust2", "Cust3", "Cust4", "Cust5" );

%formats = (
	"wav" => {
		name => "Uncompressed WAV",
		mime => "audio/x-wav",
		pref => 1
	},
	"WAV" => {
		name => "GSM Compressed WAV",
		mime => "audio/x-wav",
		pref => 2
	},
	"gsm" => {
		name => "Raw GSM Audio",
		mime => "audio/x-gsm",
		pref => 3
	}
);

$astpath = "/_asterisk";

$stdcontainerstart = "<table align=center width=600><tr><td>\n";
$footer = "<hr><font size=-1><a href=\"http://www.asterisk.org\">The Asterisk Open Source PBX</a> Copyright 2004-2008, <a href=\"http://www.digium.com\">Digium, Inc.</a></a>";
$stdcontainerend = "</td></tr><tr><td align=right>$footer</td></tr></table>\n";

sub lock_path($) {

	my($path) = @_;
	my $rand;
	my $rfile;
	my $start;
	my $res;
	
	$rand = rand 99999999;	
	$rfile = "$path/.lock-$rand";
	
	sysopen(RFILE, $rfile, O_WRONLY | O_CREAT | O_EXCL, 0666) or return -1;
	close(RFILE);
	
	$res = link($rfile, "$path/.lock");
	$start = time;
	if ($res == 0) {
	while (($res == 0) && (time - $start <= 5)) {
		$res = link($rfile, "$path/.lock");
		usleep(1);
	}
	}
	unlink($rfile);
	
	if ($res == 0) {
		return -1;
	} else {
		return 0;
	}
}

sub unlock_path($) {

	my($path) = @_;
	
	unlink("$path/.lock");
}

sub untaint($) {

	my($data) = @_;
	
	if ($data =~ /^([-\@\w.]+)$/) {
		$data = $1;
	} else {
		die "Security violation.";
	}
	
	return $data;
}

sub login_screen($) {
	print header;
	my ($message) = @_;
	print <<_EOH;

<TITLE>Asterisk Web-Voicemail</TITLE>
<BODY BGCOLOR="white">
$stdcontainerstart
<FORM METHOD="post">
<input type=hidden name="action" value="login">
<table align=center>
<tr><td valign=top align=center rowspan=6><img align=center src="$astpath/animlogo.gif"></td></tr>
<tr><td align=center colspan=2><font size=+2>Comedian Mail Login</font></td></tr>
<tr><td align=center colspan=2><font size=+1>$message</font></td></tr>
<tr><td>Mailbox:</td><td><input type=text name="mailbox"></td></tr>
<tr><td>Password:</td><td><input type=password name="password"></td></tr>
<tr><td align=right colspan=2><input value="Login" type=submit></td></tr>
<input type=hidden name="context" value="$context">
</table>
</FORM>
$stdcontainerend
</BODY>\n
_EOH

}

sub check_login($$)
{
	local ($filename, $startcat) = @_;
	local ($mbox, $context) = split(/\@/, param('mailbox'));
	local $pass = param('password');
	local $category = $startcat;
	local @fields;
	local $tmp;
	local (*VMAIL);
	if (!$category) {
		$category = "general";
	}
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	if (!$filename) {
		$filename = "/etc/asterisk/voicemail.conf";
	}
#	print header;
#	print "Including <h2>$filename</h2> while in <h2>$category</h2>...\n";
	open(VMAIL, "<$filename") || die("Bleh, no $filename");
	while(<VMAIL>) {
		chomp;
		if (/include\s\"([^\"]+)\"$/) {
			($tmp, $category) = &check_login("/etc/asterisk/$1", $category);
			if (length($tmp)) {
#				print "Got '$tmp'\n";
				return ($tmp, $category);
			}
		} elsif (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category eq "general") {
			if (/([^\s]+)\s*\=\s*(.*)/) {
				if ($1 eq "dbname") {
					$dbname = $2;
				} elsif ($1 eq "dbpass") {
					$dbpass = $2;
				} elsif ($1 eq "dbhost") {
					$dbhost = $2;
				} elsif ($1 eq "dbuser") {
					$dbuser = $2;
				}
			}
			if ($dbname and $dbpass and $dbhost and $dbuser) {

				# db variables are present.  Use db for authentication.
				my $dbh = DBI->connect("DBI:mysql:$dbname:$dbhost",$dbuser,$dbpass);
				my $sth = $dbh->prepare(qq{select fullname,context from voicemail where mailbox='$mbox' and password='$pass' and context='$context'});
				$sth->execute();
				if (($fullname, $category) = $sth->fetchrow_array()) {
					return ($fullname ? $fullname : "Extension $mbox in $context",$category);
				}
			}
		} elsif (($category ne "general") && ($category ne "zonemessages")) { 
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
#				print "<p>Mailbox is $1\n";
				if (($mbox eq $1) && (($pass eq $fields[0]) || ("-${pass}" eq $fields[0])) && ($context eq $category)) {
					return ($fields[1] ? $fields[1] : "Extension $mbox in $context", $category);
				}
			}
		}
	}
	close(VMAIL);
	return check_login_users();
}

sub check_login_users {
	my ($mbox, $context) = split(/\@/, param('mailbox'));
	my $pass = param('password');
	my ($found, $fullname) = (0, "");
	open VMAIL, "</etc/asterisk/users.conf";
	while (<VMAIL>) {
		chomp;
		if (m/\[(.*)\]/) {
			if ($1 eq $mbox) {
				$found = 1;
			} elsif ($found == 2) {
				close VMAIL;
				return (($fullname ? $fullname : "Extension $mbox in $context"), $context);
			} else {
				$found = 0;
			}
		} elsif ($found) {
			my ($var, $value) = split /\s*=\s*/, $_, 2;
			if ($var eq 'vmsecret' and $value eq $pass) {
				$found = 2;
			} elsif ($var eq 'fullname') {
				$fullname = $value;
				if ($found == 2) {
					close VMAIL;
					return ($fullname, $context);
				}
			}
		}
	}
	close VMAIL;
	return ("", "");
}

sub validmailbox($$$$)
{
	local ($context, $mbox, $filename, $startcat) = @_;
	local $category = $startcat;
	local @fields;
	local (*VMAIL);
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	if (!$filename) {
		$filename = "/etc/asterisk/voicemail.conf";
	}
	if (!$category) {
		$category = "general";
	}
	open(VMAIL, "<$filename") || die("Bleh, no $filename");
	while (<VMAIL>) {
		chomp;
		if (/include\s\"([^\"]+)\"$/) {
			($tmp, $category) = &validmailbox($mbox, $context, "/etc/asterisk/$1");
			if ($tmp) {
				return ($tmp, $category);
			}
		} elsif (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category eq "general") {
			if (/([^\s]+)\s*\=\s*(.*)/) {
				if ($1 eq "dbname") {
					$dbname = $2;
				} elsif ($1 eq "dbpass") {
					$dbpass = $2;
				} elsif ($1 eq "dbhost") {
					$dbhost = $2;
				} elsif ($1 eq "dbuser") {
					$dbuser = $2;
				}
			}
			if ($dbname and $dbpass and $dbhost and $dbuser) {

				# db variables are present.  Use db for authentication.
				my $dbh = DBI->connect("DBI:mysql:$dbname:$dbhost",$dbuser,$dbpass);
				my $sth = $dbh->prepare(qq{select fullname,context from voicemail where mailbox='$mbox' and password='$pass' and context='$context'});
				$sth->execute();
				if (($fullname, $context) = $sth->fetchrow_array()) {
					return ($fullname ? $fullname : "unknown", $category);
				}
			}
		} elsif (($category ne "general") && ($category ne "zonemessages") && ($category eq $context)) {
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
				if (($mbox eq $1) && ($context eq $category)) {
					return ($fields[2] ? $fields[2] : "unknown", $category);
				}
			}
		}
	}
	return ("", $category);
}

sub mailbox_options()
{
	local($context, $current, $filename, $category) = @_;
	local (*VMAIL);
	local $tmp2;
	local $tmp;
	if (!$filename) {
		$filename = "/etc/asterisk/voicemail.conf";
	}
	if (!$category) {
		$category = "general";
	}
#	print header;
#	print "Including <h2>$filename</h2> while in <h2>$category</h2>...\n";
	open(VMAIL, "<$filename") || die("Bleh, no voicemail.conf");
	while(<VMAIL>) {
		chomp;
		s/\;.*$//;
		if (/include\s\"([^\"]+)\"$/) {
			($tmp2, $category) = &mailbox_options($context, $current, "/etc/asterisk/$1", $category);
#			print "Got '$tmp2'...\n";
			$tmp .= $tmp2;
		} elsif (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category eq "general") {
			if (/([^\s]+)\s*\=\s*(.*)/) {
				if ($1 eq "dbname") {
					$dbname = $2;
				} elsif ($1 eq "dbpass") {
					$dbpass = $2;
				} elsif ($1 eq "dbhost") {
					$dbhost = $2;
				} elsif ($1 eq "dbuser") {
					$dbuser = $2;
				}
			}
			if ($dbname and $dbpass and $dbhost and $dbuser) {

				# db variables are present.  Use db for authentication.
				my $dbh = DBI->connect("DBI:mysql:$dbname:$dbhost",$dbuser,$dbpass);
				my $sth = $dbh->prepare(qq{select mailbox,fullname,context from voicemail where context='$context' order by mailbox});
				$sth->execute();
				while (($mailbox, $fullname, $category) = $sth->fetchrow_array()) {
					$text = $mailbox;
					if ($fullname) {
						$text .= " (".$fullname.")";
					}
					if ($mailbox eq $current) {
						$tmp .= "<OPTION SELECTED>$text</OPTION>\n";
					} else {
						$tmp .= "<OPTION>$text</OPTION>\n";
					}
				}
				return ($tmp, $category);
			}
		} elsif (($category ne "general") && ($category ne "zonemessages")) {
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
				$text = "$1";
				if ($fields[1]) {
					$text .= " ($fields[1])";
				}
				if ($1 eq $current) {
					$tmp .= "<OPTION SELECTED>$text</OPTION>\n";
				} else {
					$tmp .= "<OPTION>$text</OPTION>\n";
				}
				
			}
		}
	}
	close(VMAIL);
	return ($tmp, $category);
}

sub mailbox_list()
{
	local ($name, $context, $current) = @_;
	local $tmp;
	local $text;
	local $tmp;
	local $opts;
	if (!$context) {
		$context = "default";
	}
	$tmp = "<SELECT name=\"$name\">\n";
	($opts) = &mailbox_options($context, $current);
	$tmp .= $opts;
	$tmp .= "</SELECT>\n";
	
}

sub msgcount() 
{
	my ($context, $mailbox, $folder) = @_;
	my $path = "/var/spool/asterisk/voicemail/$context/$mailbox/$folder";
	if (opendir(DIR, $path)) {
		my @msgs = grep(/^msg....\.txt$/, readdir(DIR));
		closedir(DIR);
		return sprintf "%d", $#msgs + 1;
	}
	return "0";
}

sub msgcountstr()
{
	my ($context, $mailbox, $folder) = @_;
	my $count = &msgcount($context, $mailbox, $folder);
	if ($count > 1) {
		"$count messages";
	} elsif ($count > 0) {
		"$count message";
	} else {
		"no messages";
	}
}
sub messages()
{
	my ($context, $mailbox, $folder) = @_;
	my $path = "/var/spool/asterisk/voicemail/$context/$mailbox/$folder";
	if (opendir(DIR, $path)) {
		my @msgs = sort grep(/^msg....\.txt$/, readdir(DIR));
		closedir(DIR);
		return map { s/^msg(....)\.txt$/$1/; $_ } @msgs;
	}
	return ();
}

sub getcookie()
{
	my ($var) = @_;
	return cookie($var);
}

sub makecookie()
{
	my ($format) = @_;
	cookie(-name => "format", -value =>["$format"], -expires=>"+1y");
}

sub getfields()
{
	my ($context, $mailbox, $folder, $msg) = @_;
	my $fields;
	if (open(MSG, "</var/spool/asterisk/voicemail/$context/$mailbox/$folder/msg${msg}.txt")) {
		while(<MSG>) {
			s/\#.*$//g;
			if (/^(\w+)\s*\=\s*(.*)$/) {
				$fields->{$1} = $2;
			}
		}
		close(MSG);
		$fields->{'msgid'} = $msg;
	} else { print "<BR>Unable to open '$msg' in '$mailbox', '$folder'\n<B>"; }
	$fields;
}

sub message_prefs()
{
	my ($nextaction, $msgid) = @_;
	my $folder = param('folder');
	my $mbox = param('mailbox');
	my $context = param('context');
	my $passwd = param('password');
	my $format = param('format');
	if (!$format) {
		$format = &getcookie('format');
	}
	print header;
	print <<_EOH;

<TITLE>Asterisk Web-Voicemail: Preferences</TITLE>
<BODY BGCOLOR="white">
$stdcontainerstart
<FORM METHOD="post">
<table width=100% align=center>
<tr><td align=right colspan=3><font size=+2>Web Voicemail Preferences</font></td></tr>
<tr><td align=left><font size=+1>Preferred&nbsp;Audio&nbsp;Format:</font></td><td colspan=2></td></tr>
_EOH

foreach $fmt (sort { $formats{$a}->{'pref'} <=> $formats{$b}->{'pref'} } keys %formats) {
	my $clicked = "checked" if $fmt eq $format;
	print "<tr><td></td><td align=left><input type=radio name=\"format\" $clicked value=\"$fmt\"></td><td width=100%>&nbsp;$formats{$fmt}->{name}</td></tr>\n";
}

print <<_EOH;
<tr><td align=right colspan=3><input type=submit value="save settings..."></td></tr>
</table>
<input type=hidden name="action" value="$nextaction">
<input type=hidden name="folder" value="$folder">
<input type=hidden name="mailbox" value="$mbox">
<input type=hidden name="context" value="$context">
<input type=hidden name="password" value="$passwd">
<input type=hidden name="msgid" value="$msgid">
$stdcontainerend
</BODY>\n
_EOH

}

sub message_play()
{
	my ($message, $msgid) = @_;
	my $folder = param('folder');
	my ($mbox, $context) = split(/\@/, param('mailbox'));
	my $passwd = param('password');
	my $format = param('format');
	
	my $fields;
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	
	my $folders = &folder_list('newfolder', $context, $mbox, $folder);
	my $mailboxes = &mailbox_list('forwardto', $context, $mbox);
	if (!$format) {
		$format = &getcookie('format');
	}
	if (!$format) {
		&message_prefs("play", $msgid);
	} else {
		print header(-cookie => &makecookie($format));
		$fields = &getfields($context, $mbox, $folder, $msgid);
		if (!$fields) {
			print "<BR>Bah!\n";
			return;
		}
		my $duration = $fields->{'duration'};
		if ($duration) {
			$duration = sprintf "%d:%02d", $duration/60, $duration % 60; 
		} else {
			$duration = "<i>Unknown</i>";
		}
		print <<_EOH;
	
<TITLE>Asterisk Web-Voicemail: $folder Message $msgid</TITLE>
<BODY BGCOLOR="white">
$stdcontainerstart
<FORM METHOD="post">
<table width=100% align=center>
<tr><td align=right colspan=3><font size=+1>$folder Message $msgid</font></td></tr>
_EOH

		print <<_EOH;
<tr><td align=center colspan=3>
<table>
	<tr><td colspan=2 align=center><font size=+1>$folder <b>$msgid</b></font></td></tr>
	<tr><td><b>Message:</b></td><td>$msgid</td></tr>\n
	<tr><td><b>Mailbox:</b></td><td>$mbox\@$context</td></tr>\n
	<tr><td><b>Folder:</b></td><td>$folder</td></tr>\n
	<tr><td><b>From:</b></td><td>$fields->{callerid}</td></tr>\n
	<tr><td><b>Duration:</b></td><td>$duration</td></tr>\n
	<tr><td><b>Original Date:</b></td><td>$fields->{origdate}</td></tr>\n
	<tr><td><b>Original Mailbox:</b></td><td>$fields->{origmailbox}</td></tr>\n
	<tr><td><b>Caller Channel:</b></td><td>$fields->{callerchan}</td></tr>\n
	<tr><td align=center colspan=2>
	<input name="action" type=submit value="index">&nbsp;
	<input name="action" type=submit value="delete ">&nbsp;
	<input name="action" type=submit value="forward to -> ">&nbsp;
	$mailboxes&nbsp;
	<input name="action" type=submit value="save to ->">
	$folders&nbsp;
	<input name="action" type=submit value="play ">
	<input name="action" type=submit value="download">
</td></tr>
<tr><td colspan=2 align=center>
<embed width=400 height=40 src="vmail.cgi?action=audio&folder=$folder&mailbox=$mbox&context=$context&password=$passwd&msgid=$msgid&format=$format&dontcasheme=$$.$format" autostart=yes loop=false></embed>
</td></tr></table>
</td></tr>
</table>
<input type=hidden name="folder" value="$folder">
<input type=hidden name="mailbox" value="$mbox">
<input type=hidden name="context" value="$context">
<input type=hidden name="password" value="$passwd">
<input type=hidden name="msgid" value="$msgid">
$stdcontainerend
</BODY>\n
_EOH
	}
}

sub message_audio()
{
	my ($forcedownload) = @_;
	my $folder = &untaint(param('folder'));
	my $msgid = &untaint(param('msgid'));
	my $mailbox = &untaint(param('mailbox'));
	my $context = &untaint(param('context'));
	my $format = param('format');
	if (!$format) {
		$format = &getcookie('format');
	}
	&untaint($format);

	my $path = "/var/spool/asterisk/voicemail/$context/$mailbox/$folder/msg${msgid}.$format";

	$msgid =~ /^\d\d\d\d$/ || die("Msgid Liar ($msgid)!");
	grep(/^${format}$/, keys %formats) || die("Format Liar ($format)!");

	# Mailbox and folder are already verified
	if (open(AUDIO, "<$path")) {
		$size = -s $path;
		$|=1;
		if ($forcedownload) {
			print header(-type=>$formats{$format}->{'mime'}, -Content_length => $size, -attachment => "msg${msgid}.$format");
		} else {		
			print header(-type=>$formats{$format}->{'mime'}, -Content_length => $size);
		}
		
		while(($amt = sysread(AUDIO, $data, 4096)) > 0) {
			syswrite(STDOUT, $data, $amt);
		}
		close(AUDIO);
	} else {
		die("Hrm, can't seem to open $path\n");
	}
}

sub message_index() 
{
	my ($folder, $message) = @_;
	my ($mbox, $context) = split(/\@/, param('mailbox'));
	my $passwd = param('password');
	my $message2;
	my $msgcount;	
	my $hasmsg;
	my ($newmessages, $oldmessages);
	my $format = param('format');
	if (!$format) {
		$format = &getcookie('format');
	}
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	if ($folder) {
		$msgcount = &msgcountstr($context, $mbox, $folder);
		$message2 = "&nbsp;&nbsp;&nbsp;Folder '$folder' has " . &msgcountstr($context, $mbox, $folder);
	} else {
		$newmessages = &msgcount($context, $mbox, "INBOX");
		$oldmessages = &msgcount($context, $mbox, "Old");
		if (($newmessages > 0) || ($oldmessages < 1)) {
			$folder = "INBOX";
		} else {
			$folder = "Old";
		}
		$message2 = "You have";
		if ($newmessages > 0) {
			$message2 .= " <b>$newmessages</b> NEW";
			if ($oldmessages > 0) {
				$message2 .= "and <b>$oldmessages</b> OLD";
				if ($oldmessages != 1) {
					$message2 .= " messages.";
				} else {
					$message2 .= "message.";
				}
			} else {
				if ($newmessages != 1) {
					$message2 .= " messages.";
				} else {
					$message2 .= " message.";
				}
			}
		} else {
			if ($oldmessages > 0) {
				$message2 .= " <b>$oldmessages</b> OLD";
				if ($oldmessages != 1) {
					$message2 .= " messages.";
				} else {
					$message2 .= " message.";
				}
			} else {
				$message2 .= " <b>no</b> messages.";
			}
		}
	}
	
	my $folders = &folder_list('newfolder', $context, $mbox, $folder);
	my $cfolders = &folder_list('changefolder', $context, $mbox, $folder);
	my $mailboxes = &mailbox_list('forwardto', $context, $mbox);
	print header(-cookie => &makecookie($format));
	print <<_EOH;

<TITLE>Asterisk Web-Voicemail: $mbox\@$context $folder</TITLE>
<BODY BGCOLOR="white">
$stdcontainerstart
<FORM METHOD="post">
<table width=100% align=center>
<tr><td align=center colspan=2><font size=+2><I>$message</I></font></td></tr>
<tr><td align=right colspan=2><font size=+1><b>$folder</b> Messages</font> <input type=submit name="action" value="change to ->">$cfolders</td></tr>
<tr><td align=left colspan=2><font size=+1>$message2</font></td></tr>
</table>
<table width=100% align=center cellpadding=0 cellspacing=0>
_EOH

print "<tr><td>&nbsp;Msg</td><td>&nbsp;From</td><td>&nbsp;Duration</td><td>&nbsp;Date</td><td>&nbsp;</td></tr>\n";
print "<tr><td><hr></td><td><hr></td><td><hr></td><td><hr></td><td></td></tr>\n";
foreach $msg (&messages($context, $mbox, $folder)) {

	$fields = &getfields($context, $mbox, $folder, $msg);
	$duration = $fields->{'duration'};
	if ($duration) {
		$duration = sprintf "%d:%02d", $duration / 60, $duration % 60;
	} else {
		$duration = "<i>Unknown</i>";
	}
	$hasmsg++;
	print "<tr><td><input type=checkbox name=\"msgselect\" value=\"$msg\">&nbsp;<b>$msg</b></td><td>$fields->{'callerid'}</td><td>$duration</td><td>$fields->{'origdate'}</td><td><input name='play$msg' alt=\"Play message $msg\" border=0 type=image align=left src=\"$astpath/play.gif\"></td></tr>\n";

}
if (!$hasmsg) {
	print "<tr><td colspan=4 align=center><P><b><i>No messages</i></b><P></td></tr>";
}

print <<_EOH;
</table>
<table width=100% align=center>
<tr><td align=right colspan=2>
	<input type="submit" name="action" value="refresh">&nbsp;
_EOH

if ($hasmsg) {
print <<_EOH;
	<input type="submit" name="action" value="delete">&nbsp;
	<input type="submit" name="action" value="save to ->">
	$folders&nbsp;
	<input type="submit" name="action" value="forward to ->">
	$mailboxes
_EOH
}

print <<_EOH;
</td></tr>
<tr><td align=right colspan=2>
	<input type="submit" name="action" value="preferences">
	<input type="submit" name="action" value="logout">
</td></tr>
</table>
<input type=hidden name="folder" value="$folder">
<input type=hidden name="mailbox" value="$mbox">
<input type=hidden name="context" value="$context">
<input type=hidden name="password" value="$passwd">
</FORM>
$stdcontainerend
</BODY>\n
_EOH
}

sub validfolder()
{
	my ($folder) = @_;
	return grep(/^$folder$/, @validfolders);
}

sub folder_list()
{
	my ($name, $context, $mbox, $selected) = @_;
	my $f;
	my $count;
	my $tmp = "<SELECT name=\"$name\">\n";
	foreach $f (@validfolders) {
		$count =  &msgcount($context, $mbox, $f);
		if ($f eq $selected) {
			$tmp .= "<OPTION SELECTED>$f ($count)</OPTION>\n";
		} else {
			$tmp .= "<OPTION>$f ($count)</OPTION>\n";
		}
	}
	$tmp .= "</SELECT>";
}

sub message_rename()
{
	my ($context, $mbox, $oldfolder, $old, $newfolder, $new) = @_;
	my ($oldfile, $newfile);
	return if ($old eq $new) && ($oldfolder eq $newfolder);

	if ($context =~ /^(\w+)$/) {
		$context = $1;
	} else {
		die("Invalid Context<BR>\n");
	}
	
	if ($mbox =~ /^(\w+)$/) {
		$mbox = $1;
	} else {
		die ("Invalid mailbox<BR>\n");
	}
	
	if ($oldfolder =~ /^(\w+)$/) {
		$oldfolder = $1;
	} else {
		die("Invalid old folder<BR>\n");
	}
	
	if ($newfolder =~ /^(\w+)$/) {
		$newfolder = $1;
	} else {
		die("Invalid new folder ($newfolder)<BR>\n");
	}
	
	if ($old =~ /^(\d\d\d\d)$/) {
		$old = $1;
	} else {
		die("Invalid old Message<BR>\n");
	}
	
	if ($new =~ /^(\d\d\d\d)$/) {
		$new = $1;
	} else {
		die("Invalid old Message<BR>\n");
	}
	
	my $path = "/var/spool/asterisk/voicemail/$context/$mbox/$newfolder";
	$path =~ /^(.*)$/;
	$path = $1;
	mkdir $path, 0770;
	$path = "/var/spool/asterisk/voicemail/$context/$mbox/$oldfolder";
	opendir(DIR, $path) || die("Unable to open directory\n");
	my @files = grep /^msg${old}\.\w+$/, readdir(DIR);
	closedir(DIR);
	foreach $oldfile (@files) {
		my $tmp = $oldfile;
		if ($tmp =~ /^(msg${old}.\w+)$/) {
			$tmp = $1;
			$oldfile = $path . "/$tmp";
			$tmp =~ s/msg${old}/msg${new}/;
			$newfile = "/var/spool/asterisk/voicemail/$context/$mbox/$newfolder/$tmp";
#			print "Renaming $oldfile to $newfile<BR>\n";
			rename($oldfile, $newfile);
		}
	}
}

sub file_copy()
{
	my ($orig, $new) = @_;
	my $res;
	my $data;
	$orig =~ /^(.*)$/;
	$orig = $1;
	$new =~ /^(.*)$/;
	$new = $1;
	open(IN, "<$orig") || die("Unable to open '$orig'\n");
	open(OUT, ">$new") || DIE("Unable to open '$new'\n");
	while(($res = sysread(IN, $data, 4096)) > 0) {
		syswrite(OUT, $data, $res);
	}
	close(OUT);
	close(IN);
}

sub message_copy()
{
	my ($context, $mbox, $newmbox, $oldfolder, $old, $new) = @_;
	my ($oldfile, $newfile);
	return if ($mbox eq $newmbox);
	
	if ($mbox =~ /^(\w+)$/) {
		$mbox = $1;
	} else {
		die ("Invalid mailbox<BR>\n");
	}

	if ($newmbox =~ /^(\w+)$/) {
		$newmbox = $1;
	} else {
		die ("Invalid new mailbox<BR>\n");
	}
	
	if ($oldfolder =~ /^(\w+)$/) {
		$oldfolder = $1;
	} else {
		die("Invalid old folder<BR>\n");
	}
	
	if ($old =~ /^(\d\d\d\d)$/) {
		$old = $1;
	} else {
		die("Invalid old Message<BR>\n");
	}
	
	if ($new =~ /^(\d\d\d\d)$/) {
		$new = $1;
	} else {
		die("Invalid old Message<BR>\n");
	}
	
	my $path = "/var/spool/asterisk/voicemail/$context/$newmbox";
	$path =~ /^(.*)$/;
	$path = $1;
	mkdir $path, 0770;
	$path = "/var/spool/asterisk/voicemail/$context/$newmbox/INBOX";
	$path =~ /^(.*)$/;
	$path = $1;
	mkdir $path, 0770;
	$path = "/var/spool/asterisk/voicemail/$context/$mbox/$oldfolder";
	opendir(DIR, $path) || die("Unable to open directory\n");
	my @files = grep /^msg${old}\.\w+$/, readdir(DIR);
	closedir(DIR);
	foreach $oldfile (@files) {
		my $tmp = $oldfile;
		if ($tmp =~ /^(msg${old}.\w+)$/) {
			$tmp = $1;
			$oldfile = $path . "/$tmp";
			$tmp =~ s/msg${old}/msg${new}/;
			$newfile = "/var/spool/asterisk/voicemail/$context/$newmbox/INBOX/$tmp";
#			print "Copying $oldfile to $newfile<BR>\n";
			&file_copy($oldfile, $newfile);
		}
	}
}

sub message_delete()
{
	my ($context, $mbox, $folder, $msg) = @_;
	if ($mbox =~ /^(\w+)$/) {
		$mbox = $1;
	} else {
		die ("Invalid mailbox<BR>\n");
	}
	if ($context =~ /^(\w+)$/) {
		$context = $1;
	} else {
		die ("Invalid context<BR>\n");
	}
	if ($folder =~ /^(\w+)$/) {
		$folder = $1;
	} else {
		die("Invalid folder<BR>\n");
	}
	if ($msg =~ /^(\d\d\d\d)$/) {
		$msg = $1;
	} else {
		die("Invalid Message<BR>\n");
	}
	my $path = "/var/spool/asterisk/voicemail/$context/$mbox/$folder";
	opendir(DIR, $path) || die("Unable to open directory\n");
	my @files = grep /^msg${msg}\.\w+$/, readdir(DIR);
	closedir(DIR);
	foreach $oldfile (@files) {
		if ($oldfile =~ /^(msg${msg}.\w+)$/) {
			$oldfile = $path . "/$1";
#			print "Deleting $oldfile<BR>\n";
			unlink($oldfile);
		}
	}
}

sub message_forward()
{
	my ($toindex, @msgs) = @_;
	my $folder = param('folder');
	my ($mbox, $context) = split(/\@/, param('mailbox'));
	my $newmbox = param('forwardto');
	my $msg;
	my $msgcount;
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	$newmbox =~ s/(\w+)(\s+.*)?$/$1/;
	if (!&validmailbox($context, $newmbox)) {
		die("Bah! Not a valid mailbox '$newmbox'\n");
		return "";
	}
	
	my $txt;
	$context = &untaint($context);
	$newmbox = &untaint($newmbox);
	my $path = "/var/spool/asterisk/voicemail/$context/$newmbox/INBOX";
	if ($msgs[0]) {
		if (&lock_path($path) == 0) {
			$msgcount = &msgcount($context, $newmbox, "INBOX");
			
			if ($newmbox ne $mbox) {
	#			print header;
				foreach $msg (@msgs) {
	#				print "Forwarding $msg from $mbox to $newmbox<BR>\n";
					&message_copy($context, $mbox, $newmbox, $folder, $msg, sprintf "%04d", $msgcount);
					$msgcount++;
				}
				$txt = "Forwarded messages " . join(', ', @msgs) . "to $newmbox";
			} else {
				$txt = "Can't forward messages to yourself!\n";
			}
			&unlock_path($path); 
		} else {
			$txt = "Cannot forward messages: Unable to lock path.\n";
		}
	} else {
		$txt = "Please Select Message(s) for this action.\n";
	}
	if ($toindex) {
		&message_index($folder, $txt);
	} else {
		&message_play($txt, $msgs[0]);
	}
}

sub message_delete_or_move()
{
	my ($toindex, $del, @msgs) = @_;
	my $txt;
	my $path;
	my ($y, $x);
	my $folder = param('folder');
	my $newfolder = param('newfolder') unless $del;
	$newfolder =~ s/^(\w+)\s+.*$/$1/;
	my ($mbox, $context) = split(/\@/, param('mailbox'));
	if (!$context) {
		$context = param('context');
	}
	if (!$context) {
		$context = "default";
	}
	my $passwd = param('password');
	$context = &untaint($context);
	$mbox = &untaint($mbox);
	$folder = &untaint($folder);
	$path = "/var/spool/asterisk/voicemail/$context/$mbox/$folder";
	if ($msgs[0]) {
		if (&lock_path($path) == 0) {
			my $msgcount = &msgcount($context, $mbox, $folder);
			my $omsgcount = &msgcount($context, $mbox, $newfolder) if $newfolder;
		#	print header;
			if ($newfolder ne $folder) {
				$y = 0;
				for ($x=0;$x<$msgcount;$x++) {
					my $msg = sprintf "%04d", $x;
					my $newmsg = sprintf "%04d", $y;
					if (grep(/^$msg$/, @msgs)) {
						if ($newfolder) {
							&message_rename($context, $mbox, $folder, $msg, $newfolder, sprintf "%04d", $omsgcount);
							$omsgcount++;
						} else {
							&message_delete($context, $mbox, $folder, $msg);
						}
					} else {
						&message_rename($context, $mbox, $folder, $msg, $folder, $newmsg);
						$y++;
					}
				}
				if ($del) {
					$txt = "Deleted messages "  . join (', ', @msgs);
				} else {
					$txt = "Moved messages "  . join (', ', @msgs) . " to $newfolder";
				}
			} else {
				$txt = "Can't move a message to the same folder they're in already";
			}
			&unlock_path($path);
		} else {
			$txt = "Cannot move/delete messages: Unable to lock path.\n";
		}
	} else {
		$txt = "Please Select Message(s) for this action.\n";
	}
	# Not as many messages now
	$msgcount--;
	if ($toindex || ($msgs[0] >= $msgcount)) {
		&message_index($folder, $txt);	
	} else {
		&message_play($txt, $msgs[0]);
	}
}

if (param()) {
	my $folder = param('folder');
	my $changefolder = param('changefolder');
	$changefolder =~ s/(\w+)\s+.*$/$1/;
	
	my $newfolder = param('newfolder');
	$newfolder =~ s/^(\w+)\s+.*$/$1/;
	if ($newfolder && !&validfolder($newfolder)) {
		print header;
		die("Bah! new folder '$newfolder' isn't a folder.");
	}
	$action = param('action');
	$msgid = param('msgid');
	if (!$action) {
		my ($tmp) = grep /^play\d\d\d\d\.x$/, param;
		if ($tmp =~ /^play(\d\d\d\d)/) {
			$msgid = $1;
			$action = "play";
		} else {
			print header;
			print "No message?<BR>\n";
			return;
		}
	}
	@msgs = param('msgselect');
	@msgs = ($msgid) unless @msgs;
	{
		($mailbox) = &check_login();
		if (length($mailbox)) {
			if ($action eq 'login') {
				&message_index($folder, "Welcome, $mailbox");
			} elsif (($action eq 'refresh') || ($action eq 'index')) {
				&message_index($folder, "Welcome, $mailbox");
			} elsif ($action eq 'change to ->') {
				if (&validfolder($changefolder)) {
					$folder = $changefolder;
					&message_index($folder, "Welcome, $mailbox");
				} else {
					die("Bah!  Not a valid change to folder '$changefolder'\n");
				}
			} elsif ($action eq 'play') {
				&message_play("$mailbox $folder $msgid", $msgid);
			} elsif ($action eq 'preferences') {
				&message_prefs("refresh", $msgid);
			} elsif ($action eq 'download') {
				&message_audio(1);
			} elsif ($action eq 'play ') {
				&message_audio(0);
			} elsif ($action eq 'audio') {
				&message_audio(0);
			} elsif ($action eq 'delete') {
				&message_delete_or_move(1, 1, @msgs);
			} elsif ($action eq 'delete ') {
				&message_delete_or_move(0, 1, @msgs);
			} elsif ($action eq 'forward to ->') {
				&message_forward(1, @msgs);
			} elsif ($action eq 'forward to -> ') {
				&message_forward(0, @msgs);
			} elsif ($action eq 'save to ->') {
				&message_delete_or_move(1, 0, @msgs);
			} elsif ($action eq 'save to -> ') {
				&message_delete_or_move(0, 0, @msgs);
			} elsif ($action eq 'logout') {
				&login_screen("Logged out!\n");
			}
		} else {
			sleep(1);
			&login_screen("Login Incorrect!\n");
		}
	}
} else {
	&login_screen("\&nbsp;");
}
