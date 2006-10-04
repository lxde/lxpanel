# Part 0
# load common stuff
TOPDIR = ..
include $(TOPDIR)/Makefile.common

# backslashify slashes to avoid problems with sed
BPREFIX := $(subst /,\/,$(PREFIX))

SRC = fbpanel.1
TARGET := fbpanel.1.gz

all: $(TARGET)
$(TARGET): $(SRC)
	@$(call summary,TEXT,$@)
	$(Q)sed 's/PREFIX/$(BPREFIX)/g' < $(SRC) | gzip - > $@



install: all
	install -d  $(PREFIX)/share/man/man1
	install -m 644 $(TARGET) $(PREFIX)/share/man/man1

uninstall:
	$(RM) $(PREFIX)/share/man/man1/$(TARGET)
