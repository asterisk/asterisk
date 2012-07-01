# $Id$
#
from inc_cfg import *

test_param = TestParam(
		"Callee=optional SRTP, caller=no SRTP",
		[
			InstanceParam("callee", "--null-audio --use-srtp=1 --srtp-secure=0 --max-calls=1"),
			InstanceParam("caller", "--null-audio --max-calls=1")
		]
		)
