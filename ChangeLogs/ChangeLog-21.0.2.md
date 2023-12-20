
Change Log for Release asterisk-21.0.2
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.0.2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.0.1...21.0.2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.0.2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_rtp_asterisk: Fix regression issues with DTLS client check

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #500: [bug regression]: res_rtp_asterisk doesn't build if pjproject isn't used
  - #503: [bug]: The res_rtp_asterisk DTLS check against ICE candidates fails when it shouldn't
  - #505: [bug]: res_pjproject: ast_sockaddr_cmp() always fails on sockaddrs created by ast_sockaddr_from_pj_sockaddr()

Commits By Author:
----------------------------------------

- ### George Joseph (1):
  - res_rtp_asterisk: Fix regression issues with DTLS client check


Detail:
----------------------------------------

- ### res_rtp_asterisk: Fix regression issues with DTLS client check
  Author: George Joseph  
  Date:   2023-12-15  

  * Since ICE candidates are used for the check and pjproject is
    required to use ICE, res_rtp_asterisk was failing to compile
    when pjproject wasn't available.  The check is now wrapped
    with an #ifdef HAVE_PJPROJECT.

  * The rtp->ice_active_remote_candidates container was being
    used to check the address on incoming packets but that
    container doesn't contain peer reflexive candidates discovered
    during negotiation. This was causing the check to fail
    where it shouldn't.  We now check against pjproject's
    real_ice->rcand array which will contain those candidates.

  * Also fixed a bug in ast_sockaddr_from_pj_sockaddr() where
    we weren't zeroing out sin->sin_zero before returning.  This
    was causing ast_sockaddr_cmp() to always return false when
    one of the inputs was converted from a pj_sockaddr, even
    if both inputs had the same address and port.

  Resolves: #500
  Resolves: #503
  Resolves: #505

