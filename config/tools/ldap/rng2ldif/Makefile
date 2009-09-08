TARGET1= rng2ldif
TARGET2= genclass

all: ${TARGET1} ${TARGET2}

include ../../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_GNU_SOURCE
CFLAGS += -I. -I${incdir}
CFLAGS += `xml2-config --cflags`

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
	$(CC) -o $@ $^ $(EXTRA_LDFLAGS) $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

ldif-update: all ../../../plugins/ldap/ldap-base.csv
	make -C ../../xml cluster.rng
	./rng2ldif ../../xml/cluster.rng \
		../../../plugins/ldap/ldap-base.csv \
		../../../plugins/ldap/99cluster.ldif
	

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(SHAREDOBJS:.o=.d)
