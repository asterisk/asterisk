
Change Log for Release 18.19.0-rc2
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-18.19.0-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/18.19.0-rc1...18.19.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-18.19.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- app.h: Move declaration of ast_getdata_result before its first use
- doc: Remove obsolete CHANGES-staging and UPGRADE-staging

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #200: [bug]: Regression: In app.h an enum is used before its declaration.

Commits By Author:
----------------------------------------

- ### George Joseph (2):
  - doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  - app.h: Move declaration of ast_getdata_result before its first use


Detail:
----------------------------------------

- ### app.h: Move declaration of ast_getdata_result before its first use
  Author: George Joseph  
  Date:   2023-07-10  

  The ast_app_getdata() and ast_app_getdata_terminator() declarations
  in app.h were changed recently to return enum ast_getdata_result
  (which is how they were defined in app.c).  The existing
  declaration of ast_getdata_result in app.h was about 1000 lines
  after those functions however so under certain circumstances,
  a "use before declaration" error was thrown by the compiler.
  The declaration of the enum was therefore moved to before those
  functions.

  Resolves: #200

- ### doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  Author: George Joseph  
  Date:   2023-07-10  


