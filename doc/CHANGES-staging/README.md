## **DO NOT REMOVE THIS FILE!**

The only files that should be added to this directory are ones that will be
used by the release script to update the CHANGES file automatically. The only
time that it is necessary to add something to the CHANGES-staging directory is
if you are either adding a new feature to Asterisk or adding new functionality
to an existing feature. The file does not need to have a meaningful name, but
it probably should. If there are multiple items that need documenting, you can
add multiple files, each with their own description. If the message is going to
be the same for each subject, then you can add multiple subject headers to one
file. The "Subject: xxx" line is case sensitive! For example, if you are making
a change to PJSIP, then you might add the file "res_pjsip_my_cool_feature.txt" to
this directory, with a short description of what it does.  The files must  have
the ".txt" suffix.  If you are adding multiple entries, they should be done in
the same commit to avoid merge conflicts. Here's an example:

> Subject: res_pjsip
> Subject: Core
>
> Here's a pretty good description of my new feature that explains exactly what
> it does and how to use it.

Here's a master-only example:

> Subject: res_ari
> Master-Only: True
>
> This change will only go into the master branch. The "Master-Only" header
> will never be in a change not in master.

Note that the second subject has another header: "Master-Only". Changes that go
into the master branch and ONLY the master branch are the only ones that should
have this header. Also, the value can only be "true" or "True". The
"Master-Only" part of the header IS case-sensitive, however!

For more information, check out the wiki page:
https://wiki.asterisk.org/wiki/display/AST/CHANGES+and+UPGRADE.txt
