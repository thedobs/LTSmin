
PINS_LIBS  = ../pins-lib/libpins.la ../util-lib/libutil.la ../hre/libhre.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la ../dm/libdm.la ../lts-io/liblts-io.la
PINS_LIBS +=  ../hre-io/libhre-io.la

bin_PROGRAMS =

# etf2torx
bin_PROGRAMS += etf2torx
etf2torx_SOURCES  = spec2torx.c
etf2torx_SOURCES += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
etf2torx_LDADD    = $(PINS_LIBS)
etf2torx_LDFLAGS  = $(AM_LDFLAGS)
etf2torx_CPPFLAGS = -DETF $(TORX_CPPFLAGS) $(AM_CPPFLAGS)

# pins2torx
bin_PROGRAMS += pins2torx
pins2torx_SOURCES  = spec2torx.c
pins2torx_SOURCES += ../pins-lib/dlopen-pins.c ../pins-lib/dlopen-pins.h
pins2torx_LDADD    = $(PINS_LIBS)
pins2torx_LDFLAGS  = $(AM_LDFLAGS)
pins2torx_CPPFLAGS = -DPINS_DLL $(TORX_CPPFLAGS) $(AM_CPPFLAGS)

# dve2torx
if HAVE_DVE
bin_PROGRAMS += dve2torx
endif
dve2torx_SOURCES  = spec2torx.c
dve2torx_SOURCES += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
dve2torx_LDADD    = $(PINS_LIBS) $(DVE_LIBS)
dve2torx_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)
dve2torx_CPPFLAGS = -DDIVINE $(TORX_CPPFLAGS) $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)

# lpo2torx
if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2torx
endif
lpo2torx_SOURCES  = spec2torx.c
lpo2torx_LDADD    = ../pins-lib/libmcrl-pins.la $(PINS_LIBS) $(MCRL_LIBS)
lpo2torx_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
lpo2torx_CPPFLAGS = -DMCRL $(TORX_CPPFLAGS) $(AM_CPPFLAGS) $(MCRL_CPPFLAGS)

# lps2torx
if HAVE_LIBMCRL2
bin_PROGRAMS += lps2torx
endif
lps2torx_SOURCES  = spec2torx.c
lps2torx_LDADD    = ../pins-lib/libmcrl2-pins.la $(PINS_LIBS)
lps2torx_LDFLAGS  = $(AM_LDFLAGS)
lps2torx_CPPFLAGS = -DMCRL2 $(TORX_CPPFLAGS) $(AM_CPPFLAGS)
nodist_EXTRA_lps2torx_SOURCES = ../automake-force-linker.cxx

# prom2torx
if HAVE_SPINS
bin_PROGRAMS += prom2torx
endif
prom2torx_SOURCES  = spec2torx.c
prom2torx_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
prom2torx_LDADD    = $(PINS_LIBS) $(SPINS_LIBS)
prom2torx_LDFLAGS  = $(SPINS_LDFLAGS) $(AM_LDFLAGS)
prom2torx_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(TORX_CPPFLAGS) $(AM_CPPFLAGS)
