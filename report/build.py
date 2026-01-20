# Works on Windows only!
import subprocess

subprocess.call(["lualatex", "report.tex"])
subprocess.call(["bibtex", "report.aux"])
subprocess.call(["lualatex", "report.tex"])
subprocess.call(["lualatex", "report.tex"])
