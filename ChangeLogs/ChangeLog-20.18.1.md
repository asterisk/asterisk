
## Change Log for Release asterisk-20.18.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.18.1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.18.0...20.18.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.18.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 1
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1739: [bug]: Regression in 23.2.0 with regard to parsing fractional numbers when system locale is non-standard

### Commits By Author:

- #### Sean Bright (1):

### Commit List:

-  asterisk.c: Use C.UTF-8 locale instead of relying on user's environment.

### Commit Details:

#### asterisk.c: Use C.UTF-8 locale instead of relying on user's environment.
  Author: Sean Bright
  Date:   2026-01-23

  Resolves: #1739

