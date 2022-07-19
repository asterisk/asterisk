{section:border=false}
{column:width=70%}

h1. Introduction
All compliant participants are required to support GML as the description language but it's really only suitable for mobile devices.  As stated earlier though, you and your partners must agree on which description formats are acceptable.

The language itself is fairly simple.  There are 8 shapes that can be used to describe a location and they share a common set of attributes described below.  Determining the actual values for those attributes though can be quite complex and is not covered here.

h2. References:
* [Open Geospatial Consortium Geography Markup Language|gml]
* [GML 3.1.1 PIDF-LO Shape Application Schema|geoshape]
* [Universal Geographical Area Description (GAD)|gad] (for background)

h2. Coordinate Reference Systems
The coordinate reference system (crs) for a shape specifies whether the points that define a shape express a two dimensional or three dimensional point in space.  It does NOT specify whether the shape itself is 2D or 3D.  For instance, a Point is a one dimensional "shape" but it can be specified with just a latitude and longitude (2d) or latitude, longitude and altitude (3d).  The `crs` is specified for each shape with the `crs` attribute whose value can be either `2d` or `3d`.

h2. Units of Measure
h3. Position
Positions are always specified in decimal degrees latitude and longitude.  A 3d position adds the altitude in meters.  `pos` and `posList` are the two attributes that specify position.
h3. Distance
Distance is _always_ specified in  meters.  `height`, `radius` and the altitude component of `pos` are some of the distance attributes.

*A special note about altitude:* As of the date of this writing (May 2022) we couldn't find any mention in the RFCs concerning the altitude reference.  Is it above:
# Ground Level (AGL)
# Mean Sea Level (MSL)
# A Geoid reference (which one?)

h3. Angle
Angle may be specified in either degrees or radians by specifying the `degrees` or `radians` suffix to the angle value.  The default it `degrees` if no suffix is provided.  `orientation`, `startAngle` and `openingAngle` are some of the angle attributes.

h2. Shapes
h3. Point
A Point isn't really a "shape" because it's a one dimensional construct but we'll ignore that.  It's simply a point in space specified with either two or three dimensions.



|| Shape || Attributes || Description ||
| Point | pos or pos3d | A single point |
| Circle | pos or pos3d, radius | A two dimensional circle around a point |
| Sphere | pos3d, radius | A 3 dimensional sphere around a point |

|| Attribute || Description || Units || Example ||
| pos | A two dimensional point | Decimal degrees | pos="39.12345 -105.98766" |
| pos3d | A three dimensional point | Decimal degrees + altitude in meters | pos="39.12345 -105.98766 1690" |
| radius | Distance | Meters | radius="20" |
| height | Distance | Meters | height="45" |
| orientation | Angle | Degrees (default) or Radians | orientation="90", orientation="25 radians" |

{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}


