#!/usr/bin/perl -w 
#
#  Simple Asterisk Manager Proxy, Version 1.01
#  2004-09-26
#  Copyright (c) 2004 David C. Troy &lt;dave@popvox.com>
#
#  This code is based on Flash Operator Panel 'op_server.pl'
#  by Nicolá³ Gudiñ¯¬
#   Copyright (C) 2004.
#
#  David C. Troy &lt;dave@popvox.com>
#  Nicolá³ Gudiñ¯ ¼nicolas@house.com.ar>
#
#  This program is free software, distributed under the terms of
#  the GNU General Public License.
#
#  Security consideration: This script will open your manager port
#  for unauthenticated logins. Be careful out there :-)
#############################

#############################
# Perl Prerequisites
#############################
use strict;
use IO::Socket;
use IO::Select;
use POSIX qw(setsid);

#############################
# User Configurable Options
#############################
# Configuration for logging in to your asterisk server
# Check you Asterisk config file "manager.conf" for details
my $manager_host = '127.0.0.1';
my $manager_port = 5038;
my $manager_user = 'your_username';
my $manager_secret = 'your_secret';
# Port For this proxy
my $listen_port = 1234;
my $manager_pid = "/var/run/asterisk_managerproxy.pid";

#############################
# Declarations
#############################
my %proxy_clients;
my $O;
my $p;
my @S;
my %blocks;
my $debug = 0;

$SIG{PIPE} = 'IGNORE';
$SIG{INT}  = 'close_all';
$SIG{USR1} = 'list_clients';

if (defined($ARGV[0]))
{
    if ($ARGV[0] eq "-d")
    {
        defined(my $pid = fork) or die "Can't Fork: $!";
        exit if $pid;
        setsid or die "Can't start a new session: $!";
        open MYPIDFILE, ">$manager_pid";
        print MYPIDFILE $$;
        close MYPIDFILE;
    }
} else {
   $debug = 1;
}


# Connect to manager
$p =
  new IO::Socket::INET->new(
                            PeerAddr => $manager_host,
                            PeerPort => $manager_port,
                            Proto    => "tcp",
                            Type     => SOCK_STREAM
                           )
  or die "\nCould not connect to Asterisk Manager Port at $manager_host\n";

$p->autoflush(1);

# Login to Manager
send_command_to_manager( "Action: Login\r\nUsername: $manager_user\r\nSecret: $manager_secret\r\n\r\n" );

# Start up listener for new connections
my $m =
  new IO::Socket::INET(Listen => 1, LocalPort => $listen_port, ReuseAddr => 1)
  or die "\nCan't listen to port $listen_port\n";
$O = new IO::Select();
$O->add($m);
$O->add($p);
$/ = "\0";

sub manager_reconnect()
{
    my $attempt        = 1;
    my $total_attempts = 60;

    do
    {
        log_debug("** Attempt reconnection to manager port # $attempt", 16);
        $p =
          new IO::Socket::INET->new(
                                    PeerAddr => $manager_host,
                                    PeerPort => $manager_port,
                                    Proto    => "tcp",
                                    Type     => SOCK_STREAM
                                   );
        $attempt++;
        if ($attempt > $total_attempts)
        {
            die("!! Could not reconnect to Asterisk Manager port");
        }
        sleep(10);    # wait 10 seconds before trying to reconnect
    } until $p;
    $O->add($p);
    send_command_to_manager(
        "Action: Login\r\nUsername: $manager_user\r\nSecret: $manager_secret\r\n\r\n"
    );
}

# Loop, continuously processing new connections, input from those connections, and input from Manager conn
while (1)
{
    while (@S = $O->can_read)
    {
        foreach (@S)
        {
            if ($_ == $m)
            {
                log_debug("** New client connection", 16);
                my $C = $m->accept;
                $proxy_clients{$C} = \$C;                
                print "New Connection: $C\n" if $debug;
                $O->add($C);
            } else {
                # It's not a new client connection
                my $i;
                my $R = sysread($_, $i, 2);    # 2048; interleave every two bytes?
                if (defined($R) && $R == 0)
                {
                    # Confirm it's really dead by trying to write to it?
                    my $T = syswrite($_, ' ', 2);    # 2048
                    if (!defined($T))
                    {
                        # connection went away...
                        $O->remove($_);
                        $_->close;

                        # If we lost the socket for the Asterisk Mgr, then reconnect
                        if ($_ == $p)
                        {
                            log_debug(
                                     "** Asterisk Manager connection lost!!!!!",
                                     16);
                            manager_reconnect();
                        } else {
                            # Remove handle from proxy_clients hash
                            print "Closed Connection: $_\n" if $debug;
                            delete $proxy_clients{$_};
                        }
                    }
                }
                else  # Socket is active and we are ready to read something from it
                {
                    $blocks{$_} .= $i;
                    next if ($blocks{$_} !~ /\r\n\r\n$/);
                    # do a 'next' unless we have completed a block; we are not ready to continue

                    # Process the completed block
                    # If block is from asterisk, send to clients
                    if ($_ == $p) {
                       # block is from asterisk, send to clients
                       print "asterisk: $_\n$blocks{$_}" if $debug;
                       my $cnt = 0;
                       foreach my $client (values %proxy_clients) {
                          print "writing to $$client...\n" if $debug;
                          syswrite($$client, $blocks{$_});
                          $cnt++;
                       }
                       print "sent block to $cnt clients\n" if $debug;
                    } else {
                       # Blocks are from clients, send to asterisk
                       syswrite($p, $blocks{$_});
                       print "client: $_\n$blocks{$_}\n" if $debug;
                    }
                    delete $blocks{$_};

                } # end if read succeeded
            } # end if new client connection
        }    # end foreach @S -> can read
    }    # while can read
}    # endless loop

sub close_all
{
    log_debug("Exiting...", 0);

    foreach my $hd ($O->handles)
    {
        $O->remove($hd);
        close($hd);
    }

    exit(0);
}

sub send_command_to_manager
{
    my $comando = shift;
    if (defined $p)
    {
        my @lineas = split("\r\n", $comando);
        foreach my $linea (@lineas)
        {
            syswrite($p, "$linea\r\n");
            log_debug("-> $linea", 2);
        }
        log_debug(" ", 2);
        syswrite($p, "\r\n");
    }
}

sub log_debug
{
    my $texto = shift;
    $texto =~ s/\0//g;
    print "$texto\n" if $debug;
}

sub list_clients()
{
   my $cnt = 0;
   foreach my $client (values %proxy_clients) {
      print "client: $$client\n";
      $cnt++;
   }
   print "$cnt clients.\n\n";
}

