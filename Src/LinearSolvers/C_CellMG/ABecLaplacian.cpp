#include <winstd.H>
#include <algorithm>
#include <ABecLaplacian.H>
#include <ABec_F.H>
#include <ParallelDescriptor.H>

Real ABecLaplacian::a_def     = 0.0;
Real ABecLaplacian::b_def     = 1.0;
Real ABecLaplacian::alpha_def = 1.0;
Real ABecLaplacian::beta_def  = 1.0;

ABecLaplacian::ABecLaplacian (const BndryData& _bd,
                              Real             _h)
    :
    LinOp(_bd,_h),
    alpha(alpha_def),
    beta(beta_def)
{
    initCoefficients(_bd.boxes());
}

ABecLaplacian::ABecLaplacian (const BndryData& _bd,
                              const Real*      _h)
    :
    LinOp(_bd,_h),
    alpha(alpha_def),
    beta(beta_def)
{
    initCoefficients(_bd.boxes());
}

ABecLaplacian::~ABecLaplacian ()
{
    clearToLevel(-1);
}

Real
ABecLaplacian::norm (int nm, int level, const bool local)
{
    BL_ASSERT(nm == 0);
    const MultiFab& a   = aCoefficients(level);

    D_TERM(const MultiFab& bX  = bCoefficients(0,level);,
           const MultiFab& bY  = bCoefficients(1,level);,
           const MultiFab& bZ  = bCoefficients(2,level););

    const int nc = a.nComp();
    Real res = 0.0;
    for (MFIter amfi(a); amfi.isValid(); ++amfi)
    {
        Real tres;

        const Box&       vbx  = amfi.validbox();
        const FArrayBox& afab = a[amfi];

        D_TERM(const FArrayBox& bxfab = bX[amfi];,
               const FArrayBox& byfab = bY[amfi];,
               const FArrayBox& bzfab = bZ[amfi];);

#if (BL_SPACEDIM==2)
        FORT_NORMA(&tres,
                   &alpha, &beta,
                   afab.dataPtr(),  ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                   bxfab.dataPtr(), ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                   byfab.dataPtr(), ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                   vbx.loVect(), vbx.hiVect(), &nc,
                   h[level]);
#elif (BL_SPACEDIM==3)

        FORT_NORMA(&tres,
                   &alpha, &beta,
                   afab.dataPtr(),  ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                   bxfab.dataPtr(), ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                   byfab.dataPtr(), ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                   bzfab.dataPtr(), ARLIM(bzfab.loVect()), ARLIM(bzfab.hiVect()),
                   vbx.loVect(), vbx.hiVect(), &nc,
                   h[level]);
#endif
        res = std::max(res, tres);
    }
    if (!local)
        ParallelDescriptor::ReduceRealMax(res);
    return res;
}

void
ABecLaplacian::clearToLevel (int level)
{
    BL_ASSERT(level >= -1);

    for (int i = level+1; i < numLevels(); ++i)
    {
        delete acoefs[i];
        a_valid[i] = false;
        for (int j = 0; j < BL_SPACEDIM; ++j)
        {
            delete bcoefs[i][j];
        }
        b_valid[i] = false;
    }
}

void
ABecLaplacian::prepareForLevel (int level)
{
    LinOp::prepareForLevel(level);

    if (level == 0 )
        return;

    prepareForLevel(level-1);
    //
    // If coefficients were marked invalid, or if not yet made, make new ones
    // (Note: makeCoefficients is a LinOp routine, and it allocates AND
    // fills coefficients.  A more efficient implementation would allocate
    // and fill in separate steps--we could then use the a_valid bool
    // along with the length of a_valid to separately determine whether to
    // fill or allocate the coefficient MultiFabs.
    //
    if (level >= a_valid.size() || a_valid[level] == false)
    {
        if (acoefs.size() < level+1)
        {
            acoefs.resize(level+1);
            acoefs[level] = new MultiFab;
        }
        else
        {
            delete acoefs[level];
            acoefs[level] = new MultiFab;
        }
        makeCoefficients(*acoefs[level], *acoefs[level-1], level);
        a_valid.resize(level+1);
        a_valid[level] = true;
    }
    
    if (level >= b_valid.size() || b_valid[level] == false)
    {
        if (bcoefs.size() < level+1)
        {
            bcoefs.resize(level+1);
            for(int i = 0; i < BL_SPACEDIM; ++i)
                bcoefs[level][i] = new MultiFab;
        }
        else
        {
            for(int i = 0; i < BL_SPACEDIM; ++i)
            {
                delete bcoefs[level][i];
                bcoefs[level][i] = new MultiFab;
            }
        }
        for (int i = 0; i < BL_SPACEDIM; ++i)
        {
            makeCoefficients(*bcoefs[level][i], *bcoefs[level-1][i], level);
        }
        b_valid.resize(level+1);
        b_valid[level] = true;
    }
}

