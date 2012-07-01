include ../../../build.mak

cflags:
	@echo $(PJ_CFLAGS) $(CFLAGS)

cxxflags: cflags

ldflags:
	@echo $(PJ_LDFLAGS) $(PJ_LDLIBS) $(LDFLAGS)

