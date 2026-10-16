"""Compile and run the real component with a minimal UART/entity test transport."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "tests" / "build"
BUILD.mkdir(exist_ok=True)
SOURCES = [ROOT / "tests/test_component.cpp"] + [
    ROOT / "components/broan" / name
    for name in ("broan.cpp", "broan_control.cpp", "broan_monitor.cpp", "broan_probe.cpp")
]
INCLUDES = [ROOT / "tests/stubs", ROOT / "components/broan"]

if os.name == "nt":
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([
        str(vswhere), "-latest", "-prerelease", "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath",
    ], text=True).strip()
    if not install:
        raise RuntimeError("Install Visual Studio C++ Build Tools to run the native tests")
    setup = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    executable = BUILD / "test_component.exe"
    arguments = ["cl", "/nologo", "/std:c++20", "/EHsc", "/W3"]
    arguments += [f"/I{path}" for path in INCLUDES]
    arguments += [str(path) for path in SOURCES]
    arguments += [f"/Fe:{executable}"]
    command = f'call "{setup}" >nul && ' + subprocess.list2cmdline(arguments)
    script = BUILD / "compile.cmd"
    script.write_text("@echo off\n" + command + "\nexit /b %errorlevel%\n")
    subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=BUILD, check=True)
else:
    executable = BUILD / "test_component"
    compiler = shutil.which("c++")
    if not compiler:
        raise RuntimeError("Install a C++20 compiler to run the native tests")
    subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-g",
                    "-fsanitize=address,undefined", *[f"-I{path}" for path in INCLUDES],
                    *map(str, SOURCES), "-o", str(executable)], cwd=BUILD, check=True)
subprocess.run([str(executable)], cwd=BUILD, check=True)
