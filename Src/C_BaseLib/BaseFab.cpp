
#include <winstd.H>

#include <cstring>
#include <cstdlib>

#include <BaseFab.H>
#include <BArena.H>
#include <CArena.H>
#if !(defined(BL_NO_FORT) || defined(WIN32))
#include <SPECIALIZE_F.H>
#endif

long BoxLib::total_bytes_allocated_in_fabs     = 0;
long BoxLib::total_bytes_allocated_in_fabs_hwm = 0;

int BoxLib::BF_init::m_cnt = 0;

namespace
{
    Arena* the_arena = 0;
}

BoxLib::BF_init::BF_init ()
{
    if (m_cnt++ == 0)
    {
        BL_ASSERT(the_arena == 0);

#if defined(BL_COALESCE_FABS)
        the_arena = new CArena;
#else
        the_arena = new BArena;
#endif
    }
}

BoxLib::BF_init::~BF_init ()
{
    if (--m_cnt == 0)
        delete the_arena;
}

Arena*
BoxLib::The_Arena ()
{
    BL_ASSERT(the_arena != 0);

    return the_arena;
}

#if !(defined(BL_NO_FORT) || defined(WIN32))
template<>
void
BaseFab<Real>::performCopy (const BaseFab<Real>& src,
                            const Box&           srcbox,
                            int                  srccomp,
                            const Box&           destbox,
                            int                  destcomp,
                            int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    if (destbox == domain && srcbox == src.box())
    {
        Real*       data_dst = dataPtr(destcomp);
        const Real* data_src = src.dataPtr(srccomp);

        for (long i = 0, N = numcomp*numpts; i < N; i++)
        {
            *data_dst++ = *data_src++;
        }
    }
    else
    {
        const int* destboxlo  = destbox.loVect();
        const int* destboxhi  = destbox.hiVect();
        const int* _th_plo    = loVect();
        const int* _th_phi    = hiVect();
        const int* _x_lo      = srcbox.loVect();
        const int* _x_plo     = src.loVect();
        const int* _x_phi     = src.hiVect();
        Real*       _th_p     = dataPtr(destcomp);
        const Real* _x_p      = src.dataPtr(srccomp);

        FORT_FASTCOPY(_th_p,
                      ARLIM(_th_plo),
                      ARLIM(_th_phi),
                      D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                      D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                      _x_p,
                      ARLIM(_x_plo),
                      ARLIM(_x_phi),
                      D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                      &numcomp);
    }
}

template <>
void
BaseFab<Real>::copyToMem (const Box& srcbox,
                          int        srccomp,
                          int        numcomp,
                          Real*      dst) const
{
    BL_ASSERT(box().contains(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= nComp());

    if (srcbox.ok())
    {
        const Real* data   = dataPtr(srccomp);
        const int* _box_lo = srcbox.loVect(); 
        const int* _box_hi = srcbox.hiVect(); 
        const int* _th_plo = loVect(); 
        const int* _th_phi = hiVect(); 

        FORT_FASTCOPYTOMEM(_box_lo,
                           _box_hi,
                           data,
                           ARLIM(_th_plo),
                           ARLIM(_th_phi),
                           &numcomp,
                           dst);
    }
}

template <>
void
BaseFab<Real>::copyFromMem (const Box&  dstbox,
                            int         dstcomp,
                            int         numcomp,
                            const Real* src)
{
    BL_ASSERT(box().contains(dstbox));
    BL_ASSERT(dstcomp >= 0 && dstcomp+numcomp <= nComp());

    if (dstbox.ok()) 
    {
        Real*      data    = dataPtr(dstcomp);
        const int* _box_lo = dstbox.loVect(); 
        const int* _box_hi = dstbox.hiVect(); 
        const int* _th_plo = loVect(); 
        const int* _th_phi = hiVect(); 

        FORT_FASTCOPYFROMMEM(_box_lo,
                             _box_hi,
                             data,
                             ARLIM(_th_plo),
                             ARLIM(_th_phi),
                             &numcomp,
                             src);
    }
}

template<>
void
BaseFab<Real>::performSetVal (Real       val,
                              const Box& bx,
                              int        comp,
                              int        ncomp)
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    Real* data = dataPtr(comp);

    if (bx == domain)
    {
        for (long i = 0, N = ncomp*numpts; i < N; i++)
        {
            *data++ = val;
        }
    }
    else
    {
        const int* _box_lo = bx.loVect(); 
        const int* _box_hi = bx.hiVect(); 
        const int* _th_plo = loVect(); 
        const int* _th_phi = hiVect(); 

        FORT_FASTSETVAL(&val,
                        _box_lo,
                        _box_hi,
                        data,
                        ARLIM(_th_plo),
                        ARLIM(_th_phi),
                        &ncomp);
    }
}

