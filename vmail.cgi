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
use CGI qw/:standard/;

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
$footer = "<hr><font size=-1><a href=\"http://www.asterisk.org\">The Asterisk Open Source PBX</a> Copyright 2002, <a href=\"http://www.linux-support.net\">Linux Support Services, Inc.</a></a>";
$stdcontainerend = "</td></tr><tr><td align=right>$footer</td></tr></table>\n";

sub login_screen() {
	print header;
	my ($message) = @_;
	print <<_EOH;

<TITLE>Asterisk Web-Voicemail</TITLE>
<BODY BGCOLOR="white">
$stdcontainerstart
<FORM METHOD="post">
<table align=center>
<tr><td valign=top align=center rowspan=6><img align=center src="$astpath/animlogo.gif"></td></tr>
<tr><td align=center colspan=2><font size=+2>Commedian Mail Login</font></td></tr>
<tr><td align=center colspan=2><font size=+1>$message</font></td></tr>
<tr><td>Mailbox:</td><td><input type=text name="mailbox"></td></tr>
<tr><td>Password:</td><td><input type=password name="password"></td></tr>
<tr><td align=right colspan=2><input name="action" value="login" type=submit></td></tr>
</table>
</FORM>
$stdcontainerend
</BODY>\n
_EOH

}

sub check_login()
{
	my $mbox = param('mailbox');
	my $pass = param('password');
	my $category = "general";
	my @fields;
	open(VMAIL, "</etc/asterisk/voicemail.conf") || die("Bleh, no voicemail.conf");
	while(<VMAIL>) {
		chomp;
		if (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category ne "general") {
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
				if (($mbox eq $1) && ($pass eq $fields[0])) {
					return $fields[1] ? $fields[1] : "Extension $mbox";
				}
			}
		}
	}
}

sub validmailbox()
{
	my ($mbox) = @_;
	my $category = "general";
	my @fields;
	open(VMAIL, "</etc/asterisk/voicemail.conf") || die("Bleh, no voicemail.conf");
	while(<VMAIL>) {
		chomp;
		if (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category ne "general") {
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
				if ($mbox eq $1) {
					return $fields[2] ? $fields[2] : "unknown";
				}
			}
		}
	}
}

sub mailbox_list()
{
	my ($name, $current) = @_;
	my $tmp;
	my $text;
	$tmp = "<SELECT name=\"$name\">\n";
	open(VMAIL, "</etc/asterisk/voicemail.conf") || die("Bleh, no voicemail.conf");
	while(<VMAIL>) {
		chomp;
		s/\;.*$//;
		if (/\[(.*)\]/) {
			$category = $1;
		} elsif ($category ne "general") {
			if (/([^\s]+)\s*\=\>?\s*(.*)/) {
				@fields = split(/\,\s*/, $2);
				$text = "$1";
				if ($fields[2]) {
					$text .= " ($fields[1])";
				}
				if ($1 eq $current) {
					$tmp .= "<OPTION SELECTED>$text</OPTION>\n";
				} else {
					$tmp .= "<OPTION>$text</OPTION>\n";
				}
				
				if (($mbox eq $1) && ($pass eq $fields[0])) {
					return $fields[1];
				}
			}
		}
	}
	$tmp .= "</SELECT>\n";
	
}

sub msgcount() 
{
	my ($mailbox, $folder) = @_;
	my $path = "/var/spool/asterisk/vm/$mailbox/$folder";
	if (opendir(DIR, $path)) {
		my @msgs = grep(/^msg....\.txt$/, readdir(DIR));
		closedir(DIR);
		return sprintf "%d", $#msgs + 1;
	}
	return "0";
}

sub msgcountstr()
{
	my ($mailbox, $folder) = @_;
	my $count = &msgcount($mailbox, $folder);
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
	my ($mailbox, $folder) = @_;
	my $path = "/var/spool/asterisk/vm/$mailbox/$folder";
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
	cookie($var);
}

