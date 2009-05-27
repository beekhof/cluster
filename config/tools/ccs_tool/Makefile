TARGET1 = ccs_tool
TARGET2 = ccs_test

SBINDIRT = $(TARGET1)
SBINSYMT = $(TARGET2)

include ../../../make/defines.mk

all: depends ${TARGET1} ${TARGET2}

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS =	ccs_tool.o \
	editconf.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += `xml2-config --cflags`
CFLAGS += -I${ccsincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs
LDFLAGS += `xml2-config --libs`
LDFLAGS += -L${libdir}

${TARGET1}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

depends:
	$(MAKE) -C $(OBJDIR)/config/libs/libccsconfdb all

clean: generalclean

-include $(OBJS:.o=.d)
