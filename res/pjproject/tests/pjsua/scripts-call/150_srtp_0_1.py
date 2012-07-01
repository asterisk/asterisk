# $Id$
#
from inc_cfg import *

test_param= TestParam(
		"Callee=no SRTP, caller=optional SRTP",
		[
			InstanceParam("callee", "--null-audio --max-calls=1"),
			InstanceParam("caller", "--null-audio --use-srtp=1 --srtp-secure=0 --max-calls=1")
		]
		)
