# PyInstaller build. Run from the project root:
#
#     .venv\Scripts\python.exe -m PyInstaller packaging/picviewer.spec --noconfirm
#
# One folder, not one file. A --onefile build unpacks itself into a temp
# directory on every launch, which costs seconds each time -- unacceptable for
# something you double-click an image to open. The folder goes inside an
# installer anyway, so the user never sees it.

from pathlib import Path

from PyInstaller.utils.hooks import collect_dynamic_libs

ROOT = Path(SPECPATH).parent

# PySide6 ships every Qt module there is, and its hook bundles most of them by
# default. This viewer uses QtCore, QtGui and QtWidgets; everything below is
# dead weight, and WebEngine alone is well over a hundred megabytes.
EXCLUDED_QT = [
    "QtWebEngineCore", "QtWebEngineWidgets", "QtWebEngineQuick", "QtWebChannel",
    "QtWebSockets", "QtWebView",
    "Qt3DCore", "Qt3DRender", "Qt3DInput", "Qt3DLogic", "Qt3DAnimation",
    "Qt3DExtras",
    "QtQuick", "QtQuick3D", "QtQuickControls2", "QtQuickWidgets", "QtQml",
    "QtQmlModels", "QtQmlWorkerScript",
    "QtMultimedia", "QtMultimediaWidgets", "QtSpatialAudio",
    "QtCharts", "QtDataVisualization", "QtGraphs", "QtGraphsWidgets",
    "QtBluetooth", "QtNfc", "QtPositioning", "QtLocation", "QtSensors",
    "QtSerialPort", "QtSerialBus", "QtRemoteObjects", "QtScxml",
    "QtStateMachine", "QtTextToSpeech", "QtHttpServer", "QtNetworkAuth",
    "QtSql", "QtTest", "QtHelp", "QtDesigner", "QtUiTools", "QtDBus",
    "QtPdf", "QtPdfWidgets", "QtNetwork",
]

EXCLUDES = ["PySide6." + name for name in EXCLUDED_QT] + [
    "tkinter", "pytest", "_pytest", "unittest", "pydoc", "doctest",
    "setuptools", "pip", "distutils",
    "matplotlib", "scipy", "pandas",
]

a = Analysis(
    # launcher.py, not picviewer/__main__.py: PyInstaller runs its target as a
    # top-level script, and __main__.py's relative imports need a real package.
    [str(ROOT / "packaging" / "launcher.py")],
    pathex=[str(ROOT)],
    binaries=collect_dynamic_libs("rawpy") + collect_dynamic_libs("pillow_heif"),
    datas=[(str(ROOT / "picviewer" / "icon.ico"), "picviewer")],
    hiddenimports=["pillow_heif", "rawpy", "send2trash"],
    excludes=EXCLUDES,
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="PicViewer",
    console=False,              # pythonw equivalent: no console window
    icon=str(ROOT / "picviewer" / "icon.ico"),
    version=str(ROOT / "packaging" / "version_info.txt"),
    upx=False,                  # UPX breaks some Qt DLLs and trips antivirus
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="PicViewer",
)
