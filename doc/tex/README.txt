Asterisk Reference Documentation
--------------------------------

1) To generate a PDF from this documentation, you will need the rubber tool,
   and all of its dependencies.  The web site for this tool is:

      http://www.pps.jussieu.fr/~beffara/soft/rubber/

   Then, once this tool is installed, running "make pdf" will generate
   the PDF automatically using this tool.  The result will be asterisk.pdf.

   NOTE:  After installing rubber, you will need to re-run the top level
   configure script.  It checks to see if rubber is installed, so that the
   asterisk.pdf Makefile target can produce a useful error message when it is
   not installed.

2) To generate HTML from this documentation, you will need the latex2html tool,
   and all of its dependencies.  The web site for this tool is:

      http://www.latex2html.org/

   Then, once this tool is installed, running "make html" will generate the
   HTML documentation.  The result will be an asterisk directory full of
   HTML files.
