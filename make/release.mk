# NOTE: this make file snippet is only used by the release manager
# to build official release tarballs, handle tagging and publish.
#
# do _NOT_ use for anything else!!!!!!!!!

## do sanity checks

ifndef VERSION

all:
	@echo WARNING: VERSION= is not defined!
	@exit 1

else ifndef OLDVER

all:
	@echo WARNING: OLDVER= is not defined!
	@exit 1

else

ifdef RELEASE
TEST=""
else
TEST="test"
endif

## setup stuff

MASTERPROJECT=cluster

ifdef RELEASE
MASTERPV=$(MASTERPROJECT)-$(VERSION)
else
MASTERPV=HEAD
endif
MASTERTGZ=$(TEST)$(MASTERPROJECT)-$(VERSION).tar.gz

# fence-agents
FENCEPROJECT=fence-agents
FENCEPV=$(FENCEPROJECT)-$(VERSION)
FENCETGZ=$(TEST)$(FENCEPV).tar.gz

# resource-agents
RASPROJECT=resource-agents
RASPV=$(RASPROJECT)-$(VERSION)
RASTGZ=$(TEST)$(RASPV).tar.gz

all: tag tarballs

ifdef RELEASE
tag:
	git tag -a -m "$(MASTERPV) release" $(MASTERPV) HEAD

else
tag:

endif

tarballs: master-tarball fence-agents-tarball resource-agents-tarball

master-tarball:
	git archive \
		--format=tar \
		--prefix=$(MASTERPROJECT)-$(VERSION)/ \
		$(MASTERPV) | \
		tar xp
	sed -i -e \
		's#<CVS>#$(VERSION)#g' \
		$(MASTERPROJECT)-$(VERSION)/gfs-kernel/src/gfs/gfs.h
	echo "VERSION \"$(VERSION)\"" \
		>> $(MASTERPROJECT)-$(VERSION)/make/official_release_version
	tar cp $(MASTERPROJECT)-$(VERSION) | \
		gzip -9 \
		> ../$(MASTERTGZ)
	rm -rf $(MASTERPROJECT)-$(VERSION)

fence-agents-tarball:
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(FENCEPV)
	cd $(FENCEPV) && \
		rm -rf bindings cman common config contrib dlm gfs* group rgmanager && \
		rm -rf fence/fenced fence/fence_node fence/fence_tool fence/include fence/libfence fence/libfenced && \
		rm -rf fence/man/fence.8 fence/man/fenced.8 fence/man/fence_node.8 fence/man/fence_tool.8 && \
		sed -i -e 's/fence.8//g' -e 's/fenced.8//g' -e 's/fence_node.8//g' -e 's/fence_tool.8//g' fence/man/Makefile
	tar cp $(FENCEPV) | \
		gzip -9 \
		> ../$(FENCETGZ)
	rm -rf $(FENCEPV)

resource-agents-tarball:
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(RASPV)
	cd $(RASPV) && \
		rm -rf bindings cman common config contrib dlm fence gfs* group && \
		rm -rf rgmanager/ChangeLog rgmanager/errors.txt rgmanager/event-script.txt \
			rgmanager/examples rgmanager/include rgmanager/init.d rgmanager/man \
			rgmanager/README && \
		rm -rf rgmanager/src/clulib rgmanager/src/daemons rgmanager/src/utils
	tar cp $(RASPV) | \
		gzip -9 \
		> ../$(RASTGZ)
	rm -rf $(RASPV)

publish:
	git push --tags origin
	scp ../$(MASTERTGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	scp ../$(FENCETGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	scp ../$(RASTGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	git log $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) | \
		git shortlog > ../$(MASTERPV).emaildata
	git diff --stat $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) \
		>> ../$(MASTERPV).emaildata
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to cluster-devel and linux-cluster

endif
