
## Change Log for Release asterisk-20.9.2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.9.2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.9.1...20.9.2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.9.2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-c4cg-9275-6w44](https://github.com/asterisk/asterisk/security/advisories/GHSA-c4cg-9275-6w44): Write=originate, is sufficient permissions for code execution / System() dialplan

### User Notes:


### Upgrade Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-c4cg-9275-6w44: Write=originate, is sufficient permissions for code execution / System() dialplan

### Commits By Author:

- #### George Joseph (1):
  - manager.c: Add entries to Originate blacklist


### Commit List:

-  manager.c: Add entries to Originate blacklist

### Commit Details:

#### manager.c: Add entries to Originate blacklist
  Author: George Joseph
  Date:   2024-07-22

  Added Reload and DBdeltree to the list of dialplan application that
  can't be executed via the Originate manager action without also
  having write SYSTEM permissions.

  Added CURL, DB*, FILE, ODBC and REALTIME* to the list of dialplan
  functions that can't be executed via the Originate manager action
  without also having write SYSTEM permissions.

  If the Queue application is attempted to be run by the Originate
  manager action and an AGI parameter is specified in the app data,
  it'll be rejected unless the manager user has either the AGI or
  SYSTEM permissions.

  Resolves: #GHSA-c4cg-9275-6w44

