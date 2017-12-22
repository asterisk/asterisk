#!/bin/bash

# manager.conf:
#
# [general]
# ...
# allowmultipleconnects=no
# ...
#
# [russell]
# secret=blah123
# read = system,call,log,verbose,command,agent,user,config
# write = system,call,log,verbose,command,agent,user,config
# deny=0.0.0.0/0.0.0.0
# permit=127.0.0.1/255.255.255.255
#
# [russell2]
# secret=blah123
# read = system,call,log,verbose,command,agent,user,config
# write = system,call,log,verbose,command,agent,user,config
# deny=127.0.0.1/255.255.255.255

# Invalid User
printf "Action: Login\r\nUsername: foo\r\nSecret: moo\r\n\r\n" | nc localhost 5038

# Invalid Secret
printf "Action: Login\r\nUsername: russell\r\nSecret: moo\r\n\r\n" | nc localhost 5038

# Auth Success
printf "Action: Login\r\nUsername: russell\r\nSecret: blah123\r\n\r\n" | nc -w 1 localhost 5038

# Failed ACL
printf "Action: Login\r\nUsername: russell2\r\nSecret: blah123\r\n\r\n" | nc -w 1 localhost 5038

# Request Not Allowed
printf "Action: Login\r\nUsername: russell\r\nSecret: blah123\r\n\r\nAction: Originate\r\n\r\n" | nc -w 1 localhost 5038

# Request Bad Format
printf "Action: Login\r\nUsername: russell\r\nSecret: blah123\r\n\r\nAction: FakeActionBLAH\r\n\r\n" | nc -w 1 localhost 5038

# Failed Challenge Response
printf "Action: Challenge\r\nUsername: russell\r\nAuthType: MD5\r\n\r\nAction: Login\r\nUsername: russell\r\nAuthType: MD5\r\nKey: 00000000\r\n\r\n" | nc localhost 5038

# Session Limit
printf "Action: Login\r\nUsername: russell\r\nSecret: blah123\r\n\r\n" | nc -w 5 localhost 5038 &
printf "Action: Login\r\nUsername: russell\r\nSecret: blah123\r\n\r\n" | nc -w 1 localhost 5038