void
ABecLaplacian::initCoefficients (const BoxArray& _ba)
{
    const int nComp=1;
    const int nGrow=0;
    acoefs.resize(1);
    bcoefs.resize(1);
    acoefs[0] = new MultiFab(_ba, nComp, nGrow, Fab_allocate);
    acoefs[0]->setVal(a_def);
    a_valid.resize(1);
    a_valid[0] = true;

    for (int i = 0; i < BL_SPACEDIM; ++i)
    {
        BoxArray edge_boxes(_ba);
        edge_boxes.surroundingNodes(i);
        bcoefs[0][i] = new MultiFab(edge_boxes, nComp, nGrow, Fab_allocate);
        bcoefs[0][i]->setVal(b_def);
    }
    b_valid.resize(1);
    b_valid[0] = true;
}

void
ABecLaplacian::aCoefficients (const MultiFab& _a)
{
    BL_ASSERT(_a.ok());
    BL_ASSERT(_a.boxArray() == (acoefs[0])->boxArray());
    invalidate_a_to_level(0);
    (*acoefs[0]).copy(_a,0,0,1);
}

void
ABecLaplacian::bCoefficients (const MultiFab& _b,
                              int             dir)
{
    BL_ASSERT(_b.ok());
    BL_ASSERT(_b.boxArray() == (bcoefs[0][dir])->boxArray());
    invalidate_b_to_level(0);
    (*bcoefs[0][dir]).copy(_b,0,0,1);
}

const MultiFab&
ABecLaplacian::aCoefficients (int level)
{
    prepareForLevel(level);
    return *acoefs[level];
}

const MultiFab&
ABecLaplacian::bCoefficients (int dir,int level)
{
    prepareForLevel(level);
    return *bcoefs[level][dir];
}

void
ABecLaplacian::setCoefficients (const MultiFab &_a,
                                const MultiFab &_bX,
                                const MultiFab &_bY)
{
    aCoefficients(_a);
    bCoefficients(_bX, 0);
    bCoefficients(_bY, 1);
}

void
ABecLaplacian::setCoefficients (const MultiFab& _a,
                                const MultiFab* _b)
{
    aCoefficients(_a);
    for (int n = 0; n < BL_SPACEDIM; ++n)
        bCoefficients(_b[n], n);
}

void
ABecLaplacian::invalidate_a_to_level (int lev)
{
    lev = (lev >= 0 ? lev : 0);
    for (int i = lev; i < numLevels(); i++)
        a_valid[i] = false;
}

void
ABecLaplacian::invalidate_b_to_level (int lev)
{
    lev = (lev >= 0 ? lev : 0);
    for (int i = lev; i < numLevels(); i++)
        b_valid[i] = false;
}

void
ABecLaplacian::compFlux (D_DECL(MultiFab &xflux, MultiFab &yflux, MultiFab &zflux),
			 MultiFab& in, const BC_Mode& bc_mode)
{
    compFlux(D_DECL(xflux, yflux, zflux), in, bc_mode, true);
}

