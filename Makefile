# Common tasks for the Group 28 CoCIS Smart Control & Monitoring System.
# Works from PowerShell, cmd or Git Bash (recipes only use commands valid in all of them).
#
#   make pdf          build docs/report.pdf (needs only MiKTeX); `make report` does the same
#   make pdf-watch    rebuild the PDF on every save (latexmk, needs Perl)
#   make pdf-clean    delete LaTeX build files (keeps report.pdf)
#   make test         run the host unit tests (gcc, or the gcc Docker image)
#   make diagram      regenerate firmware/diagram.json and the report's wiring figure

PYTHON ?= python
REPORT  = docs/report

# pdflatex is run twice so the table of contents and cross-references are filled in.
# -aux-directory (MiKTeX) keeps .aux/.log/.toc in docs/report/build/; the PDF lands in docs/.
LATEX = pdflatex -interaction=nonstopmode -halt-on-error -file-line-error -jobname=report -aux-directory=build -output-directory=..

.PHONY: help pdf report pdf-watch pdf-clean test diagram

help:
	@echo make pdf        - build docs/report.pdf, alias: make report
	@echo make pdf-watch  - rebuild the PDF on every save, needs Perl
	@echo make pdf-clean  - remove LaTeX build files
	@echo make test       - run the host unit tests
	@echo make diagram    - regenerate diagram.json and the wiring figure, needs Pillow

pdf:
	cd $(REPORT) && $(LATEX) main.tex && $(LATEX) main.tex

report: pdf

pdf-watch:
	cd $(REPORT) && latexmk -pvc

pdf-clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('$(REPORT)/build', ignore_errors=True)"

test:
	sh firmware/test/host/run_tests.sh

diagram:
	cd firmware && $(PYTHON) tools/gen_diagram.py --preview ../$(REPORT)/figures/wiring.png
