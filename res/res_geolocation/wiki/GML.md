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
Positions are always specified in decimal degrees latitude and longitude.  A 3d position adds the altitude in meters.  `pos` and `pos3d` are the two attributes that specify position.
h3. Distance
Distance is _always_ specified in  meters.  `height`, `radius` and the altitude component of `pos` are some of the distance attributes.

*A special note about altitude:* As of the date of this writing (May 2022) we couldn't find any mention in the RFCs concerning the altitude reference.  Is it above:
# Ground Level (AGL)
# Mean Sea Level (MSL)
# A Geoid reference (which one?)

h3. Angle
Angle may be specified in either degrees or radians by specifying the `degrees` or `radians` suffix to the angle value.  The default it `degrees` if no suffix is provided.  `orientation`, `startAngle` and `openingAngle` are some of the angle attributes.

h2. Shapes
See the references above for the exact shape definitions.

|| Shape || Attributes ||
| Point | pos or pos3d |
| Circle | pos or pos3d, radius |
| Sphere | pos3d, radius |
| Ellipse| pos or pos3d, semiMajorAxis, semiMinorAxis, orientation |
| ArcBand | pos or pos3d, innerRadius, outerRadius, startAngle, openingAngle |
| Ellipsoid | pos3d, semiMajorAxis, semiMinorAxis, verticalAxis, orientation |
| Polygon | 3 or more pos or pos3d |
| Prism | 3 or more pos3d, height |


|| Attribute || Description || Units || Example ||
| pos | A two dimensional point | Decimal degrees | pos="39.12345 -105.98766" |
| pos3d | A three dimensional point | Decimal degrees + altitude in meters | pos="39.12345 -105.98766 1690" |
| radius | Distance | Meters | radius="20" |
| height | Distance | Meters | height="45" |
| orientation | Angle | Degrees (default) or Radians | orientation="90", orientation="25 radians" |
| semiMajorAxis | Distance | Meters | semiMajorAxis="145" |
| semiMinorAxis | Distance | Meters | semiMinorAxis="145" |
| innerRadius | Distance | Meters | innerRadius="350" |
| outerRadius | Distance | Meters | outerRadius="350" |
| verticalAxis | Distance | Meters | verticalAxis="20" |

h2. Examples:

{code}
location_info = shape=Point, crs=2d, pos="39.12345 -105.98766"
location_info = shape=Point, crs=3d, pos="39.12345 -105.98766 1892.0"
location_info = shape=Circle, crs=2d, pos="39.12345 -105.98766" radius="45"
location_info = shape=Sphere, crs=3d, pos="39.12345 -105.98766 1902" radius="20"
location_info = shape=Ellipse, crs=2d, pos="39.12345 -105.98766" semiMajorAxis="20", semiMinorAxis="10", orientation="25 radians"
location_info = shape=ArcBand, crs=2d, pos="39.12345 -105.98766" innerRadius="1200", outerRadius="1500", startAngle="90", openingAngle="120"
location_info = shape=Polygon, crs=2d, pos="39.12345 -105.98766", pos=40.7890 -105.98766", pos="40.7890 -106.3456", pos=39.12345 -106.3456"
location_info = shape=Prism, crs=3d, pos="39.12345 -105.98766 1890", pos="40.7890 -105.98766 1890", pos="40.7890 -106.3456 1890", pos=39.12345 -106.3456 1890", height="45"
{code}


{column}
{column:width=30%}
Table of Contents:
{toc}


Geolocation:
{pagetree:root=Geolocation|expandCollapseAll=true}
{column}
{section}


