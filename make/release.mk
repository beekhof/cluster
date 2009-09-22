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

## setup stuff

MASTERPROJECT=cluster

ifdef RELEASE
MASTERPV=$(MASTERPROJECT)-$(VERSION)
TEST=""
else
MASTERPV=HEAD
TEST="test"
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

# rgmanager
RGMPROJECT=rgmanager
RGMPV=$(RGMPROJECT)-$(VERSION)
RGMTGZ=$(TEST)$(RGMPV).tar.gz

# gfs-utils
GFSPROJECT=gfs-utils
GFSPV=$(GFSPROJECT)-$(VERSION)
GFSTGZ=$(TEST)$(GFSPV).tar.gz

all: tag tarballs

ifdef RELEASE
tag:
	git tag -a -m "$(MASTERPV) release" $(MASTERPV) HEAD

else
tag:

endif

tarballs: master-tarball
tarballs: fence-agents-tarball
tarballs: resource-agents-tarball
tarballs: rgmanager-tarball
tarballs: gfs-tarball

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

fence-agents-tarball: master-tarball
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(FENCEPV)
	cd $(FENCEPV) && \
		rm -rf bindings cman common config contrib dlm gfs* group \
			rgmanager fence/fenced fence/fence_node \
			fence/fence_tool fence/include fence/libfence \
			fence/libfenced fence/man/fence.8 fence/man/fenced.8 \
			fence/man/fence_node.8 fence/man/fence_tool.8 && \
		sed -i -e 's/fence.8//g' -e 's/fenced.8//g' \
			-e 's/fence_node.8//g' -e 's/fence_tool.8//g' \
			fence/man/Makefile
	tar cp $(FENCEPV) | \
		gzip -9 \
		> ../$(FENCETGZ)
	rm -rf $(FENCEPV)

resource-agents-tarball: master-tarball
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(RASPV)
	cd $(RASPV) && \
		rm -rf bindings cman common config contrib dlm fence gfs* \
			group rgmanager/ChangeLog rgmanager/errors.txt \
			rgmanager/event-script.txt rgmanager/examples \
			rgmanager/include rgmanager/init.d rgmanager/man \
			rgmanager/README rgmanager/src/clulib \
			rgmanager/src/daemons rgmanager/src/utils
	tar cp $(RASPV) | \
		gzip -9 \
		> ../$(RASTGZ)
	rm -rf $(RASPV)

rgmanager-tarball: master-tarball
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(RGMPV)
	cd $(RGMPV) && \
		rm -rf bindings cman common config contrib dlm fence gfs* group \
			rgmanager/src/resources
	tar cp $(RGMPV) | \
		gzip -9 \
		> ../$(RGMTGZ)
	rm -rf $(RGMPV)

gfs-tarball: master-tarball
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPROJECT)-$(VERSION) $(GFSPV)
	cd $(GFSPV) && \
		rm -rf bindings cman common config contrib dlm fence group \
			rgmanager gfs2
	tar cp $(GFSPV) | \
		gzip -9 \
		> ../$(GFSTGZ)
	rm -rf $(GFSPV)

publish:
	git push --tags origin
	scp ../$(MASTERTGZ) \
	    ../$(FENCETGZ) \
	    ../$(RASTGZ) \
	    ../$(GFSTGZ) \
	    ../$(RGMTGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	git log $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) | \
		git shortlog > ../$(MASTERPV).emaildata
	git diff --stat $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) \
		>> ../$(MASTERPV).emaildata
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to cluster-devel and linux-cluster

endif
