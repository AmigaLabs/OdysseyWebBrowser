list(APPEND WTF_SOURCES
    posix/ThreadingPOSIX.cpp
    posix/FileSystemPOSIX.cpp
    generic/WorkQueueGeneric.cpp
    generic/RunLoopGeneric.cpp
    text/mui/TextBreakIteratorInternalICUMorphOS.cpp
    mui/CPUTimeAROS.cpp
    mui/LanguageMorphOS.cpp
    generic/MemoryFootprintGeneric.cpp
    generic/MainThreadGeneric.cpp
    generic/MemoryPressureHandlerGeneric.cpp
)
if (AROS)
    list(APPEND WTF_SOURCES 
        mui/execallocator.cpp
        OSAllocatorAROS.cpp
    )
endif()
if (AmigaOS4)
    list(APPEND WTF_SOURCES 
        OSAllocatorAmigaOS.cpp
    )
endif()
