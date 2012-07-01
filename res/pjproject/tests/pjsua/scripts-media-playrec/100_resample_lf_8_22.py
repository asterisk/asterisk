# $Id$
#
from inc_cfg import *

# simple test
test_param = TestParam(
		"Resample (large filter) 8 KHZ to 22 KHZ",
		[
			InstanceParam("endpt", "--null-audio --quality 10 --clock-rate 22050 --play-file wavs/input.8.wav --rec-file wavs/tmp.22.wav")
		]
		)
