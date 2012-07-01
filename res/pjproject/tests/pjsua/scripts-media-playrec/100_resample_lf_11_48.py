# $Id$
#
from inc_cfg import *

# simple test
test_param = TestParam(
		"Resample (large filter) 11 KHZ to 48 KHZ",
		[
			InstanceParam("endpt", "--null-audio --quality 10 --clock-rate 48000 --play-file wavs/input.11.wav --rec-file wavs/tmp.48.wav")
		]
		)
