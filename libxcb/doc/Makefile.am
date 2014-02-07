
EXTRA_DIST = \
tutorial/index.html \
tutorial/xcb.css \
xcb.doxygen.in \
xkb_internals \
xkb_issues

docdirs = $(srcdir)/tutorial

if ENABLE_DEVEL_DOCS
if HAVE_DOXYGEN
if HAVE_DOT

docdirs += manual

# rule to build documentation and copy necessary files
manual:
	doxygen xcb.doxygen

# rules to clean
clean-local:
	rm -rf manual/

endif
endif
endif

all-local: $(docdirs)

# rule to install the html documentation and tutorial in $(htmldir)
install-data-local:
	@if ! test -d "$(DESTDIR)$(htmldir)"; then \
	  echo "$(mkinstalldirs) '$(DESTDIR)$(htmldir)'"; \
	  $(mkinstalldirs) '$(DESTDIR)$(htmldir)'; \
	fi
	@for d in $(docdirs); do \
	  echo "cp -pR $$d '$(DESTDIR)$(htmldir)/'"; \
	  cp -pR $$d '$(DESTDIR)$(htmldir)/'; \
	done

uninstall-local:
	@for d in $(docdirs); do \
	  d=`basename $$d`; \
	  echo "test ! -d '$(DESTDIR)$(htmldir)/'$$d || { find '$(DESTDIR)$(htmldir)/'$$d -type d ! -perm -200 -exec chmod u+w '{}' ';' && rm -rf '$(DESTDIR)$(htmldir)/'$$d; }"; \
	  test ! -d '$(DESTDIR)$(htmldir)/'$$d || { find '$(DESTDIR)$(htmldir)/'$$d -type d ! -perm -200 -exec chmod u+w '{}' ';' && rm -rf '$(DESTDIR)$(htmldir)/'$$d; }; \
	done
	rmdir "$(DESTDIR)$(htmldir)/" || true
