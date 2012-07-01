# $Id$
#
from inc_cfg import *

# simple test
test_param = TestParam(
		"Resample (large filter) 11 KHZ to 8 KHZ",
		[
			InstanceParam("endpt", "--null-audio --quality 10 --clock-rate 8000 --play-file wavs/input.11.wav --rec-file wavs/tmp.8.wav")
		]
		)
