# $Id$
#
from inc_cfg import *

# Different number of ICE components
test_param = TestParam(
		"Callee=use ICE, caller=use ICE",
		[
			InstanceParam("callee", "--null-audio --use-ice --max-calls=1", enable_buffer=True),
			InstanceParam("caller", "--null-audio --use-ice --max-calls=1 --ice-no-rtcp", enable_buffer=True)
		]
		)