void
ABecLaplacian::compFlux (D_DECL(MultiFab &xflux, MultiFab &yflux, MultiFab &zflux),
                         MultiFab& in, const BC_Mode& bc_mode, bool do_ApplyBC)
{
    const int level    = 0;
    const int src_comp = 0;
    const int num_comp = 1;
    if (do_ApplyBC)
        applyBC(in,src_comp,num_comp,level,bc_mode);

    const MultiFab& a = aCoefficients(level);

    D_TERM(const MultiFab& bX = bCoefficients(0,level);,
           const MultiFab& bY = bCoefficients(1,level);,
           const MultiFab& bZ = bCoefficients(2,level););

    const int nc = in.nComp();

    for (MFIter inmfi(in); inmfi.isValid(); ++inmfi)
    {
        const Box& vbx   = inmfi.validbox();
        FArrayBox& infab = in[inmfi];

        D_TERM(const FArrayBox& bxfab = bX[inmfi];,
               const FArrayBox& byfab = bY[inmfi];,
               const FArrayBox& bzfab = bZ[inmfi];);

        D_TERM(FArrayBox& xfluxfab = xflux[inmfi];,
               FArrayBox& yfluxfab = yflux[inmfi];,
               FArrayBox& zfluxfab = zflux[inmfi];);

        FORT_FLUX(infab.dataPtr(),
		  ARLIM(infab.loVect()), ARLIM(infab.hiVect()),
		  &alpha, &beta, a[inmfi].dataPtr(), 
		  ARLIM(a[inmfi].loVect()), ARLIM(a[inmfi].hiVect()),
		  bxfab.dataPtr(), 
		  ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
#if (BL_SPACEDIM >= 2)
		  byfab.dataPtr(), 
		  ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
#if (BL_SPACEDIM == 3)
		  bzfab.dataPtr(), 
		  ARLIM(bzfab.loVect()), ARLIM(bzfab.hiVect()),
#endif
#endif
		  vbx.loVect(), vbx.hiVect(), &nc,
		  h[level],
		  xfluxfab.dataPtr(),
		  ARLIM(xfluxfab.loVect()), ARLIM(xfluxfab.hiVect())
#if (BL_SPACEDIM >= 2)
		  ,yfluxfab.dataPtr(),
		  ARLIM(yfluxfab.loVect()), ARLIM(yfluxfab.hiVect())
#endif
#if (BL_SPACEDIM == 3)
		  ,zfluxfab.dataPtr(),
		  ARLIM(zfluxfab.loVect()), ARLIM(zfluxfab.hiVect())
#endif
		  );
    }
}
        
//
// Must be defined for MultiGrid/CGSolver to work.
//

