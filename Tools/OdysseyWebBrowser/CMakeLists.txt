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
    JavaScriptCore
    WebKitLegacy
    -mcrt=clib4
    -Wl,--start-group WebCore -Wl,--end-group
    -lz
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
    -ldebug
    -lavformat
    -lbz2
    -lavcodec
    -lz
    -lavutil
    -lswresample
    -lswscale
    -lpsl
    -latomic
    -lpixman-1
    -lfontconfig
    -lnghttp2
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
    -lavformat
    -lbz2
    -lavcodec
    -liconv
    -lz
    -lavutil
    -lswresample
    -lswscale
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