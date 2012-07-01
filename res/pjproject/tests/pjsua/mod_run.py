# $Id$
import imp
import sys

from inc_cfg import *

# Read configuration
cfg_file = imp.load_source("cfg_file", ARGS[1])

# Here where it all comes together
test = cfg_file.test_param
