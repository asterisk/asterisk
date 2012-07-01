# $Id$
#
from inc_cfg import *

# TCP call
test_param = TestParam(
		"TCP transport",
		[
			InstanceParam("callee", "--null-audio --no-udp --max-calls=1", uri_param=";transport=tcp"),
			InstanceParam("caller", "--null-audio --no-udp --max-calls=1")
		]
		)
