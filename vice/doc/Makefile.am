SUBDIRS = building html readmes vim

MISC_DOX = \
	CIA-README.txt \
	coding-guidelines.txt \
	Documentation-Howto.txt \
	Doxygen-Howto.txt \
	iec-bus.txt \
	Release-Howto.txt \
	vice.texi

TEXI_TOOLS = \
	fixdox.sh

DOC_TOOLS = \
	checkdoc.c \
	checkdoc.mak \
	Doxyfile \
	mainpage.dox \
	mkdoxy.sh \
	src2doxy.sh \
	txt2doxy.sh

STYLE_TOOLS = \
	.indent.pro \
	indent-test.c \
	uncrustify.cfg

# VICE_DOX is conditionally extended with vice.pdf
VICE_DOX = @VICE_PDF_FILE_NAME@

# info_TEXINFOS = vice.texi
# AM_MAKEINFOFLAGS = -D$(PLATFORM_DOX_FLAGS)

dist_doc_DATA = $(MISC_DOX) $(VICE_DOX)

EXTRA_DIST = $(DOC_TOOLS) $(TEXI_TOOLS) $(STYLE_TOOLS)

if UNIX_MACOSX_COMPILE
# Use brew supplied texi2dvi over buggy Apple version
export PATH := /usr/local/opt/texinfo/bin:$(PATH)
endif

.vice-texi-charset-ok:	$(srcdir)/vice.texi
	@if [ "`file --mime-encoding $<`" != "$<: iso-8859-1" ]; then \
		echo "ERROR: vice.texi contains content that is not valid iso-8859-x" >&2; \
		false; \
	fi
	touch .vice-texi-charset-ok

#if 0
$(builddir)/vice.txt:	.vice-texi-charset-ok
	touch vicetmp.txt
	LC_ALL=C $(MAKEINFO) -D$(PLATFORM_DOX_FLAGS) -o vicetmp.txt --no-validate --no-headers $(srcdir)/vice.texi
	$(srcdir)/fixdox.sh txt <vicetmp.txt | LC_ALL=C sed 's/ \+$$//' >$(builddir)/vice.txt
	rm -f vicetmp.txt
#endif

if ENABLE_PDF_DOCS
$(builddir)/vice.pdf:	.vice-texi-charset-ok
	@LC_ALL=C sed 's/@heading NO WARRANTY/@center NO WARRANTY/g' <$(srcdir)/vice.texi >vicepdf.texi
	@LC_ALL=C $(TEXI2DVI) -I $(srcdir) -q --clean --pdf "--texinfo=@set $(PLATFORM_DOX_FLAGS) " -o $(builddir)/vice.pdf vicepdf.texi
	@rm -f vicepdf.texi
endif


# This doesn't appear to delete vice.pdf etc added
# in this Makefile with VICE_DOX += FOO, perhaps just some ordinary `rm -f`
# would do better? -- Compyx
CLEANFILES = .vice-texi-charset-ok $(VICE_DOX)

