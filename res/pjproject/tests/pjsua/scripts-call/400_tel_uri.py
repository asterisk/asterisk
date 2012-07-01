# $Id$
#
from inc_cfg import *

# Simple call
test_param = TestParam(
		"tel: URI in From",
		[
			InstanceParam("callee", "--null-audio --max-calls=1 --id tel:+111"),
			InstanceParam("caller", "--null-audio --max-calls=1")
		]
		)
