
## Change Log for Release asterisk-certified-20.7-cert8

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert8.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert7...certified-20.7-cert8)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert8.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 7
- Commit Authors: 3
- Issues Resolved: 7
- Security Advisories Resolved: 0

### User Notes:

- #### res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
  The AMI command sorcery memory cache populate will now
  return an error if there is an internal error performing the populate.
  The CLI command will display an error in this case as well.

- #### res_geolocation:  Fix multiple issues with XML generation.
  Geolocation: Two new optional profile parameters have been added.
  * `pidf_element_id` which sets the value of the `id` attribute on the top-level
    PIDF-LO `device`, `person` or `tuple` elements.
  * `device_id` which sets the content of the `<deviceID>` element.
  Both parameters can include channel variables.


### Upgrade Notes:

- #### res_geolocation:  Fix multiple issues with XML generation.
  Geolocation: In order to correct bugs in both code and
  documentation, the following changes to the parameters for GML geolocation
  locations are now in effect:
  * The documented but unimplemented `crs` (coordinate reference system) element
    has been added to the location_info parameter that indicates whether the `2d`
    or `3d` reference system is to be used. If the crs isn't valid for the shape
    specified, an error will be generated. The default depends on the shape
    specified.
  * The Circle, Ellipse and ArcBand shapes MUST use a `2d` crs.  If crs isn't
    specified, it will default to `2d` for these shapes.
    The Sphere, Ellipsoid and Prism shapes MUST use a `3d` crs. If crs isn't
    specified, it will default to `3d` for these shapes.
    The Point and Polygon shapes may use either crs.  The default crs is `2d`
    however so if `3d` positions are used, the crs must be explicitly set to `3d`.
  * The `geoloc show gml_shape_defs` CLI command has been updated to show which
    coordinate reference systems are valid for each shape.
  * The `pos3d` element has been removed in favor of allowing the `pos` element
    to include altitude if the crs is `3d`.  The number of values in the `pos`
    element MUST be 2 if the crs is `2d` and 3 if the crs is `3d`.  An error
    will be generated for any other combination.
  * The angle unit-of-measure for shapes that use angles should now be included
    in the respective parameter.  The default is `degrees`. There were some
    inconsistent references to `orientation_uom` in some documentation but that
    parameter never worked and is now removed.  See examples below.
  Examples...
  ```
    location_info = shape="Sphere", pos="39.0 -105.0 1620", radius="20"
    location_info = shape="Point", crs="3d", pos="39.0 -105.0 1620"
    location_info = shape="Point", pos="39.0 -105.0"
    location_info = shape=Ellipsoid, pos="39.0 -105.0 1620", semiMajorAxis="20"
                  semiMinorAxis="10", verticalAxis="0", orientation="25 degrees"
    pidf_element_id = ${CHANNEL(name)}-${EXTEN}
    device_id = mac:001122334455
    Set(GEOLOC_PROFILE(pidf_element_id)=${CHANNEL(name)}/${EXTEN})
  ```


### Developer Notes:


### Commit Authors:

- George Joseph: (4)
- Mike Bradeen: (2)
- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1259: [bug]: New TenantID feature doesn't seem to set CDR for incoming calls
  - 1349: [bug]: Race condition on redirect can cause missing Diversion header
  - 1539: [bug]: safe_asterisk without TTY doesn't log to file
  - 1554: [bug]: safe_asterisk recurses into subdirectories of startup.d after f97361
  - 1667: [bug]: Multiple geolocation issues with rendering XML
  - 1681: [bug]: stasis/control.c: Memory leak of hangup_time in set-timeout
  - 1700: [improvement]: Improve sorcery cache populate

### Commits By Author:

- #### George Joseph (4):

- #### Mike Bradeen (2):

- #### Sean Bright (1):

### Commit List:

-  res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
-  res_geolocation:  Fix multiple issues with XML generation.
-  stasis/control.c: Add destructor to timeout_datastore.
-  safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
-  safe_asterisk:  Fix logging and sorting issue.
-  res_pjsip_diversion: resolve race condition between Diversion header processing and redirect
-  cdr.c: Set tenantid from party_a->base instead of chan->base.

### Commit Details:

#### res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
  Author: Mike Bradeen
  Date:   2026-01-06

  Reduce cache lock time for AMI and CLI sorcery memory cache populate
  commands by adding a new populate_lock to the sorcery_memory_cache
  struct which is locked separately from the existing cache lock so that
  the cache lock can be maintained for a reduced time, locking only when
  the cache objects are removed and re-populated.

  Resolves: #1700

  UserNote: The AMI command sorcery memory cache populate will now
  return an error if there is an internal error performing the populate.
  The CLI command will display an error in this case as well.

