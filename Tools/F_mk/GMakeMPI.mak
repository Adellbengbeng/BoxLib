# MPI reconfiguration...

ifndef MPI
$(error THIS FILE SHOULD ONLY BE LOADED WITH MPI defined)
endif

# Architecture specific changes...
ifeq ($(ARCH),AIX)
  F90 = mpxlf95$(rsuf)
  FC  = mpxlf$(rsuf)
  mpi_libraries = -lmpi
endif

ifeq ($(ARCH),Darwin)

  # specifics for maudib
  # I have both GNU and Intel compilers installed, as well as MPI-wrapped
  # versions, so I need to be careful here
  ifeq ($(UNAMEN),maudib.ucolick.org)

    # GNU compilers
    ifeq ($(COMP),gfortran)
      FC = mpif90
      F90 = mpif90
      CC = mpicc

      MPIHOME=/Users/cmalone/work/usr/local

    # Intel compilers
    else
      FC  = mpiif90
      F90 = mpiif90
      CC  = mpiicc

      MPIHOME=/Users/cmalone/work/usr/local/intel
    endif 

  # attempt at general support for Mac; only works if MPIHOME env var is set
  # and applies some general defaults
  else ifdef MPIHOME
    FC = $(MPIHOME)/bin/mpif90
    F90 = $(MPIHOME)/bin/mpif90
    CC = $(MPIHOME)/bin/mpicc
    CXX = $(MPIHOME)/bin/mpicxx

  # otherwise break
  else
    $(error SORRY, no MPI specification for your Darwin/Mac machine; check BoxLib/Tools/F_mk/GMakeMPI.mak and/or set MPIHOME environment variable)
  endif

  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib

endif

ifeq ($(ARCH),Linux)
  ifeq ($(COMP),g95)
    F90FLAGS += -fno-second-underscore
    FFLAGS   += -fno-second-underscore
    override F_C_LINK := UNDERSCORE
  endif
  ifeq ($(COMP),PathScale)
    FC = mpif90
    F90 = mpif90
    ifeq ($(findstring atlas, $(UNAMEN)), atlas)
      FC = mpipathf90
      F90 = mpipathf90
      CC = mpipathcc
    endif
    ifdef MPIHOME
      mpi_include_dir = $(MPIHOME)/include
    endif
  endif  # end of PathScale
endif
#
# Try and catch the Lawrencium cluster ...
#
ifeq ($(findstring .scs, $(HOSTNAMEF)), .scs)
    FC  = mpif90
    F90 = mpif90
endif
#
# Host changes ....
#
ifeq ($(findstring intrepid, $(HOSTNAMEF)), intrepid)
    #
    # intrepid.alcf.anl.gov -- we only seem to be able to get the name
    #                          intrepid from hostname -f.  $HOST or
    #                          uname -n don't indicate intrepid
    #

    ifdef OMP
      CC  := mpixlc_r
      FC  := mpixlf95_r -qfixed=72
      F90 := mpixlf95_r
    else  
      CC  := mpixlc
      FC  := mpixlf95 -qfixed=72
      F90 := mpixlf95
    endif

    FFLAGS   += -qmoddir=$(mdir) -I$(mdir)
    F90FLAGS += -qmoddir=$(mdir) -I$(mdir)
    CFLAGS   += -I$(mdir) -Wp,-DBL_AIX

    ifdef NDEBUG
      FFLAGS   += -O2 -qarch=450d -qtune=450
      F90FLAGS += -O2 -qarch=450d -qtune=450
      CFLAGS   += -O2 -qarch=450d -qtune=450
    else
      FFLAGS   += -g -C
      F90FLAGS += -g -C
      CFLAGS   += -g -qcheck=bounds
    endif

    F_C_LINK := LOWERCASE

    # if using the bg* compilers instead of the mpi* wrappers above, you may
    # need these
    #
    #    MPIHOME=/bgsys/drivers/ppcfloor
    #    mpi_include_dir = $(MPIHOME)/arch/include -I$(MPIHOME)/comm/include
    #    mpi_lib_dir = $(MPIHOME)/comm/lib -L$(MPIHOME)/runtime/SPI
    #    mpi_libraries += -lmpich.cnk -ldcmfcoll.cnk -ldcmf.cnk 
    #    mpi_libraries += -lpthread -lrt -lSPI.cna
