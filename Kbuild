# Kernel version check: require >= 6.18.0
# VERSION and PATCHLEVEL are integer variables set by the kernel build system.
KVER_OK := $(shell \
	if [ $(VERSION) -gt 6 ]; then echo 1; \
	elif [ $(VERSION) -eq 6 ] && [ $(PATCHLEVEL) -ge 18 ]; then echo 1; \
	else echo 0; fi)

$(if $(filter 0,$(KVER_OK)),\
	$(error dp-modules requires kernel >= 6.18.0 but found $(VERSION).$(PATCHLEVEL).$(SUBLEVEL). Stopping build.))

obj-y	+= dp/
