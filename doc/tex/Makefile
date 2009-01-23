include ../../makeopts

pdf: asterisk.pdf

asterisk.pdf: $(wildcard *.tex)
ifeq ($(findstring rubber,$(RUBBER)),)
	@echo "**********************************************"
	@echo "** You must install the \"rubber\" tool      ***"
	@echo "** to generate the Asterisk reference PDF. ***"
	@echo "**********************************************"
else
	@echo "**********************************************"
	@echo "** The Asterisk reference PDF will now be  ***"
	@echo "** generated.  When complete, it will be   ***"
	@echo "** located at asterisk.pdf.                ***"	
	@echo "**********************************************"
ifneq ($(findstring kpsewhich,$(KPATHSEA)),)
ifeq ($(findstring fncychap.sty,$(shell find `$(KPATHSEA) --expand-braces='$${TEXMF}'| tr -d \! | sed 's/:/ /g'` -name fncychap.sty -print)),)
	@echo
	@echo "WARNING:  The fncychap.sty document was not found"
	@echo "On Ubuntu, install the texlive-latex-extra package."
	@echo
	@exit
endif
endif
	@cp asterisk.tex asterisk.tex.orig
	@sed -e 's/ASTERISKVERSION/$(shell echo $(ASTERISKVERSION) | sed -e 's/\//\\\//g' | sed -e 's/_/\\\\_/g')/' asterisk.tex > asterisk_local.tex
	@mv asterisk_local.tex asterisk.tex
	@$(RUBBER) --pdf asterisk.tex
	@mv asterisk.tex.orig asterisk.tex
endif

html:
	@echo "**********************************************"
	@echo "** The Asterisk reference HTML will now be ***"
	@echo "** generated.  When complete, it will be   ***"
	@echo "** located in the asterisk/ directory.     ***"	
	@echo "** Note that the latex2html tool is        ***"	
	@echo "** required for this to work.              ***"	
	@echo "**********************************************"
	@cp asterisk.tex asterisk.tex.orig
	@sed -e 's/ASTERISKVERSION/$(ASTERISKVERSION)/' asterisk.tex > asterisk_local.tex
	@mv asterisk_local.tex asterisk.tex
	@latex2html asterisk.tex
	@mv asterisk.tex.orig asterisk.tex

