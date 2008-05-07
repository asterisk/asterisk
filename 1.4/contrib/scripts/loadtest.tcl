#!/usr/bin/tclsh
#
# Usage (as root):
#
# $ tclsh loadtest.tcl
#
# Copyleft 2005 by Chris Maj <cmaj_at_freedomcorpse_dot_com>
#
# Create a (huge) bunch of call files to dial via pbx_spool.
# Defaults are selected with 'Enter' and, if all defaults
# are selected, you'll dial Zap/1/s into default|s|1
#


# where Asterisk's pbx/pbx_spool.c will be looking for work
set SPOOLDIR /var/spool/asterisk/outgoing
# pbx_spool is fairly aggresive, so make files here first
set TEMPDIR /tmp

if { ![file writable $SPOOLDIR] } {
	puts "Do you need to be root to write to $SPOOLDIR ?"
	exit
}

if { ![file readable $TEMPDIR] } {
	puts "Do you need to be root to read from $TEMPDIR ?"
	exit
}

if { ![file writable $TEMPDIR] } {
	puts "Do you need to be root to write to $TEMPDIR ?"
	exit
}

# gets some input from the user
proc get {var_ default_ prompt_} {
	global $var_
	puts $prompt_
	if { $default_ != "" } {
		puts -nonewline "(default: $default_) ? "
	} else {
		puts -nonewline "? "
	}
	flush stdout
	gets stdin $var_
	if { [set $var_] == "" && $default_ != "" } {
		set $var_ $default_
	}
}

# puts the user requested channels into a neat, ordered list
proc splitchans {inch_} {
	global changroup
	set outch [list]
	foreach range [split $inch_ {, }] {
		set start [lindex [split $range -] 0]
		set stop [lindex [split $range -] end]
		if { [string is digit $start] && [string is digit $stop] } {
			set ::changroup "channel"
			for {set ch $start} {$ch <= $stop} {incr ch} {
				if { [lsearch $outch $ch] == -1 } {
					lappend outch $ch
				}
			}
		} else {
			set ::changroup "group"
			foreach ch [split $range -] {
				lappend outch $ch
			}
		}
	}
	return [lsort -dictionary $outch]
}

# writes out a file in the temporary directory,
# then changes the mtime of the file before
# sticking it into the outgoing spool directory
# (where pbx_spool will be looking)
proc spool {channel_ callcnt_ when_} {
	set callstr "
Channel: $::technology/$channel_/$::destination
Context: $::context
Extension: $::extension
Priority: $::priority
WaitTime: $::timeout
RetryTime: $::retrytime
MaxRetries: $::maxretries
Callerid: $::clid
SetVar: $::astvar
Account: $::account
"
	set fn "loadtest.call$callcnt_.ch$channel_"
	set fd [open $::TEMPDIR/$fn w]
	puts $fd $callstr
	close $fd
	file mtime $::TEMPDIR/$fn $when_
	file rename -force $::TEMPDIR/$fn $::SPOOLDIR/$fn
}

# prompt the user for some info
get technology "Zap" "\nEnter technology type
Zap, IAX, SIP, etc."
get chans "1" "\nEnter channel(s) or group to test in formats like
2\n1-4\n3 5 7 9\n1-23,25-47,49-71,73-95\ng4\ng2,g1"
set channels [splitchans $chans]

get destination "s" "\nEnter destination number"
get context "default" "\nEnter context"
get extension "s" "\nEnter extension"
get priority "1" "\nEnter priority"
get timeout "45" "\nEnter timeout for call to be answered in seconds"
get maxretries "0" "\nEnter maximum number of retries"

if { $maxretries > 0 } {
	get retrytime "300" "\nEnter time between retries in seconds"
} else {
	set retrytime 300
}

get clid "" "\nEnter callerid"
get astvar "" "\nEnter some extra variables"
get account "loadtest" "\nEnter account code"
get calls "1" "\nEnter number of test calls per $changroup"
get period "60" "\nEnter period between placing calls on a particular $changroup in seconds"

if { [llength $channels] > 1 } {
	get rate "0" "\nEnter period between placing each call in seconds
0 will send a call on each $changroup every $period seconds
1 will send a call on $changroup [lindex $channels 0] at [expr {$period + 0}]s, [lindex $channels 1] at [expr {$period + 1 }]s, etc.
5 will send a call on $changroup [lindex $channels 0] at [expr {$period + 0}]s, [lindex $channels 1] at [expr {$period + 5 }]s, etc."
} else {
	set rate 0
}

puts -nonewline "\nCreating spooled call files...  "
set now [clock seconds]
set spoolcnt 0
set spinner [list / - \\ |]
for {set i 0} {$i < $calls} {incr i} {
	foreach ch $channels {
		set chidx [lsearch $channels $ch]
		spool $ch [incr spoolcnt] [expr {$now + ($i * $period) + ($rate * $chidx)}]
		puts -nonewline "\b"
		puts -nonewline [lindex $spinner [expr {$spoolcnt % 4}]]
		flush stdout
	}
}
puts "\b$spoolcnt calls placed into $SPOOLDIR !"
