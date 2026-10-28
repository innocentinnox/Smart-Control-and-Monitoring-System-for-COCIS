# Lets `make pdf` / `make report` (and the other report targets) work from inside docs/report/.
# Everything is forwarded to the Makefile at the repository root, which holds the real recipes.

.PHONY: pdf report pdf-watch pdf-clean

pdf report pdf-watch pdf-clean:
	$(MAKE) -C ../.. $@
