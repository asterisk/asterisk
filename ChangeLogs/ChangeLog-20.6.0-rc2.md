
Change Log for Release asterisk-20.6.0-rc2
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.6.0-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.6.0-rc1...20.6.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.6.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- logger: Fix linking regression.

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #539: [bug]: Existence of logger.xml causes linking failure

Commits By Author:
----------------------------------------

- ### Naveen Albert (1):
  - logger: Fix linking regression.


Detail:
----------------------------------------

- ### logger: Fix linking regression.
  Author: Naveen Albert  
  Date:   2024-01-16  

  Commit 008731b0a4b96c4e6c340fff738cc12364985b64
  caused a regression by resulting in logger.xml
  being compiled and linked into the asterisk
  binary in lieu of logger.c on certain platforms
  if Asterisk was compiled in dev mode.

  To fix this, we ensure the file has a unique
  name without the extension. Most existing .xml
  files have been named differently from any
  .c files in the same directory or did not
  pose this issue.

  channels/pjsip/dialplan_functions.xml does not
  pose this issue but is also being renamed
  to adhere to this policy.

  Resolves: #539

