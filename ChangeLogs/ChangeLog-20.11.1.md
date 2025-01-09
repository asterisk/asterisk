
## Change Log for Release asterisk-20.11.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.11.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.11.0...20.11.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.11.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-33x6-fj46-6rfh](https://github.com/asterisk/asterisk/security/advisories/GHSA-33x6-fj46-6rfh): Path traversal via AMI ListCategories allows access to outside files

### User Notes:

- #### manager.c: Restrict ListCategories to the configuration directory.              
  The ListCategories AMI action now restricts files to the
  configured configuration directory.


### Upgrade Notes:


### Commit Authors:

- Ben Ford: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-33x6-fj46-6rfh: Path traversal via AMI ListCategories allows access to outside files

### Commits By Author:

- #### Ben Ford (1):
  - manager.c: Restrict ListCategories to the configuration directory.


### Commit List:

-  manager.c: Restrict ListCategories to the configuration directory.

### Commit Details:

#### manager.c: Restrict ListCategories to the configuration directory.
  Author: Ben Ford
  Date:   2024-12-17

  When using the ListCategories AMI action, it was possible to traverse
  upwards through the directories to files outside of the configured
  configuration directory. This action is now restricted to the configured
  directory and an error will now be returned if the specified file is
  outside of this limitation.

  Resolves: #GHSA-33x6-fj46-6rfh

  UserNote: The ListCategories AMI action now restricts files to the
  configured configuration directory.