endif

ifeq ($(findstring surveyor, $(HOSTNAME)), surveyor)
    #
    # surveyor.alcf.anl.gov
    #
    CC  := mpixlc
    FC  := mpixlf95 -qfixed=72
    F90 := mpixlf95

    FFLAGS   := -qmoddir=$(mdir) -I$(mdir)
    F90FLAGS := -qmoddir=$(mdir) -I$(mdir)
    CFLAGS   := -I$(mdir) -Wp,-DBL_AIX

    ifdef NDEBUG
      FFLAGS   += -O2 -qarch=450d -qtune=450
      F90FLAGS += -O2 -qarch=450d -qtune=450
      CFLAGS   += -O2 -qarch=450d -qtune=450
    else
      FFLAGS   += -g -C
      F90FLAGS += -g -C
      CFLAGS   += -g -qcheck=bounds
    endif

    F_C_LINK := LOWERCASE

    # if using the bg* compilers instead of the mpi* wrappers above, you may
    # need these
    #
    #    MPIHOME=/bgsys/drivers/ppcfloor
    #    mpi_include_dir = $(MPIHOME)/arch/include -I$(MPIHOME)/comm/include
    #    mpi_lib_dir = $(MPIHOME)/comm/lib -L$(MPIHOME)/runtime/SPI
    #    mpi_libraries += -lmpich.cnk -ldcmfcoll.cnk -ldcmf.cnk 
    #    mpi_libraries += -lpthread -lrt -lSPI.cna
endif

ifeq ($(findstring cvrsvc, $(HOST)), cvrsvc)
    #
    # carver.nersc.gov
    #
    ifdef MPI
        CXX := mpiCC
        FC  := mpif90
        F90 := mpif90
    endif
endif
ifeq ($(findstring nid, $(HOST)), nid)
    #
    # franklin.nersc.gov
    #
    ifdef MPI
        CXX := CC -target=linux
        CC  := cc -target=linux
        FC  := ftn -target=linux
        F90 := ftn -target=linux
    endif
endif
ifeq ($(findstring grace, $(HOST)), grace)
    #
    # grace.nersc.gov
    #
    ifdef MPI
        CXX := CC -target=linux
        CC  := cc -target=linux
        FC  := ftn -target=linux
        F90 := ftn -target=linux
    endif
endif
ifeq ($(findstring hopper, $(HOST)), hopper)
    #
    # hopper.nersc.gov
    #
    ifdef MPI
        CXX := CC -target=linux
        CC  := cc -target=linux
        FC  := ftn -target=linux
        F90 := ftn -target=linux
    endif
endif
ifeq ($(findstring jaguar, $(HOST)), jaguar)
    #
    # jaguar
    #
    ifdef MPI
        CXX := CC -target=linux
        CC  := cc -target=linux
        FC  := ftn -target=linux
        F90 := ftn -target=linux
    endif
endif
ifeq ($(findstring kraken, $(UNAMEN)), kraken)
    #
    # kraken
    #
    ifdef MPI
        CXX := CC -target=linux
        FC  := ftn -target=linux
        F90 := ftn -target=linux
    endif
endif



ifeq ($(HOST),cfe3)
  mpi_libraries += -lmpi
endif

ifeq ($(HOST), orga)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpl -lpthread
  ifeq ($(COMP),g95)
    $(error SORRY NO MPI WITH G95)
  endif
endif

ifeq ($(HOST),naphta)
  MPIHOME=/home/car/mpich2
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = -L$(MPIHOME)/lib
  mpi_libraries += -lmpich
  ifeq ($(COMP),g95)
    $(error SORRY NO MPI WITH G95)
  endif
endif

ifeq ($(HOST),gigan)
  MPIHOME=/usr/local/mpich2
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lpthread
  ifeq ($(COMP),g95)
    $(error SORRY NO MPI WITH G95)
  endif
endif

ifeq ($(HOST),kiryu)
  MPIHOME=/usr/local 
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lpthread
  ifeq ($(COMP),g95)
    $(error SORRY NO MPI WITH G95)
  endif
