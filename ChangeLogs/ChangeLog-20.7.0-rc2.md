
Change Log for Release asterisk-20.7.0-rc2
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.7.0-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.7.0-rc1...20.7.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.7.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_pjsip_stir_shaken.c:  Add checks for missing parameters                     

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #645: [bug]: Occasional SEGV in res_pjsip_stir_shaken.c

Commits By Author:
----------------------------------------

- ### George Joseph (1):
  - res_pjsip_stir_shaken.c:  Add checks for missing parameters


Detail:
----------------------------------------

- ### res_pjsip_stir_shaken.c:  Add checks for missing parameters                     
  Author: George Joseph  
  Date:   2024-03-11  

  * Added checks for missing session, session->channel and rdata
    in stir_shaken_incoming_request.

  * Added checks for missing session, session->channel and tdata
    in stir_shaken_outgoing_request.

  Resolves: #645