void
ABecLaplacian::Fsmooth (MultiFab&       solnL,
                        const MultiFab& rhsL,
                        int             level,
                        int             redBlackFlag)
{
    OrientationIter oitr;

    const FabSet& f0 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f1 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f2 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f3 = (*undrrelxr[level])[oitr()]; oitr++;
#if (BL_SPACEDIM > 2)
    const FabSet& f4 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f5 = (*undrrelxr[level])[oitr()]; oitr++;
#endif    
    const MultiFab& a = aCoefficients(level);

    D_TERM(const MultiFab& bX = bCoefficients(0,level);,
           const MultiFab& bY = bCoefficients(1,level);,
           const MultiFab& bZ = bCoefficients(2,level););

    const int nc = solnL.nComp();

    for (MFIter solnLmfi(solnL); solnLmfi.isValid(); ++solnLmfi)
    {
        oitr.rewind();

        const int gn = solnLmfi.index();

        const LinOp::MaskTuple& mtuple = maskvals[level][gn];

        const Mask& m0 = *mtuple[oitr()]; oitr++;
        const Mask& m1 = *mtuple[oitr()]; oitr++;
        const Mask& m2 = *mtuple[oitr()]; oitr++;
        const Mask& m3 = *mtuple[oitr()]; oitr++;
#if (BL_SPACEDIM > 2)
        const Mask& m4 = *mtuple[oitr()]; oitr++;
        const Mask& m5 = *mtuple[oitr()]; oitr++;
#endif
        const Box&       vbx     = solnLmfi.validbox();
        FArrayBox&       solnfab = solnL[gn];
        const FArrayBox& rhsfab  = rhsL[gn];
        const FArrayBox& afab    = a[gn];

        D_TERM(const FArrayBox& bxfab = bX[gn];,
               const FArrayBox& byfab = bY[gn];,
               const FArrayBox& bzfab = bZ[gn];);

        const FArrayBox& f0fab = f0[gn];
        const FArrayBox& f1fab = f1[gn];
        const FArrayBox& f2fab = f2[gn];
        const FArrayBox& f3fab = f3[gn];
#if (BL_SPACEDIM > 2)
        const FArrayBox& f4fab = f4[gn];
        const FArrayBox& f5fab = f5[gn];
#endif

#if (BL_SPACEDIM == 2)
        FORT_GSRB(solnfab.dataPtr(), ARLIM(solnfab.loVect()),ARLIM(solnfab.hiVect()),
                  rhsfab.dataPtr(), ARLIM(rhsfab.loVect()), ARLIM(rhsfab.hiVect()),
                  &alpha, &beta,
                  afab.dataPtr(), ARLIM(afab.loVect()),    ARLIM(afab.hiVect()),
                  bxfab.dataPtr(), ARLIM(bxfab.loVect()),   ARLIM(bxfab.hiVect()),
                  byfab.dataPtr(), ARLIM(byfab.loVect()),   ARLIM(byfab.hiVect()),
                  f0fab.dataPtr(), ARLIM(f0fab.loVect()),   ARLIM(f0fab.hiVect()),
                  m0.dataPtr(), ARLIM(m0.loVect()),   ARLIM(m0.hiVect()),
                  f1fab.dataPtr(), ARLIM(f1fab.loVect()),   ARLIM(f1fab.hiVect()),
                  m1.dataPtr(), ARLIM(m1.loVect()),   ARLIM(m1.hiVect()),
                  f2fab.dataPtr(), ARLIM(f2fab.loVect()),   ARLIM(f2fab.hiVect()),
                  m2.dataPtr(), ARLIM(m2.loVect()),   ARLIM(m2.hiVect()),
                  f3fab.dataPtr(), ARLIM(f3fab.loVect()),   ARLIM(f3fab.hiVect()),
                  m3.dataPtr(), ARLIM(m3.loVect()),   ARLIM(m3.hiVect()),
                  vbx.loVect(), vbx.hiVect(),
                  &nc, h[level], &redBlackFlag);
#endif

#if (BL_SPACEDIM == 3)
        FORT_GSRB(solnfab.dataPtr(), ARLIM(solnfab.loVect()),ARLIM(solnfab.hiVect()),
                  rhsfab.dataPtr(), ARLIM(rhsfab.loVect()), ARLIM(rhsfab.hiVect()),
                  &alpha, &beta,
                  afab.dataPtr(), ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                  bxfab.dataPtr(), ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                  byfab.dataPtr(), ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                  bzfab.dataPtr(), ARLIM(bzfab.loVect()), ARLIM(bzfab.hiVect()),
                  f0fab.dataPtr(), ARLIM(f0fab.loVect()), ARLIM(f0fab.hiVect()),
                  m0.dataPtr(), ARLIM(m0.loVect()), ARLIM(m0.hiVect()),
                  f1fab.dataPtr(), ARLIM(f1fab.loVect()), ARLIM(f1fab.hiVect()),
                  m1.dataPtr(), ARLIM(m1.loVect()), ARLIM(m1.hiVect()),
                  f2fab.dataPtr(), ARLIM(f2fab.loVect()), ARLIM(f2fab.hiVect()),
                  m2.dataPtr(), ARLIM(m2.loVect()), ARLIM(m2.hiVect()),
                  f3fab.dataPtr(), ARLIM(f3fab.loVect()), ARLIM(f3fab.hiVect()),
                  m3.dataPtr(), ARLIM(m3.loVect()), ARLIM(m3.hiVect()),
                  f4fab.dataPtr(), ARLIM(f4fab.loVect()), ARLIM(f4fab.hiVect()),
                  m4.dataPtr(), ARLIM(m4.loVect()), ARLIM(m4.hiVect()),
                  f5fab.dataPtr(), ARLIM(f5fab.loVect()), ARLIM(f5fab.hiVect()),
                  m5.dataPtr(), ARLIM(m5.loVect()), ARLIM(m5.hiVect()),
                  vbx.loVect(), vbx.hiVect(),
                  &nc, h[level], &redBlackFlag);
#endif
    }
}

