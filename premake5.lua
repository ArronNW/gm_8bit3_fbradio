PROJECT_GENERATOR_VERSION = 2

newoption({
	trigger = "gmcommon",
	description = "Sets the path to garrysmod_common",
	value = "path"
})

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
	"you didn't provide a path to garrysmod_common")

include(gmcommon)

CreateWorkspace({name = "eightbit"})
	platforms({"x86", "x86_64"})

	CreateProject({serverside = true})
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeDetouring()
		IncludeScanning()
		IncludeLuaShared()
		IncludeHelpersExtended()

		links("opus")
		includedirs("opus/include")

		filter({"platforms:x86_64"})
			architecture("x86_64")
			libdirs("opus/lib64")

		filter({"platforms:x86"})
			architecture("x86")
			libdirs("opus/lib32")

		filter("system:windows")
			links("ws2_32")

		filter("system:linux")
			links("dl")

