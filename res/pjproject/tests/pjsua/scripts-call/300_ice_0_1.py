# $Id$
#
from inc_cfg import *

# ICE mismatch
test_param = TestParam(
		"Callee=no ICE, caller=use ICE",
		[
			InstanceParam("callee", "--null-audio --max-calls=1"),
			InstanceParam("caller", "--null-audio --use-ice --max-calls=1")
		]
		)
