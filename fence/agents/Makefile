include ../../make/defines.mk

ifdef fence_agents
SUBDIRS = ${fence_agents} lib
endif

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		if [ "$$i" = "xvm" ] && [ "${enable_virt}" != "1" ] && [ "$@" != "clean" ]; then \
			continue; \
		else \
			${MAKE} -C $$i $@; \
		fi; \
	done
