# $Id$
#
from inc_cfg import *

# Direct peer to peer presence
test_param = TestParam(
		"Direct peer to peer presence",
		[
			InstanceParam("client1", "--null-audio"),
			InstanceParam("client2", "--null-audio")
		]
		)