void
ABecLaplacian::Fsmooth_jacobi (MultiFab&       solnL,
                               const MultiFab& rhsL,
                               int             level)
{
    OrientationIter oitr;

    const FabSet& f0 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f1 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f2 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f3 = (*undrrelxr[level])[oitr()]; oitr++;
#if (BL_SPACEDIM > 2)
    const FabSet& f4 = (*undrrelxr[level])[oitr()]; oitr++;
    const FabSet& f5 = (*undrrelxr[level])[oitr()]; oitr++;
#endif    
    const MultiFab& a = aCoefficients(level);

    D_TERM(const MultiFab& bX = bCoefficients(0,level);,
           const MultiFab& bY = bCoefficients(1,level);,
           const MultiFab& bZ = bCoefficients(2,level););

    const int nc = solnL.nComp();

    for (MFIter solnLmfi(solnL); solnLmfi.isValid(); ++solnLmfi)
    {
        oitr.rewind();

        const int gn = solnLmfi.index();

        const LinOp::MaskTuple& mtuple = maskvals[level][gn];

        const Mask& m0 = *mtuple[oitr()]; oitr++;
        const Mask& m1 = *mtuple[oitr()]; oitr++;
        const Mask& m2 = *mtuple[oitr()]; oitr++;
        const Mask& m3 = *mtuple[oitr()]; oitr++;
#if (BL_SPACEDIM > 2)
        const Mask& m4 = *mtuple[oitr()]; oitr++;
        const Mask& m5 = *mtuple[oitr()]; oitr++;
#endif
        const Box&       vbx     = solnLmfi.validbox();
        FArrayBox&       solnfab = solnL[gn];
        const FArrayBox& rhsfab  = rhsL[gn];
        const FArrayBox& afab    = a[gn];

        D_TERM(const FArrayBox& bxfab = bX[gn];,
               const FArrayBox& byfab = bY[gn];,
               const FArrayBox& bzfab = bZ[gn];);

        const FArrayBox& f0fab = f0[gn];
        const FArrayBox& f1fab = f1[gn];
        const FArrayBox& f2fab = f2[gn];
        const FArrayBox& f3fab = f3[gn];
#if (BL_SPACEDIM > 2)
        const FArrayBox& f4fab = f4[gn];
        const FArrayBox& f5fab = f5[gn];
#endif

#if (BL_SPACEDIM == 2)
        FORT_JACOBI(solnfab.dataPtr(), ARLIM(solnfab.loVect()),ARLIM(solnfab.hiVect()),
                    rhsfab.dataPtr(), ARLIM(rhsfab.loVect()), ARLIM(rhsfab.hiVect()),
                    &alpha, &beta,
                    afab.dataPtr(), ARLIM(afab.loVect()),    ARLIM(afab.hiVect()),
                    bxfab.dataPtr(), ARLIM(bxfab.loVect()),   ARLIM(bxfab.hiVect()),
                    byfab.dataPtr(), ARLIM(byfab.loVect()),   ARLIM(byfab.hiVect()),
                    f0fab.dataPtr(), ARLIM(f0fab.loVect()),   ARLIM(f0fab.hiVect()),
                    m0.dataPtr(), ARLIM(m0.loVect()),   ARLIM(m0.hiVect()),
                    f1fab.dataPtr(), ARLIM(f1fab.loVect()),   ARLIM(f1fab.hiVect()),
                    m1.dataPtr(), ARLIM(m1.loVect()),   ARLIM(m1.hiVect()),
                    f2fab.dataPtr(), ARLIM(f2fab.loVect()),   ARLIM(f2fab.hiVect()),
                    m2.dataPtr(), ARLIM(m2.loVect()),   ARLIM(m2.hiVect()),
                    f3fab.dataPtr(), ARLIM(f3fab.loVect()),   ARLIM(f3fab.hiVect()),
                    m3.dataPtr(), ARLIM(m3.loVect()),   ARLIM(m3.hiVect()),
                    vbx.loVect(), vbx.hiVect(),
                    &nc, h[level]);
#endif

#if (BL_SPACEDIM == 3)
        FORT_JACOBI(solnfab.dataPtr(), ARLIM(solnfab.loVect()),ARLIM(solnfab.hiVect()),
                    rhsfab.dataPtr(), ARLIM(rhsfab.loVect()), ARLIM(rhsfab.hiVect()),
                    &alpha, &beta,
                    afab.dataPtr(), ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                    bxfab.dataPtr(), ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                    byfab.dataPtr(), ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                    bzfab.dataPtr(), ARLIM(bzfab.loVect()), ARLIM(bzfab.hiVect()),
                    f0fab.dataPtr(), ARLIM(f0fab.loVect()), ARLIM(f0fab.hiVect()),
                    m0.dataPtr(), ARLIM(m0.loVect()), ARLIM(m0.hiVect()),
                    f1fab.dataPtr(), ARLIM(f1fab.loVect()), ARLIM(f1fab.hiVect()),
                    m1.dataPtr(), ARLIM(m1.loVect()), ARLIM(m1.hiVect()),
                    f2fab.dataPtr(), ARLIM(f2fab.loVect()), ARLIM(f2fab.hiVect()),
                    m2.dataPtr(), ARLIM(m2.loVect()), ARLIM(m2.hiVect()),
                    f3fab.dataPtr(), ARLIM(f3fab.loVect()), ARLIM(f3fab.hiVect()),
                    m3.dataPtr(), ARLIM(m3.loVect()), ARLIM(m3.hiVect()),
                    f4fab.dataPtr(), ARLIM(f4fab.loVect()), ARLIM(f4fab.hiVect()),
                    m4.dataPtr(), ARLIM(m4.loVect()), ARLIM(m4.hiVect()),
                    f5fab.dataPtr(), ARLIM(f5fab.loVect()), ARLIM(f5fab.hiVect()),
                    m5.dataPtr(), ARLIM(m5.loVect()), ARLIM(m5.hiVect()),
                    vbx.loVect(), vbx.hiVect(),
                    &nc, h[level]);
#endif
    }
}

