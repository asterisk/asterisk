# $Id$
#
from inc_cfg import *

# TCP call
test_param = TestParam(
		"Caller requires PRACK",
		[
			InstanceParam("callee", "--null-audio --max-calls=1"),
			InstanceParam("caller", "--null-audio --max-calls=1 --use-100rel")
		]
		)
