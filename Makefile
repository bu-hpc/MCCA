# RPA App Makefile

PROG_BUILD_NUM=.build_number
BUILD_DATE = $(shell date +'%Y%m%d')
BUILD_NUM = $(shell cat $(PROG_BUILD_NUM))
MAJOR_NUM = $(shell cat .major_number)
MINOR_NUM = $(shell cat .minor_number)

# INCLUDEDIR=/usr/include/libxml2 -I../include
INCLUDEDIR=

CC=mpicc

TRG=mcca

# SRC=starscan_xml.c imageproc.c

#PKGCFG= `pkg-config libxml-2.0 --cflags`
PKGCFG=

CFLAGS=  -o $(TRG) -Wall -g -O3
#CFLAGS=  -o $(TRG).$(MAJOR_NUM).$(MINOR_NUM).$(BUILD_NUM) -Wall -g -O
#CFLAGS+=  -I$(INCLUDEDIR)
LFLAGS= -lpthread -lm

CFLAGS += -DBUILD_DATE=$(BUILD_DATE) -DBUILD_NUM=$(BUILD_NUM)
CFLAGS += -DMAJOR_NUM=$(MAJOR_NUM) -DMINOR_NUM=$(MINOR_NUM)

all: $(TRG) $(BUILD_NUM)


#LIB=../lib/mal.a ../lib/cal_ueye.a /usr/lib/libueye_api.so ../lib/lal.a
LIB=

$(TRG): $(TRG).c $(SRC) $(LIB)
	$(CC) $(CFLAGS) $(TRG).c $(SRC) $(PKGCFG) $(LIB) $(LFLAGS)

$(BUILD_NUM):
	@echo Incrementing build number
	@if ! test -f $(PROG_BUILD_NUM); then echo 0 > $(PROG_BUILD_NUM); fi
	@echo $$(($$(cat $(PROG_BUILD_NUM)) + 1)) > $(PROG_BUILD_NUM)

clean:
	rm -f *.o *~ core $(TRG)