endif

ifeq ($(HOST),lijewski)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpl -lpthread
  ifeq ($(COMP),g95)
    $(error SORRY NO MPI WITH G95)
  endif
endif
ifeq ($(HOST),manda)
  MPIHOME=/home/almgren/bin/mpich2-install
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpichf90 -lpthread
endif
ifeq ($(HOST),gojira)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpichf90 -lpthread
endif
ifeq ($(HOST),atragon)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lpthread
endif
ifeq ($(HOST),ebirah)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lpthread
endif
ifeq ($(HOST),posse)
  MPIHOME=/usr/lib/mpich
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lpthread
endif
ifeq ($(HOST),mothra)
  F90 = ifort
  CXX = icc
  MPIHOME=/usr/local/mpich2
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpichf90 -lpthread
endif
ifeq ($(HOST),gimantis)
  F90 = ifort
  CXX = icc
  MPIHOME=/usr/local/mpich2
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpichf90 -lpthread
endif
ifeq ($(HOST),angilas)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_libraries += -lmpich -lmpichf90 -lpthread
endif
ifeq ($(findstring donev, $(HOSTNAME)), donev)
   ifeq ($(MPIVENDOR),OpenMPI)
      MPIHOME=$(HOME)/HPC/Libraries/OMPI
      mpi_libraries += -lmpi -lmpi_f77 #-lmpi_f90      
   else
      MPIHOME=$(HOME)/HPC/Libraries/MPI
      mpi_libraries += -lmpich -lmpichf90 -lpthread    
   endif
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = $(MPIHOME)/lib
else ifeq ($(findstring cims.nyu.edu, $(HOSTNAME)), cims.nyu.edu)
   MPIHOME=/usr/lib64/openmpi
   mpi_libraries += -lmpi -lmpi_f77 #-lmpi_f90
   mpi_include_dir = /usr/include/openmpi-x86_64
   mpi_lib_dir = $(MPIHOME)/lib
endif

ifeq ($(HOST),greenstreet)
  MPIHOME=/usr/local/anag/pkg/mpich-1.2.6-intel90
  mpi_lib_dir = $(MPIHOME)/lib
  mpi_include_dir = $(MPIHOME)/include
  mpi_libraries += -lmpich
endif

ifeq ($(findstring nan, $(UNAMEN)), nan)
  MPIHOME=/usr/local/mpich2/
  F90 = mpif90
  CXX = mpicxx
endif

ifeq ($(findstring inf, $(UNAMEN)), inf)
  MPIHOME=/usr/lib64/mpich2
  F90 = mpif90
  CXX = mpicxx
  mpi_lib_dir = $(MPIHOME)/lib
endif

ifeq ($(findstring sn, $(UNAMEN)), sn)
  MPIHOME=/usr/lib64/mpich2
  F90 = mpif90
  CXX = mpicxx
endif

ifeq ($(findstring bender, $(UNAMEN)), bender)
  MPIHOME=/usr/lib64/mpich2
  F90 = mpif90 -f90=gfortran 
  CXX = mpicxx -cxx=gcc
endif

ifeq ($(findstring xrb, $(UNAMEN)), xrb)
  MPIHOME=/usr/lib64/mpich2
  F90 = mpif90
  CXX = mpicxx
endif

# generic linux laptop install with MPICH -- assumes that the MPIHOME 
# environment variable is set
ifeq ($(findstring localhost, $(UNAMEN)), localhost)
  ifeq ($(findstring mpich, $(MPIHOME)), mpich)
    F90 = mpif90
    CXX = mpicxx
  endif
endif

ifeq ($(HOST),lookfar)
  MPIHOME=/usr/local
  mpi_include_dir = $(MPIHOME)/include
  mpi_lib_dir = -L$(MPIHOME)/lib
  mpi_libraries += -lmpich
  ifeq ($(COMP),Intel)
    FFLAGS += -assume 2underscores
    F90FLAGS += -assume 2underscores
    CFLAGS += -DBL_FORT_USE_DBL_UNDERSCORE
    CFLAGS += -UBL_FORT_USE_UNDERSCORE
  endif
endif

