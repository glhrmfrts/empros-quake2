import os, sys, shutil

build_type = sys.argv[1]

DLL_PATH = "build/release/{}/game.dll".format(build_type)
BASEQ2_PATH = "build/release/{}/baseq2".format(build_type)

try:
	os.mkdir(BASEQ2_PATH)
except FileExistsError:
	pass

shutil.copyfile(DLL_PATH, os.path.join(BASEQ2_PATH, "game.dll"))
