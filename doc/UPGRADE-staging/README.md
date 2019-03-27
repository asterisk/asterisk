## **DO NOT REMOVE THIS FILE!**

The only files that should be added to this directory are ones that will be
used by the release script to update the UPGRADE.txt file automatically. The
only time that it is necessary to add something to the UPGRADE-staging directory
is if you are making a breaking change to an existing feature in Asterisk. The
file does not need to have a meaningful name, but it probably should. If there
are multiple items that need documenting, each can be separated with a subject
line, which should always start with "Subject:", followed by the subject of the
change. This is case sensitive! For example, if you are making a change to PJSIP,
then you might add the file "res_pjsip_breaking_change" to this directory, with
a short description of what it does. If you are adding multiple entries, they
should be done in the same commit to avoid merge conflicts. Here's an example:

> Subject: res_pjsip
>
> Here's a pretty good description of what I changed that explains exactly what
> it does and why it breaks things (and why they needed to be broken).
>
> Subject: core
> Master-Only: true
>
> Here's another description of something else I added that is a big enough
> change to warrant another entry in the UPDATE.txt file.

Note that the second subject has another header: "Master-Only". Changes that go
into the master branch and ONLY the master branch are the only ones that should
have this header. Also, the value can only be "true" or "True". The
"Master-Only" part of the header IS case-sensitive, however!

For more information, check out the wiki page:
https://wiki.asterisk.org/wiki/display/AST/CHANGES+and+UPGRADE.txt