template<>
BaseFab<Real>&
BaseFab<Real>::invert (Real       val,
                       const Box& bx,
                       int        comp,
                       int        ncomp)
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    const int* _box_lo = bx.loVect();            
    const int* _box_hi = bx.hiVect();            
    const int* _datalo = loVect();                           
    const int* _datahi = hiVect();

    const Real* _data = dataPtr(comp);

    FORT_FASTINVERT(_data,
                    ARLIM(_datalo),
                    ARLIM(_datahi),
                    _box_lo,
                    _box_hi,
                    &val,
                    &ncomp);
    return *this;
}

template<>
Real
BaseFab<Real>::norm (const Box& bx,
                     int        p,
                     int        comp,
                     int        ncomp) const
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    const int* _box_lo = bx.loVect();            
    const int* _box_hi = bx.hiVect();            
    const int* _datalo = loVect();                           
    const int* _datahi = hiVect();

    const Real* _data = dataPtr(comp);

    Real nrm = 0;

    if (p == 0)
    {
        FORT_FASTZERONORM(_data,
                          ARLIM(_datalo),
                          ARLIM(_datahi),
                          _box_lo,
                          _box_hi,
                          &ncomp,
                          &nrm);
    }
    else if (p == 1)
    {
        FORT_FASTONENORM(_data,
                         ARLIM(_datalo),
                         ARLIM(_datahi),
                         _box_lo,
                         _box_hi,
                         &ncomp,
                         &nrm);
    }
    else
    {
        BoxLib::Error("BaseFab<Real>::norm(): only p == 0 or p == 1 are supported");
    }

    return nrm;
}

template<>
Real
BaseFab<Real>::sum (const Box& bx,
                    int        comp,
                    int        ncomp) const
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    const int* _box_lo = bx.loVect();            
    const int* _box_hi = bx.hiVect();            
    const int* _datalo = loVect();                           
    const int* _datahi = hiVect();

    const Real* _data = dataPtr(comp);

    Real sm = 0;

    FORT_FASTSUM(_data,
                 ARLIM(_datalo),
                 ARLIM(_datahi),
                 _box_lo,
                 _box_hi,
                 &ncomp,
                 &sm);

    return sm;
}

