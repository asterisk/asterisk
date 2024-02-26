
Change Log for Release asterisk-certified-18.9-cert8-rc2
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-certified-18.9-cert8-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert8-rc1...certified-18.9-cert8-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-certified-18.9-cert8-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- Rename dialplan_functions.xml to dialplan_functions_doc.xml                     
- openssl: Supress deprecation warnings from OpenSSL 3.0                          

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

None

Commits By Author:
----------------------------------------

- ### George Joseph (1):
  - Rename dialplan_functions.xml to dialplan_functions_doc.xml

- ### Sean Bright (1):
  - openssl: Supress deprecation warnings from OpenSSL 3.0


Detail:
----------------------------------------

- ### Rename dialplan_functions.xml to dialplan_functions_doc.xml                     
  Author: George Joseph  
  Date:   2024-02-26  

  When using COMPILE_DOUBLE, dialplan_functions.xml is mistaken
  for the source for an embedded XML document and gets compiled
  to dialplan_functions.o.  This causes dialplan_functions.c to
  be ignored making its functions unavailable and causing chan_pjsip
  to fail to load.

- ### openssl: Supress deprecation warnings from OpenSSL 3.0                          
  Author: Sean Bright  
  Date:   2022-03-25  

  There is work going on to update our OpenSSL usage to avoid the
  deprecated functions but in the meantime make it possible to compile
  in devmode.