sub makecookie()
{
	my ($format) = @_;
	cookie(-name => "format", -value =>["$format"]);
}

sub getfields()
{
	my ($mailbox, $folder, $msg) = @_;
	my $fields;
	if (open(MSG, "</var/spool/asterisk/vm/$mailbox/$folder/msg${msg}.txt")) {
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
	my $mbox = param('mailbox');
	my $passwd = param('password');
	my $format = param('format');
	my $fields;
	my $folders = &folder_list('newfolder', $mbox, $folder);
	my $mailboxes = &mailbox_list('forwardto', $mbox);
	if (!$format) {
		$format = &getcookie('format');
	}
	if (!$format) {
		&message_prefs("play", $msgid);
	} else {
		print header(-cookie => &makecookie($format));
		$fields = &getfields($mbox, $folder, $msgid);
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
	<tr><td><b>Mailbox:</b></td><td>$mbox</td></tr>\n
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
<embed width=400 height=40 src="vmail.cgi?action=audio&folder=$folder&mailbox=$mbox&password=$passwd&msgid=$msgid&format=$format&dontcasheme=$$.$format" autostart=yes loop=false></embed>
</td></tr></table>
</td></tr>
</table>
<input type=hidden name="folder" value="$folder">
<input type=hidden name="mailbox" value="$mbox">
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
	my $folder = param('folder');
	my $msgid = param('msgid');
	my $mailbox = param('mailbox');
	my $format = param('format');
	if (!$format) {
		$format = &getcookie('format');
	}
	my $path = "/var/spool/asterisk/vm/$mailbox/$folder/msg${msgid}.$format";

	$msgid =~ /^\d\d\d\d$/ || die("Msgid Liar ($msgid)!");
	grep(/^${format}$/, keys %formats) || die("Format Liar ($format)!");

	# Mailbox and folder are already verified
	if (open(AUDIO, "<$path")) {
		$|=1;
		if ($forcedownload) {
			print header(-type=>$formats{$format}->{'mime'}, -attachment => "msg${msgid}.$format");
		} else {		
			print header(-type=>$formats{$format}->{'mime'});
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
	my $mbox = param('mailbox');
	my $passwd = param('password');
	my $message2;
	my $msgcount;	
	my $hasmsg;
	my $newmessages, $oldmessages;
	my $format = param('format');
	if (!$format) {
		$format = &getcookie('format');
	}
	if ($folder) {
		$msgcount = &msgcountstr($mbox, $folder);
		$message2 = "&nbsp;&nbsp;&nbsp;Folder '$folder' has " . &msgcountstr($mbox, $folder);
	} else {
		$newmessages = &msgcount($mbox, "INBOX");
		$oldmessages = &msgcount($mbox, "Old");
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
	
	my $folders = &folder_list('newfolder', $mbox, $folder);
	my $cfolders = &folder_list('changefolder', $mbox, $folder);
	my $mailboxes = &mailbox_list('forwardto', $mbox);
	print header(-cookie => &makecookie($format));
	print <<_EOH;

<TITLE>Asterisk Web-Voicemail: $mbox $folder</TITLE>
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
foreach $msg (&messages($mbox, $folder)) {

	$fields = &getfields($mbox, $folder, $msg);
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
	my ($name, $mbox, $selected) = @_;
	my $f;
	my $count;
	my $tmp = "<SELECT name=\"$name\">\n";
	foreach $f (@validfolders) {
		$count =  &msgcount($mbox, $f);
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
	my ($mbox, $oldfolder, $old, $newfolder, $new) = @_;
	my $oldfile, $newfile;
	return if ($old eq $new) && ($oldfolder eq $newfolder);
	
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
	
	my $path = "/var/spool/asterisk/vm/$mbox/$newfolder";
	mkdir $path, 0755;
	my $path = "/var/spool/asterisk/vm/$mbox/$oldfolder";
	opendir(DIR, $path) || die("Unable to open directory\n");
	my @files = grep /^msg${old}\.\w+$/, readdir(DIR);
	closedir(DIR);
	foreach $oldfile (@files) {
		my $tmp = $oldfile;
		if ($tmp =~ /^(msg${old}.\w+)$/) {
			$tmp = $1;
			$oldfile = $path . "/$tmp";
			$tmp =~ s/msg${old}/msg${new}/;
			$newfile = "/var/spool/asterisk/vm/$mbox/$newfolder/$tmp";
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
	my ($mbox, $oldfolder, $old, $newmbox, $new) = @_;
	my $oldfile, $newfile;
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
	
	my $path = "/var/spool/asterisk/vm/$newmbox";
	mkdir $path, 0755;
	my $path = "/var/spool/asterisk/vm/$newmbox/INBOX";
	mkdir $path, 0755;
	my $path = "/var/spool/asterisk/vm/$mbox/$oldfolder";
	opendir(DIR, $path) || die("Unable to open directory\n");
	my @files = grep /^msg${old}\.\w+$/, readdir(DIR);
	closedir(DIR);
	foreach $oldfile (@files) {
		my $tmp = $oldfile;
		if ($tmp =~ /^(msg${old}.\w+)$/) {
			$tmp = $1;
			$oldfile = $path . "/$tmp";
			$tmp =~ s/msg${old}/msg${new}/;
			$newfile = "/var/spool/asterisk/vm/$newmbox/INBOX/$tmp";
#			print "Copying $oldfile to $newfile<BR>\n";
			&file_copy($oldfile, $newfile);
		}
	}
}

sub message_delete()
{
	my ($mbox, $folder, $msg) = @_;
	if ($mbox =~ /^(\w+)$/) {
		$mbox = $1;
	} else {
		die ("Invalid mailbox<BR>\n");
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
	my $path = "/var/spool/asterisk/vm/$mbox/$folder";
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
	my $mbox = param('mailbox');
	my $newmbox = param('forwardto');
	my $msg;
	my $msgcount;
	$newmbox =~ s/(\w+)(\s+.*)?$/$1/;
	if (!&validmailbox($newmbox)) {
		die("Bah! Not a valid mailbox '$newmbox'\n");
		return "";
	}
	$msgcount = &msgcount($newmbox, "INBOX");
	my $txt;
	if ($newmbox ne $mbox) {
#		print header;
		foreach $msg (@msgs) {
#			print "Forwarding $msg from $mbox to $newmbox<BR>\n";
			&message_copy($mbox, $folder, $msg, $newmbox, sprintf "%04d", $msgcount);
			$msgcount++;
		}
		$txt = "Forwarded messages " . join(', ', @msgs) . "to $newmbox";
	} else {
		$txt = "Can't forward messages to yourself!\n";
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
	my $y, $x;
	my $folder = param('folder');
	my $newfolder = param('newfolder') unless $del;
	$newfolder =~ s/^(\w+)\s+.*$/$1/;
	my $mbox = param('mailbox');
	my $passwd = param('password');
	my $msgcount = &msgcount($mbox, $folder);
	my $omsgcount = &msgcount($mbox, $newfolder) if $newfolder;
#	print header;
	if ($newfolder ne $folder) {
		$y = 0;
		for ($x=0;$x<$msgcount;$x++) {
			my $msg = sprintf "%04d", $x;
			my $newmsg = sprintf "%04d", $y;
			if (grep(/^$msg$/, @msgs)) {
				if ($newfolder) {
					&message_rename($mbox, $folder, $msg, $newfolder, sprintf "%04d", $omsgcount);
					$omsgcount++;
				} else {
					&message_delete($mbox, $folder, $msg);
				}
			} else {
				&message_rename($mbox, $folder, $msg, $folder, $newmsg);
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
		$mailbox = check_login();
		if ($mailbox) {
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