template<>
BaseFab<Real>&
BaseFab<Real>::plus (const BaseFab<Real>& src,
                     const Box&           srcbox,
                     const Box&           destbox,
                     int                  srccomp,
                     int                  destcomp,
                     int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTPLUS(_th_p,
                  ARLIM(_th_plo),
                  ARLIM(_th_phi),
                  D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                  D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                  _x_p,
                  ARLIM(_x_plo),
                  ARLIM(_x_phi),
                  D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                  &numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::mult (const BaseFab<Real>& src,
                     const Box&           srcbox,
                     const Box&           destbox,
                     int                  srccomp,
                     int                  destcomp,
                     int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTMULT(_th_p,
                  ARLIM(_th_plo),
                  ARLIM(_th_phi),
                  D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                  D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                  _x_p,
                  ARLIM(_x_plo),
                  ARLIM(_x_phi),
                  D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                  &numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::saxpy (Real a, const BaseFab<Real>& src,
                      const Box&        srcbox,
                      const Box&        destbox,
                      int               srccomp,
                      int               destcomp,
                      int               numcomp)
{
    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTSAXPY(_th_p,
                   ARLIM(_th_plo),
                   ARLIM(_th_phi),
                   D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                   D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                   &a,
                   _x_p,
                   ARLIM(_x_plo),
                   ARLIM(_x_phi),
                   D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                   &numcomp);

    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::minus (const BaseFab<Real>& src,
                      const Box&           srcbox,
                      const Box&           destbox,
                      int                  srccomp,
                      int                  destcomp,
                      int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTMINUS(_th_p,
                   ARLIM(_th_plo),
                   ARLIM(_th_phi),
                   D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                   D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                   _x_p,
                   ARLIM(_x_plo),
                   ARLIM(_x_phi),
                   D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                   &numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::divide (const BaseFab<Real>& src,
                       const Box&           srcbox,
                       const Box&           destbox,
                       int                  srccomp,
                       int                  destcomp,
                       int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTDIVIDE(_th_p,
                    ARLIM(_th_plo),
                    ARLIM(_th_phi),
                    D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                    D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                    _x_p,
                    ARLIM(_x_plo),
                    ARLIM(_x_phi),
                    D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                    &numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::protected_divide (const BaseFab<Real>& src,
                                 const Box&           srcbox,
                                 const Box&           destbox,
                                 int                  srccomp,
                                 int                  destcomp,
                                 int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    const int* destboxlo  = destbox.loVect();
    const int* destboxhi  = destbox.hiVect();
    const int* _th_plo    = loVect();
    const int* _th_phi    = hiVect();
    const int* _x_lo      = srcbox.loVect();
    const int* _x_plo     = src.loVect();
    const int* _x_phi     = src.hiVect();
    Real*       _th_p     = dataPtr(destcomp);
    const Real* _x_p      = src.dataPtr(srccomp);

    FORT_FASTPROTDIVIDE(_th_p,
                        ARLIM(_th_plo),
                        ARLIM(_th_phi),
                        D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
                        D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
                        _x_p,
                        ARLIM(_x_plo),
                        ARLIM(_x_phi),
                        D_DECL(_x_lo[0],_x_lo[1],_x_lo[2]),
                        &numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::polyInterp (const Array< BaseFab<Real>* > &sf,
			   int                            scomp,
			   const Array<Real>&             st,
			   Real                           t,
			   const Box&                     b,
			   int                            comp,
			   int                            numcomp)
{
    int nsf = sf.size();

    BL_ASSERT( nsf >= 3 );
    BL_ASSERT( comp >= 0 && comp + numcomp <= nComp() );

    BL_ASSERT( scomp >=0 );
    for (int i=0; i<nsf; i++) {
	BL_ASSERT( scomp + numcomp <= sf[i]->nComp() );
    }

    const int *sflo = sf[0]->loVect();
    const int *sflen = sf[0]->length();
    for (int i=1; i<nsf; i++) {
	BL_ASSERT( sflo[0] == (sf[i]->loVect())[0] );
	BL_ASSERT( sflen[0] == (sf[i]->length())[0] );
#if (BL_SPACEDIM == 2)
	BL_ASSERT( sflo[1] == (sf[i]->loVect())[1] );
	BL_ASSERT( sflen[1] == (sf[i]->length())[1] );
#elif (BL_SPACEDIM == 3)
	BL_ASSERT( sflo[2] == (sf[i]->loVect())[2] );
	BL_ASSERT( sflen[2] == (sf[i]->length())[2] );
#endif
    }

    Box destbox = box();
    destbox &= b;

    if (destbox.ok())
    {
	Real* p               = dataPtr(comp);
        const int* lo         = loVect();
        const int* hi         = hiVect();
	const Real* p1        = sf[0]->dataPtr(scomp); 
        const int* lo1        = sf[0]->loVect();
        const int* hi1        = sf[0]->hiVect();
	const Real* p2        = sf[1]->dataPtr(scomp); 
        const int* lo2        = sf[1]->loVect();
        const int* hi2        = sf[1]->hiVect();
	const Real* p3        = sf[2]->dataPtr(scomp); 
        const int* lo3        = sf[2]->loVect();
        const int* hi3        = sf[2]->hiVect();
        const int* destboxlo  = destbox.loVect();
        const int* destboxhi  = destbox.hiVect();

	if (nsf == 3) 
	{
	    BoxLib::Error("BaseFab<Real>::polyInterp: nsf = 3 TODO");
	    FORT_QUADINTERP(p,
	    		    ARLIM(lo),
	    		    ARLIM(hi),
	    		    D_DECL(destboxlo[0],destboxlo[1],destboxlo[2]),
	    		    D_DECL(destboxhi[0],destboxhi[1],destboxhi[2]),
	    		    p1,
	    		    ARLIM(lo1),
	    		    ARLIM(hi1),
	    		    p2,
	    		    ARLIM(lo2),
	    		    ARLIM(hi2),
	    		    p3,
	    		    ARLIM(lo3),
	    		    ARLIM(hi3),
	    		    &numcomp,
	    	            &t,
	    	            &st[0],
	    	            &st[1],
	    	            &st[2]);
			    
	}
	else
	{
	    BoxLib::Error("BaseFab<Real>::polyInterp: nsf > 3 TODO");
	}
    }

    return *this;
}

#endif
