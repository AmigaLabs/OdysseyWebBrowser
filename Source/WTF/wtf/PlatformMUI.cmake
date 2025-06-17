list(APPEND WTF_SOURCES
    posix/ThreadingPOSIX.cpp
    posix/FileSystemPOSIX.cpp
    posix/OSAllocatorPOSIX.cpp
    generic/WorkQueueGeneric.cpp
    generic/RunLoopGeneric.cpp
    text/mui/TextBreakIteratorInternalICUMorphOS.cpp
    mui/CPUTimeAROS.cpp
    mui/LanguageMorphOS.cpp
    generic/MemoryFootprintGeneric.cpp
    generic/MainThreadGeneric.cpp
    morphos/Signals.cpp
)
if (AROS)
    list(APPEND WTF_SOURCES 
        mui/execallocator.cpp
        OSAllocatorAROS.cpp
        generic/MemoryPressureHandlerGeneric.cpp
    )
endif()
if (AmigaOS4)
    list(APPEND WTF_SOURCES 
        OSAllocatorAmigaOS.cpp
        morphos/OS4Misc.cpp
        morphos/MemoryPressureHandlerMorphOS.cpp
        mui/utils.cpp
    )
endif()