void
ABecLaplacian::Fapply (MultiFab&       y,
                       const MultiFab& x,
                       int             level)
{
    const MultiFab& a   = aCoefficients(level);

    D_TERM(const MultiFab& bX  = bCoefficients(0,level);,
           const MultiFab& bY  = bCoefficients(1,level);,
           const MultiFab& bZ  = bCoefficients(2,level););

    const int nc = y.nComp();

    for (MFIter ymfi(y); ymfi.isValid(); ++ymfi)
    {
        const Box&       vbx  = ymfi.validbox();
        FArrayBox&       yfab = y[ymfi];
        const FArrayBox& xfab = x[ymfi];
        const FArrayBox& afab = a[ymfi];

        D_TERM(const FArrayBox& bxfab = bX[ymfi];,
               const FArrayBox& byfab = bY[ymfi];,
               const FArrayBox& bzfab = bZ[ymfi];);

#if (BL_SPACEDIM == 2)
        FORT_ADOTX(yfab.dataPtr(),
                   ARLIM(yfab.loVect()),ARLIM(yfab.hiVect()),
                   xfab.dataPtr(),
                   ARLIM(xfab.loVect()), ARLIM(xfab.hiVect()),
                   &alpha, &beta, afab.dataPtr(), 
                   ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                   bxfab.dataPtr(), 
                   ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                   byfab.dataPtr(), 
                   ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                   vbx.loVect(), vbx.hiVect(), &nc,
                   h[level]);
#endif
#if (BL_SPACEDIM ==3)
        FORT_ADOTX(yfab.dataPtr(),
                   ARLIM(yfab.loVect()), ARLIM(yfab.hiVect()),
                   xfab.dataPtr(),
                   ARLIM(xfab.loVect()), ARLIM(xfab.hiVect()),
                   &alpha, &beta, afab.dataPtr(), 
                   ARLIM(afab.loVect()), ARLIM(afab.hiVect()),
                   bxfab.dataPtr(), 
                   ARLIM(bxfab.loVect()), ARLIM(bxfab.hiVect()),
                   byfab.dataPtr(), 
                   ARLIM(byfab.loVect()), ARLIM(byfab.hiVect()),
                   bzfab.dataPtr(), 
                   ARLIM(bzfab.loVect()), ARLIM(bzfab.hiVect()),
                   vbx.loVect(), vbx.hiVect(), &nc,
                   h[level]);
#endif
    }
}
