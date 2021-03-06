binPath=${rootPath}
libPath=${rootPath}

#Modify this variable to set the location of sonLib
sonLibRootPath=${rootPath}/../sonLib
sonLibPath=${sonLibRootPath}/lib

halRootPath=${rootPath}/../hal
halPath=${halRootPath}/lib

include  ${sonLibRootPath}/include.mk

cflags += -I ${sonLibPath}  -I ${halPath} 
cppflags += -I ${sonLibPath}  -I ${halPath}
basicLibs = ${halPath}/halLiftover.a ${halPath}/halLib.a ${sonLibPath}/sonLib.a ${sonLibPath}/cuTest.a 
basicLibsDependencies = ${basicLibs}

# hdf5 compilation is done through its wrappers.
# we can speficy our own (sonlib) compilers with these variables:
HDF5_CXX = ${cpp}
HDF5_CXXLINKER = ${cpp}
HDF5_CC = ${cxx}
HDF5_CCLINKER = ${cxx} 
cpp = h5c++ ${h5prefix}
cxx = h5cc ${h5prefix}

# add compiler flag and kent paths if udc is enabled
# relies on KENTSRC containing path to top level kent/ dir
# and MACHTYPE being specified
ifdef ENABLE_UDC
#  Find samtabix as in kent/src/inc/common.mk:
	ifeq (${SAMTABIXDIR},)
		SAMTABIXDIR = /hive/data/outside/samtabix/${MACHTYPE}
	endif

	basicLibs += ${KENTSRC}/src/lib/${MACHTYPE}/jkweb.a  ${SAMTABIXDIR}/libsamtabix.a -lssl -lcrypto
endif


