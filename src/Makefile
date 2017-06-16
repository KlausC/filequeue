# @(#)Makefile $Header: svn://d_apps/ems_industry_core/ISC/CPP/trunk/Makefile 140 2012-03-13 10:48:03Z crusius $
IDENT=  
#
#	LPATH environment variable contains $PROD/lib ( not X/OPEN ) 
#	CF environment variable or CCOPTS makro definition on commandline 
#	may be used to control -g or -O cc-flags
#
#

LANG=english

SHELL=	/bin/sh
AR=	/usr/bin/ar
TMPDIR=	/tmp

#	ANSI=$(ANSI)
# 	BSDINC=$(BSDINC)	
#	BSDLIB=$(BSDLIB)

CC=	cc
LD=	test -r $@ && find $@ -type l -exec rm {} \; ;\
	cc

COPTS=	${IDENT} -std=c11 -Wall -Wextra -Wpedantic
CCOPTS=
CFLAGS=$(ANSI) $(CF) $(COPTS) $(CCOPTS)
LIBSBD=$(LIBS)
LDFLAGS= $(LF) $(CFLAGS)

CP=	/bin/ln -f
MKDIR= /bin/mkdir -p
CHMOD= /bin/chmod

#
RM=	/bin/rm
SIZE=   /bin/size

############################################################################### 
SCRUTI=	
BINUTI=	fifomain fifomainp
SRCUTI= fifomain.c

INC1=	fifo.h
OBJ1= 	fifo.o fifop.o
SRC1=	fifo.c fifop.c

############################################################################### 

INCL=	${INC1}

SRC_NONINCL=  ${SRC1}

SRC=	${INCL} ${SRC_NONINCL}


LOBJS=	${OBJ1}
LSRC=	${SRC1}

OBJS=	$(LOBJS)

BINS= ${BINUTI}	

SCRIPTS= ${SCRUTI} test
TESTSCRIPTS= test

############################################################################### 
install:	instbin instscripts 

recompile:
		rm -f $(OBJS) $(BINS)
		make install

# libraries
$(BDLIB):	$(INCF) $(LOBJS)
		rm -f $@; $(AR) qv $@ $(LOBJS)


# installation procedures

instbin:	$(BINS)
		-$(MKDIR) bin; $(CP) $? bin

instscripts:	$(SCRIPTS)
		-$(MKDIR) bin; $(CHMOD) +x $?; $(CP) $? bin

testscripts:	$(TESTSCRIPTS)
		-$(MKDIR) bin; $CHMOD) +x $?; $(CP) $? bin

# rules for binaries  

fifomain: $(INC1) fifo.o  
		$(LD) $(CFLAGS) $(SRCUTI) -o $@ fifo.o $(LDFLAGS)

fifomainp: $(INC1) fifop.o  
		$(LD) $(CFLAGS) -pthread $(SRCUTI) -o $@ fifop.o $(LDFLAGS)

$(OBJ1):	$(INC1)

src:		$(SRC)

clear clean:
		-rm -f $(BINS) $(OBJS)
		-rm -f *.o

tags:		$(SRC)
		ctags -t $(SRC) && mv tags tags.x
		awk '$$1 != "if" && $$1 != "for" && $$1 != "while" && $$1 != "long" && $$1 != int ' tags.x >$@
		rm tags.x


.c:
		$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

### END

