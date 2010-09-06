# This is a convenience script for systems on which BSD make is the default,
# such that typing 'make' will do what people expect, instead of producing an
# error (due to incompatibilities between BSD make and GNU make).

.include "makeopts"

all::
	$(MAKE)

$(.TARGETS)::
	$(MAKE) $(.TARGETS)
