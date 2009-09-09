TARGET1= rng2ldif
TARGET2= genclass

all: ${TARGET1} ${TARGET2}

include ../../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_GNU_SOURCE
CFLAGS += -I. 
CFLAGS += `xml2-config --cflags`
CFLAGS += -I${incdir}

LDFLAGS += `xml2-config --libs`
LDFLAGS += -L${libdir}

OBJS1=  rng2ldif.o \
	tree.o

OBJS2=  genclass.o

SHAREDOBJS= zalloc.o \
	    value-list.o \
	    ldaptypes.o \
	    name.o

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

ldif-update: all $(SRCDIR)/config/plugins/ldap/ldap-base.csv
	make -C $(OBJDIR)/config/tools/xml cluster.rng
	./rng2ldif $(OBJDIR)/config/tools/xml/cluster.rng \
		$(SRCDIR)/config/plugins/ldap/ldap-base.csv \
		$(SRCDIR)/config/plugins/ldap/99cluster.ldif

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(SHAREDOBJS:.o=.d)
