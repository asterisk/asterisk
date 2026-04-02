
## Change Log for Release asterisk-22.9.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.9.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.9.0-rc1...22.9.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.9.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 2
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (1)
- nappsoft: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1844: [bug]: cdrel_custom isn't respecting the default time format for CEL records
  - 1845: [bug]:res_cdrel_custom produces wrong float timestamps
  - 1852: [bug]: res_cdrel_custom: connection to the sqlite3 database closes from time to time

### Commits By Author:

- #### George Joseph (1):
  - res_cdrel_custom: Resolve several formatting issues.

- #### nappsoft (1):
  - res_cdrel_custom: do not free config when no new config was loaded

### Commit List:

-  res_cdrel_custom: do not free config when no new config was loaded
-  res_cdrel_custom: Resolve several formatting issues.

### Commit Details:

#### res_cdrel_custom: do not free config when no new config was loaded
  Author: nappsoft
  Date:   2026-04-02

  When the res_cdrel_custom modules is reloaded and the config has not been changed asterisk should not free the old config. Otherwise the connection to the database will be closed and no new connection will be opened.

  Resolves: #1852

#### res_cdrel_custom: Resolve several formatting issues.
  Author: George Joseph
  Date:   2026-03-31

  Several issues are resolved:

  * Internally, floats were used for timestamp values but this could result
  in wrapping so they've been changed to doubles.

  * Historically, the default CEL eventtime format is `<seconds>.<microseconds>`
  with `<microseconds>` always being 6 digits.  This should have continued to be
  the case but res_cdrel_custom wasn't checking the `dateformat` setting in
  cel.conf and was defaulting to `%F %T`.  res_cdrel_custom now gets the default
  date format from cel.conf, which will be whatever the `dateformat` parameter
  is set to or `<seconds>.<microseconds>` if not set.

  * The timeval field formatter for both CDR and CEL wasn't handling custom
  strftime format strings correctly.  This is now fixed so you should be able
  to specifiy custom strftime format strings for the CEL `eventtime` and CDR
  `start`, `answer` and `end` fields.  For example: `eventtime(%FT%T%z)`.

  Resolves: #1844
  Resolves: #1845