#### res_geolocation:  Fix multiple issues with XML generation.
  Author: George Joseph
  Date:   2025-12-17

  * 3d positions were being rendered without an enclosing `<gml:pos>`
    element resulting in invalid XML.
  * There was no way to set the `id` attribute on the enclosing `tuple`, `device`
    and `person` elements.
  * There was no way to set the value of the `deviceID` element.
  * Parsing of degree and radian UOMs was broken resulting in them appearing
    outside an XML element.
  * The UOM schemas for degrees and radians were reversed.
  * The Ellipsoid shape was missing and the Ellipse shape was defined multiple
    times.
  * The `crs` location_info parameter, although documented, didn't work.
  * The `pos3d` location_info parameter appears in some documentation but
    wasn't being parsed correctly.
  * The retransmission-allowed and retention-expiry sub-elements of usage-rules
    were using the `gp` namespace instead of the `gbp` namespace.

  In addition to fixing the above, several other code refactorings were
  performed and the unit test enhanced to include a round trip
  XML -> eprofile -> XML validation.

  Resolves: #1667

  UserNote: Geolocation: Two new optional profile parameters have been added.
  * `pidf_element_id` which sets the value of the `id` attribute on the top-level
    PIDF-LO `device`, `person` or `tuple` elements.
  * `device_id` which sets the content of the `<deviceID>` element.
  Both parameters can include channel variables.

  UpgradeNote: Geolocation: In order to correct bugs in both code and
  documentation, the following changes to the parameters for GML geolocation
  locations are now in effect:
  * The documented but unimplemented `crs` (coordinate reference system) element
    has been added to the location_info parameter that indicates whether the `2d`
    or `3d` reference system is to be used. If the crs isn't valid for the shape
    specified, an error will be generated. The default depends on the shape
    specified.
  * The Circle, Ellipse and ArcBand shapes MUST use a `2d` crs.  If crs isn't
    specified, it will default to `2d` for these shapes.
    The Sphere, Ellipsoid and Prism shapes MUST use a `3d` crs. If crs isn't
    specified, it will default to `3d` for these shapes.
    The Point and Polygon shapes may use either crs.  The default crs is `2d`
    however so if `3d` positions are used, the crs must be explicitly set to `3d`.
  * The `geoloc show gml_shape_defs` CLI command has been updated to show which
    coordinate reference systems are valid for each shape.
  * The `pos3d` element has been removed in favor of allowing the `pos` element
    to include altitude if the crs is `3d`.  The number of values in the `pos`
    element MUST be 2 if the crs is `2d` and 3 if the crs is `3d`.  An error
    will be generated for any other combination.
  * The angle unit-of-measure for shapes that use angles should now be included
    in the respective parameter.  The default is `degrees`. There were some
    inconsistent references to `orientation_uom` in some documentation but that
    parameter never worked and is now removed.  See examples below.
  Examples...
  ```
    location_info = shape="Sphere", pos="39.0 -105.0 1620", radius="20"
    location_info = shape="Point", crs="3d", pos="39.0 -105.0 1620"
    location_info = shape="Point", pos="39.0 -105.0"
    location_info = shape=Ellipsoid, pos="39.0 -105.0 1620", semiMajorAxis="20"
                  semiMinorAxis="10", verticalAxis="0", orientation="25 degrees"
    pidf_element_id = ${CHANNEL(name)}-${EXTEN}
    device_id = mac:001122334455
    Set(GEOLOC_PROFILE(pidf_element_id)=${CHANNEL(name)}/${EXTEN})
  ```

#### stasis/control.c: Add destructor to timeout_datastore.
  Author: George Joseph
  Date:   2025-12-31

  The timeout_datastore was missing a destructor resulting in a leak
  of 16 bytes for every outgoing ARI call.

  Resolves: #1681

#### safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
  Author: Sean Bright
  Date:   2025-10-22

  * Using `==` with the POSIX sh `test` utility is UB.
  * Switch back to using globs instead of using `$(find … | sort)`.
  * Fix a missing redirect when checking for the OS type.

  Resolves: #1554

#### safe_asterisk:  Fix logging and sorting issue.
  Author: George Joseph
  Date:   2025-10-17

  Re-enabled "TTY=9" which was erroneously disabled as part of a recent
  security fix and removed another logging "fix" that was added.

  Also added a sort to the "find" that enumerates the scripts to be sourced so
  they're sourced in the correct order.

  Resolves: #1539

#### res_pjsip_diversion: resolve race condition between Diversion header processing and redirect
  Author: Mike Bradeen
  Date:   2025-08-07

  Based on the firing order of the PJSIP call-backs on a redirect, it was possible for
  the Diversion header to not be included in the outgoing 181 response to the UAC and
  the INVITE to the UAS.

  This change moves the Diversion header processing to an earlier PJSIP callback while also
  preventing the corresponding update that can cause a duplicate 181 response when processing
  the header at that time.

  Resolves: #1349

#### cdr.c: Set tenantid from party_a->base instead of chan->base.
  Author: George Joseph
  Date:   2025-07-17

  The CDR tenantid was being set in cdr_object_alloc from the channel->base
  snapshot.  Since this happens at channel creation before the dialplan is even
  reached, calls to `CHANNEL(tenantid)=<something>` in the dialplan were being
  ignored.  Instead we now take tenantid from party_a when
  cdr_object_create_public_records() is called which is after the call has
  ended and all channel snapshots rebuilt.  This is exactly how accountcode
  and amaflags, which can also be set in tha dialplpan, are handled.

  Resolves: #1259

