
Change Log for Release 19.8.1
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-19.8.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/19.8.0...19.8.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-19.8.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- apply_patches: Use globbing instead of file/sort.
- bundled_pjproject: Backport 2 SSL patches from upstream
- bundled_pjproject: Backport security fixes from pjproject 2.13.1
- apply_patches: Sort patch list before applying

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #188: [improvement]:  pjsip: Upgrade bundled version to pjproject 2.13.1 #187 
  - #193: [bug]: third-party/apply-patches doesn't sort the patch file list before applying
  - #194: [bug]: Segfault/double-free in bundled pjproject using TLS transport

Commits By Author:
----------------------------------------

- ### George Joseph (3):
  - apply_patches: Sort patch list before applying
  - bundled_pjproject: Backport security fixes from pjproject 2.13.1
  - bundled_pjproject: Backport 2 SSL patches from upstream

- ### Sean Bright (1):
  - apply_patches: Use globbing instead of file/sort.


Detail:
----------------------------------------

- ### apply_patches: Use globbing instead of file/sort.
  Author: Sean Bright  
  Date:   2023-07-06  

  This accomplishes the same thing as a `find ... | sort` but with the
  added benefit of clarity and avoiding a call to a subshell.

  Additionally drop the -s option from call to patch as it is not POSIX.

- ### bundled_pjproject: Backport 2 SSL patches from upstream
  Author: George Joseph  
  Date:   2023-07-06  

  * Fix double free of ossock->ossl_ctx in case of errors
  https://github.com/pjsip/pjproject/commit/863629bc65d6

  * free SSL context and reset context pointer when setting the cipher
    list fails
  https://github.com/pjsip/pjproject/commit/0fb32cd4c0b2

  Resolves: #194

- ### bundled_pjproject: Backport security fixes from pjproject 2.13.1
  Author: George Joseph  
  Date:   2023-07-05  

  Merge-pull-request-from-GHSA-9pfh-r8x4-w26w.patch
  Merge-pull-request-from-GHSA-cxwq-5g9x-x7fr.patch
  Locking-fix-so-that-SSL_shutdown-and-SSL_write-are-n.patch
  Don-t-call-SSL_shutdown-when-receiving-SSL_ERROR_SYS.patch

  Resolves: #188

- ### apply_patches: Sort patch list before applying
  Author: George Joseph  
  Date:   2023-07-06  

  The apply_patches script wasn't sorting the list of patches in
  the "patches" directory before applying them. This left the list
  in an indeterminate order. In most cases, the list is actually
  sorted but rarely, they can be out of order and cause dependent
  patches to fail to apply.

  We now sort the list but the "sort" program wasn't in the
  configure scripts so we needed to add that and regenerate
  the scripts as well.

  Resolves: #193

