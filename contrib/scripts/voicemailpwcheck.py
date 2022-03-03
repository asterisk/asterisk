#!/usr/bin/env python
''' Sample externpasscheck script for use with voicemail.conf

Copyright (C) 2010, Digium, Inc.
Russell Bryant <russell@digium.com>

The externpasscheck option in voicemail.conf allows an external script to
validate passwords when a user is changing it.  The script can enforce password
strength rules.  This script is an example of doing so and implements a check
on password length, a password with too many identical consecutive numbers, or
a password made up of sequential digits.
'''

import sys
import re


# Set this to the required minimum length for a password
REQUIRED_LENGTH = 6


# Regular expressions that match against invalid passwords
REGEX_BLACKLIST = [
    ("(?P<digit>\d)(?P=digit){%d}" % (REQUIRED_LENGTH - 1),
        "%d consecutive numbers that are the same" % REQUIRED_LENGTH)
]


# Exact passwords that are forbidden.  If the string of digits specified here
# is found in any part of the password specified, it is considered invalid.
PW_BLACKLIST = [
    "123456",
    "234567",
    "345678",
    "456789",
    "567890",
    "098765",
    "987654",
    "876543",
    "765432",
    "654321"
]


mailbox, context, old_pw, new_pw = sys.argv[1:5]

# Enforce a password length of at least 6 characters
if len(new_pw) < REQUIRED_LENGTH:
    print("INVALID: Password is too short (%d) - must be at least %d" % \
            (len(new_pw), REQUIRED_LENGTH))
    sys.exit(0)

for regex, error in REGEX_BLACKLIST:
    if re.search(regex, new_pw):
        print("INVALID: %s" % error)
        sys.exit(0)

for pw in PW_BLACKLIST:
    if new_pw.find(pw) != -1:
        print("INVALID: %s is forbidden in a password" % pw)
        sys.exit(0)

print("VALID")

sys.exit(0)
