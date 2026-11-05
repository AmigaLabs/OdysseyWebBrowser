set(OdysseyWebBrowser_SOURCES
    main.cpp
)

set(OdysseyWebBrowser_INCLUDE_DIRECTORIES
    "${CAIRO_INCLUDE_DIRS}"
    "${WEBKITLEGACY_DIR}/mui/UI"
    "${WEBKITLEGACY_DIR}/mui/UI/AROS/include"
    "${DERIVED_SOURCES_WEBCORE_DIR}"
)

if (AmigaOS4)
set(OdysseyWebBrowser_LIBRARIES
    # TEMP: one group so static-library cycles (WebKitLegacy <-> WTF/WebCore) resolve on AmigaOS
    -Wl,--start-group JavaScriptCore WebKitLegacy WebCore PAL WTF -Wl,--end-group
    -mcrt=clib4
    -lfreetype
    -lcairo
    -lwebp 
    -lwebpdemux
    -lfontconfig
    -lbrotlidec
    -lbrotlicommon
    -lz
    -lcurl
    -lrtmp
    -lidn2
    -ljpeg
    -lpng
    -lssl
    -lcrypto
    -licui18n
    -licuuc
    -licudata
    -lsqlite3
    -lxslt
    -lxml2
    -lexpat
    # TEMP: FFmpeg (video off) and nghttp2 (not in SDK) removed from link; re-add when enabled
    -lbz2
    -lz
    -lpsl
    -latomic
    -lpixman-1
    -lfontconfig
    -lpthread
    -lunistring
    ${ZLIB_LIBRARIES}    
    -athread=native
)
else()
set(OdysseyWebBrowser_LIBRARIES
    JavaScriptCore
    WebKitLegacy
    -Wl,--start-group WebCore -Wl,--end-group
    -lz
    -lcairo
    -lpixman
    -lfontconfig
    -lfreetype2
    -lxml2
    -lz
    -lcurl
    -ljpeg
    -lpng
    -lssl
    -lcrypto
    -licui18n
    -licuuc
    -licudata
    -lsqlite3
    -lxslt
    -ldebug
    -lopenurl
    -lrexxsyslib
    -lbz2
    -liconv
    -lz
    -lpsl
    -latomic
    ${ZLIB_LIBRARIES}
)
endif()

include_directories(${OdysseyWebBrowser_INCLUDE_DIRECTORIES})
if (AmigaOS4)
add_executable(Odyssey ${OdysseyWebBrowser_SOURCES})
target_link_libraries(Odyssey ${OdysseyWebBrowser_LIBRARIES})
else()
add_executable(OWB ${OdysseyWebBrowser_SOURCES})
target_link_libraries(OWB ${OdysseyWebBrowser_LIBRARIES})
endif()