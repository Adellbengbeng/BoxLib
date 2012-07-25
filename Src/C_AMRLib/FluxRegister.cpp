
#include <winstd.H>
#include <BArena.H>
#include <FluxRegister.H>
#include <Geometry.H>
#include <FLUXREG_F.H>
#include <ParallelDescriptor.H>
#include <ccse-mpi.H>

#include <deque>
#include <vector>

FluxRegister::FluxRegister ()
{
    fine_level = ncomp = -1;
    ratio = IntVect::TheUnitVector();
    ratio.scale(-1);
}

FluxRegister::FluxRegister (const BoxArray& fine_boxes, 
                            const IntVect&  ref_ratio,
                            int             fine_lev,
                            int             nvar)
{
    define(fine_boxes,ref_ratio,fine_lev,nvar);
}

FluxRegister::FluxRegister (const BoxArray&            fine_boxes, 
                            const IntVect&             ref_ratio,
                            int                        fine_lev,
                            int                        nvar,
                            const DistributionMapping& dm)
{
    define(fine_boxes,ref_ratio,fine_lev,nvar,dm);
}

const IntVect&
FluxRegister::refRatio () const
{
    return ratio;
}

int
FluxRegister::fineLevel () const
{
    return fine_level;
}

int
FluxRegister::crseLevel () const
{
    return fine_level-1;
}

int
FluxRegister::nComp () const
{
    return ncomp;
}

const BoxArray&
FluxRegister::coarsenedBoxes () const
{
    return grids;
}

void
FluxRegister::define (const BoxArray& fine_boxes, 
                      const IntVect&  ref_ratio,
                      int             fine_lev,
                      int             nvar)
{
    BL_ASSERT(fine_boxes.isDisjoint());
    BL_ASSERT(grids.size() == 0);

    ratio      = ref_ratio;
    fine_level = fine_lev;
    ncomp      = nvar;

    grids.define(fine_boxes);
    grids.coarsen(ratio);

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        const Orientation lo_face(dir,Orientation::low);
        const Orientation hi_face(dir,Orientation::high);

        IndexType typ(IndexType::TheCellType());

        typ.setType(dir,IndexType::NODE);

        BndryRegister::define(lo_face,typ,0,1,0,nvar);
        BndryRegister::define(hi_face,typ,0,1,0,nvar);
    }
}

void
FluxRegister::define (const BoxArray&            fine_boxes, 
                      const IntVect&             ref_ratio,
                      int                        fine_lev,
                      int                        nvar,
                      const DistributionMapping& dm)
{
    BL_ASSERT(fine_boxes.isDisjoint());
    BL_ASSERT(grids.size() == 0);

    ratio      = ref_ratio;
    fine_level = fine_lev;
    ncomp      = nvar;

    grids.define(fine_boxes);
    grids.coarsen(ratio);

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        const Orientation lo_face(dir,Orientation::low);
        const Orientation hi_face(dir,Orientation::high);

        IndexType typ(IndexType::TheCellType());

        typ.setType(dir,IndexType::NODE);

        BndryRegister::define(lo_face,typ,0,1,0,nvar,dm);
        BndryRegister::define(hi_face,typ,0,1,0,nvar,dm);
    }
}

FluxRegister::~FluxRegister () {}

Real
FluxRegister::SumReg (int comp) const
{
    Real sum = 0.0;

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        const FabSet& lofabs = bndry[Orientation(dir,Orientation::low)];
        const FabSet& hifabs = bndry[Orientation(dir,Orientation::high)];

        for (FabSetIter fsi(lofabs); fsi.isValid(); ++fsi)
        {
            sum += lofabs[fsi].sum(comp);
            sum -= hifabs[fsi].sum(comp);
        }
    }

    ParallelDescriptor::ReduceRealSum(sum);

    return sum;
}

void
FluxRegister::copyTo (FArrayBox& flx,
                      int        dir,
                      int        src_comp,
                      int        dest_comp,
                      int        num_comp)
{
    BL_ASSERT(dir >= 0 && dir < BL_SPACEDIM);

    const FabSet& lofabs = bndry[Orientation(dir,Orientation::low)];
    const FabSet& hifabs = bndry[Orientation(dir,Orientation::high)];

    lofabs.copyTo(flx,src_comp,dest_comp,num_comp);
    hifabs.copyTo(flx,src_comp,dest_comp,num_comp);
}

void
FluxRegister::Reflux (MultiFab&       S,
                      const MultiFab& volume,
                      Real            scale,
                      int             src_comp,
                      int             dest_comp,
                      int             num_comp, 
                      const Geometry& geom,
		      const Real*     multf)
{
    FabSetCopyDescriptor fscd;

    FabSetId fsid[2*BL_SPACEDIM];

    for (OrientationIter fi; fi; ++fi)
    {
        fsid[fi()] = fscd.RegisterFabSet(&bndry[fi()]);
    }

    std::deque<FluxRegister::Rec> Recs;

    BoxArray ba = grids;
    ba.grow(1);

    std::vector< std::pair<int,Box> > isects;

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int  idx = mfi.index();
        const Box& vbx = mfi.validbox();
        //
        // Find flux register that intersect with this grid.
        //
        ba.intersections(vbx,isects);

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            const int k = isects[i].first;

            for (OrientationIter fi; fi; ++fi)
            {
                //
                // low (high) face of fine grid => high (low)
                // face of the exterior coarse grid cell updated.
                //
                const Orientation face = fi();

                const Box ovlp = vbx & BoxLib::adjCell(grids[k],face);

                if (ovlp.ok())
                {
                    FillBoxId fbid = fscd.AddBox(fsid[face],
                                                 bndry[face].box(k),
                                                 0,
                                                 k,
                                                 src_comp,
                                                 0,
                                                 num_comp);

                    Recs.push_back(Rec(idx,k,face,fbid));
                }
            }
        }
    }
    //
    // Add periodic possibilities.
    //
    if (geom.isAnyPeriodic())
    {
        Array<IntVect>  pshifts(27);

        for (MFIter mfi(S); mfi.isValid(); ++mfi)
        {
            const int        idx  = mfi.index();
            const Box&       vbx  = mfi.validbox();
            FArrayBox&       sfab = S[mfi];
            const FArrayBox& vfab = volume[mfi];

            for (int k = 0, N = grids.size(); k < N; k++)
            {
                const Box& bx = ba[k];

                if (!geom.Domain().contains(bx))
                {
                    geom.periodicShift(bx,vbx,pshifts);

                    const Box& kgrid = grids[k];

                    for (int iiv = 0, M = pshifts.size(); iiv < M; iiv++)
                    {
                        const IntVect& iv = pshifts[iiv];
                        sfab.shift(iv);
                        //
                        // This is a funny situation.  I don't want to permanently
                        // change vol, but I need to do a shift on it.  I'll shift
                        // it back later, so the overall change is nil.  But to do
                        // this, I have to cheat and do a cast.  This is pretty 
                        // disgusting.
                        //
                        FArrayBox* cheatvol = const_cast<FArrayBox*>(&vfab);
                        BL_ASSERT(cheatvol != 0);
                        cheatvol->shift(iv);
                        Box sftbox = vbx;
                        sftbox.shift(iv);
                        BL_ASSERT(bx.intersects(sftbox));

                        for (OrientationIter fi; fi; ++fi)
                        {
                            //
                            // low (high)  face of fine grid => high (low)
                            // face of the exterior coarse grid cell updated.
                            //
                            const Orientation face = fi();

                            const Box ovlp = sftbox & BoxLib::adjCell(kgrid,face);

                            if (ovlp.ok())
                            {
                                FillBoxId fbid = fscd.AddBox(fsid[face],
                                                             bndry[face].box(k),
                                                             0,
                                                             k,
                                                             src_comp,
                                                             0,
                                                             num_comp);

                                Recs.push_back(Rec(iv,idx,k,face,fbid));
                            }
                        }
                        sfab.shift(-iv);
                        cheatvol->shift(-iv);
                    }
                }
            }
        }
    }

    fscd.CollectData();

    FArrayBox reg;

    for (std::deque<FluxRegister::Rec>::const_iterator it = Recs.begin(),
             End = Recs.end();
         it != End;
         ++it)
    {
        const Rec&       rf   = *it;
        const FillBoxId& fbid = rf.m_fbid;

        BL_ASSERT(bndry[rf.m_face].box(rf.m_idx) == fbid.box());
        BL_ASSERT(S.DistributionMap()[rf.m_fabidx] == ParallelDescriptor::MyProc());
        BL_ASSERT(volume.DistributionMap()[rf.m_fabidx] == ParallelDescriptor::MyProc());

        FArrayBox&       fab_S      = S[rf.m_fabidx];
        const FArrayBox& fab_volume = volume[rf.m_fabidx];
        Real*            s_dat      = fab_S.dataPtr(dest_comp);
        const int*       slo        = fab_S.loVect();
        const int*       shi        = fab_S.hiVect();
        const Real*      vol_dat    = fab_volume.dataPtr();
        Box              fine_face  = BoxLib::adjCell(grids[rf.m_idx],rf.m_face);
        const int*       rlo        = fine_face.loVect();
        const int*       rhi        = fine_face.hiVect();

	Real mult;
	if (multf == 0)
	  mult = rf.m_face.isLow() ? -scale : scale;
	else
	  mult = (*multf)*scale;
	  
        if (!rf.m_shifted)
        {
            const Box ovlp = S.box(rf.m_fabidx) & fine_face;

            BL_ASSERT(ovlp.ok());

            reg.resize(fbid.box(), num_comp);
            fscd.FillFab(fsid[rf.m_face], fbid, reg);

            const Real* reg_dat = reg.dataPtr(0);
            const int*  vlo     = fab_volume.loVect();
            const int*  vhi     = fab_volume.hiVect();
            const int*  lo      = ovlp.loVect();
            const int*  hi      = ovlp.hiVect();

            FORT_FRREFLUX(s_dat,ARLIM(slo),ARLIM(shi),
                          vol_dat,ARLIM(vlo),ARLIM(vhi),
                          reg_dat,ARLIM(rlo),ARLIM(rhi),
                          lo,hi,&num_comp,&mult);
        }
        else
        {
            fab_S.shift(rf.m_shift);
            //
            // This is a funny situation.  I don't want to permanently
            // change vol, but I need to do a shift on it.  I'll shift
            // it back later, so the overall change is nil.  But to do
            // this, I have to cheat and do a cast.  This is pretty 
            // disgusting.
            //
            FArrayBox* cheatvol = const_cast<FArrayBox*>(&fab_volume);
            BL_ASSERT(cheatvol != 0);
            cheatvol->shift(rf.m_shift);
            Box sftbox = S.box(rf.m_fabidx);
            sftbox.shift(rf.m_shift);
            Box ovlp = sftbox & fine_face;

            BL_ASSERT(ovlp.ok());

            reg.resize(fbid.box(), num_comp);
            fscd.FillFab(fsid[rf.m_face], fbid, reg);

            const Real* reg_dat = reg.dataPtr(0);
            const int*  vlo      = cheatvol->loVect();
            const int*  vhi      = cheatvol->hiVect();
            const int*  lo       = ovlp.loVect();
            const int*  hi       = ovlp.hiVect();

            FORT_FRREFLUX(s_dat,ARLIM(slo),ARLIM(shi),
                          vol_dat,ARLIM(vlo),ARLIM(vhi),
                          reg_dat,ARLIM(rlo),ARLIM(rhi),lo,hi,
                          &num_comp,&mult);
            fab_S.shift(-rf.m_shift);
            cheatvol->shift(-rf.m_shift);
        }
    }
}

void
FluxRegister::Reflux (MultiFab&       S,
                      Real            scale,
                      int             src_comp,
                      int             dest_comp,
                      int             num_comp, 
                      const Geometry& geom)
{
    const Real* dx = geom.CellSize();

    FabSetCopyDescriptor fscd;

    FabSetId fsid[2*BL_SPACEDIM];

    for (OrientationIter fi; fi; ++fi)
    {
        fsid[fi()] = fscd.RegisterFabSet(&bndry[fi()]);
    }

    std::deque<FluxRegister::Rec> Recs;

    BoxArray ba = grids;
    ba.grow(1);

    std::vector< std::pair<int,Box> > isects;

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        //
        // Find flux register that intersect with this grid.
        //
        ba.intersections(mfi.validbox(),isects);

        const int  idx = mfi.index();
        const Box& vbx = mfi.validbox();

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            const int  k     = isects[i].first;
            const Box& kgrid = grids[k];

            for (OrientationIter fi; fi; ++fi)
            {
                //
                // low (high) face of fine grid => high (low)
                // face of the exterior coarse grid cell updated.
                //
                const Box ovlp = vbx & BoxLib::adjCell(kgrid,fi());

                if (ovlp.ok())
                {
                    FillBoxId fbid = fscd.AddBox(fsid[fi()],
                                                 bndry[fi()].box(k),
                                                 0,
                                                 k,
                                                 src_comp,
                                                 0,
                                                 num_comp);

                    Recs.push_back(Rec(idx,k,fi(),fbid));
                }
            }
        }
    }
    //
    // Add periodic possibilities.
    //
    if (geom.isAnyPeriodic())
    {
        Array<IntVect>  pshifts(27);

        for (MFIter mfi(S); mfi.isValid(); ++mfi)
        {
            const int  idx = mfi.index();
            const Box& vbx = mfi.validbox();
            FArrayBox& sfab = S[mfi];

            for (int k = 0, N = grids.size(); k < N; k++)
            {
                const Box& bx = ba[k];

                if (!geom.Domain().contains(bx))
                {
                    geom.periodicShift(bx,vbx,pshifts);

                    for (int iiv = 0, M = pshifts.size(); iiv < M; iiv++)
                    {
                        const IntVect& iv = pshifts[iiv];
                        sfab.shift(iv);
                        Box sftbox = vbx;
                        sftbox.shift(iv);
                        BL_ASSERT(bx.intersects(sftbox));

                        for (OrientationIter fi; fi; ++fi)
                        {
                            //
                            // low (high) face of fine grid => high (low)
                            // face of the exterior coarse grid cell updated.
                            //
                            const Box ovlp = sftbox & BoxLib::adjCell(grids[k],fi());

                            if (ovlp.ok())
                            {
                                FillBoxId fbid = fscd.AddBox(fsid[fi()],
                                                             bndry[fi()].box(k),
                                                             0,
                                                             k,
                                                             src_comp,
                                                             0,
                                                             num_comp);

                                Recs.push_back(Rec(iv,idx,k,fi(),fbid));
                            }
                        }
                        sfab.shift(-iv);
                    }
                }
            }
        }
    }

    fscd.CollectData();

    FArrayBox reg;

    for (std::deque<FluxRegister::Rec>::const_iterator it = Recs.begin(),
             End = Recs.end();
         it != End;
         ++it)
    {
        const Rec&       rf   = *it;
        const FillBoxId& fbid = rf.m_fbid;

        BL_ASSERT(bndry[rf.m_face].box(rf.m_idx) == fbid.box());
        BL_ASSERT(S.DistributionMap()[rf.m_fabidx] == ParallelDescriptor::MyProc());

        FArrayBox& fab_S     = S[rf.m_fabidx];
        Box        fine_face = BoxLib::adjCell(grids[rf.m_idx],rf.m_face);
        Real       mult      = rf.m_face.isLow() ? -scale : scale;
        const int* rlo       = fine_face.loVect();
        const int* rhi       = fine_face.hiVect();
        Real*      s_dat     = fab_S.dataPtr(dest_comp);
        const int* slo       = fab_S.loVect();
        const int* shi       = fab_S.hiVect();

        if (!rf.m_shifted)
        {
            const Box ovlp = S.box(rf.m_fabidx) & fine_face;

            BL_ASSERT(ovlp.ok());

            reg.resize(fbid.box(), num_comp);
            fscd.FillFab(fsid[rf.m_face], fbid, reg);

            const Real* reg_dat = reg.dataPtr(0);
            const int*  lo      = ovlp.loVect();
            const int*  hi      = ovlp.hiVect();

            FORT_FRCVREFLUX(s_dat,ARLIM(slo),ARLIM(shi),dx,
                            reg_dat,ARLIM(rlo),ARLIM(rhi),lo,hi,
                            &num_comp,&mult);
        }
        else
        {
            fab_S.shift(rf.m_shift);
            Box sftbox = S.box(rf.m_fabidx);
            sftbox.shift(rf.m_shift);
            Box ovlp = sftbox & fine_face;

            BL_ASSERT(ovlp.ok());

            reg.resize(fbid.box(), num_comp);
            fscd.FillFab(fsid[rf.m_face], fbid, reg);

            const Real* reg_dat = reg.dataPtr(0);
            const int*  lo      = ovlp.loVect();
            const int*  hi      = ovlp.hiVect();

            FORT_FRCVREFLUX(s_dat,ARLIM(slo),ARLIM(shi),dx,
                            reg_dat,ARLIM(rlo),ARLIM(rhi),
                            lo,hi,&num_comp,&mult);

            fab_S.shift(-rf.m_shift);
        }
    }
}

void
FluxRegister::CrseInit (const MultiFab& mflx,
                        const MultiFab& area,
                        int             dir,
                        int             srccomp,
                        int             destcomp,
                        int             numcomp,
                        Real            mult,
                        FrOp            op)
{
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= mflx.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= ncomp);

    const Orientation face_lo(dir,Orientation::low);
    const Orientation face_hi(dir,Orientation::high);

    MultiFabCopyDescriptor mfcd;

    MultiFabId mfid_mflx = mfcd.RegisterFabArray(const_cast<MultiFab*>(&mflx));
    MultiFabId mfid_area = mfcd.RegisterFabArray(const_cast<MultiFab*>(&area));

    std::vector<FillBoxId> fillBoxId_mflx, fillBoxId_area;

    fillBoxId_mflx.reserve(32);
    fillBoxId_area.reserve(32);

    std::vector< std::pair<int,Box> > isects;

    for (FabSetIter mfi_lo(bndry[face_lo]); mfi_lo.isValid(); ++mfi_lo)
    {
        const int index = mfi_lo.index();

        mflx.boxArray().intersections(bndry[face_lo][mfi_lo].box(),isects);

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            const int  k     = isects[i].first;
            const Box& lobox = isects[i].second;

            fillBoxId_mflx.push_back(mfcd.AddBox(mfid_mflx,
                                                 lobox,
                                                 0,
                                                 k,
                                                 srccomp,
                                                 0,
                                                 numcomp));

            BL_ASSERT(fillBoxId_mflx.back().box() == lobox);
            //
            // Here we'll save the index into the FabSet.
            //
            fillBoxId_mflx.back().FabIndex(index);

            fillBoxId_area.push_back(mfcd.AddBox(mfid_area,
                                                 lobox,
                                                 0,
                                                 k,
                                                 0,
                                                 0,
                                                 1));

            BL_ASSERT(fillBoxId_area.back().box() == lobox);
            //
            // Here we'll save the direction.
            //
            fillBoxId_area.back().FabIndex(Orientation::low);
        }

        mflx.boxArray().intersections(bndry[face_hi][mfi_lo].box(),isects);

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            const int  k     = isects[i].first;
            const Box& hibox = isects[i].second;

            fillBoxId_mflx.push_back(mfcd.AddBox(mfid_mflx,
                                                 hibox,
                                                 0,
                                                 k,
                                                 srccomp,
                                                 0,
                                                 numcomp));

            BL_ASSERT(fillBoxId_mflx.back().box() == hibox);
            //
            // Here we'll save the index into the FabSet.
            //
            fillBoxId_mflx.back().FabIndex(index);

            fillBoxId_area.push_back(mfcd.AddBox(mfid_area,
                                                 hibox,
                                                 0,
                                                 k,
                                                 0,
                                                 0,
                                                 1));

            BL_ASSERT(fillBoxId_area.back().box() == hibox);
            //
            // Here we'll save the direction.
            //
            fillBoxId_area.back().FabIndex(Orientation::high);
        }
    }

    mfcd.CollectData();

    BL_ASSERT(fillBoxId_mflx.size() == fillBoxId_area.size());

    FArrayBox mflx_fab, area_fab, tmp_fab;

    const int N = fillBoxId_mflx.size();

    for (int i = 0; i < N; i++)
    {
        const FillBoxId& fbid_mflx = fillBoxId_mflx[i];
        const FillBoxId& fbid_area = fillBoxId_area[i];

        BL_ASSERT(fbid_mflx.box() == fbid_area.box());

        const Orientation the_face(dir,Orientation::Side(fbid_area.FabIndex()));

        BL_ASSERT(the_face == face_lo || the_face == face_hi);

        mflx_fab.resize(fbid_mflx.box(), numcomp);
        area_fab.resize(fbid_mflx.box(), 1);
        mfcd.FillFab(mfid_mflx, fbid_mflx, mflx_fab);
        mfcd.FillFab(mfid_area, fbid_area, area_fab);

        FabSet&   fabset   = bndry[the_face];
        const int fabindex = fbid_mflx.FabIndex();

        BL_ASSERT(fabset.DistributionMap()[fabindex] == ParallelDescriptor::MyProc());

        FArrayBox&  fab      = fabset[fabindex];
        tmp_fab.resize(fabset[fabindex].box(),numcomp);
        const int*  flo      = mflx_fab.box().loVect();
        const int*  fhi      = mflx_fab.box().hiVect();
        const Real* flx_dat  = mflx_fab.dataPtr();
        const int*  alo      = area_fab.box().loVect();
        const int*  ahi      = area_fab.box().hiVect();
        const Real* area_dat = area_fab.dataPtr();
        const int*  rlo      = tmp_fab.loVect();
        const int*  rhi      = tmp_fab.hiVect();
        Real*       lodat    = tmp_fab.dataPtr();
        const int*  lo       = fbid_mflx.box().loVect();
        const int*  hi       = fbid_mflx.box().hiVect();
        FORT_FRCAINIT(lodat,ARLIM(rlo),ARLIM(rhi),
                      flx_dat,ARLIM(flo),ARLIM(fhi),
                      area_dat,ARLIM(alo),ARLIM(ahi),
                      lo,hi,&numcomp,&dir,&mult);
        if (op == COPY)
        {
            fab.copy(tmp_fab,fbid_mflx.box(),0,fbid_mflx.box(),destcomp,numcomp);
        }
        else
        {
            fab.plus(tmp_fab,fbid_mflx.box(),0,destcomp,numcomp);
        }
    }
}

void
FluxRegister::CrseInit (const MultiFab& mflx,
                        int             dir,
                        int             srccomp,
                        int             destcomp,
                        int             numcomp,
                        Real            mult,
                        FrOp            op)
{
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= mflx.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= ncomp);

    const Orientation face_lo(dir,Orientation::low);
    const Orientation face_hi(dir,Orientation::high);

    MultiFabCopyDescriptor mfcd;

    MultiFabId mfid = mfcd.RegisterFabArray(const_cast<MultiFab*>(&mflx));

    std::vector<int>       side;
    std::vector<FillBoxId> fillBoxId;

    side.reserve(32);
    fillBoxId.reserve(32);

    std::vector< std::pair<int,Box> > isects;

    for (FabSetIter mfi_lo(bndry[face_lo]); mfi_lo.isValid(); ++mfi_lo)
    {
        mflx.boxArray().intersections(bndry[face_lo][mfi_lo].box(),isects);

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            fillBoxId.push_back(mfcd.AddBox(mfid,
                                            isects[i].second,
                                            0,
                                            isects[i].first,
                                            srccomp,
                                            0,
                                            numcomp));

            BL_ASSERT(fillBoxId.back().box() == isects[i].second);
            //
            // Here we'll save the index into the FabSet.
            //
            fillBoxId.back().FabIndex(mfi_lo.index());
            //
            // Here we'll save the direction.
            //
            side.push_back(Orientation::low);
        }

        mflx.boxArray().intersections(bndry[face_hi][mfi_lo].box(),isects);

        for (int i = 0, N = isects.size(); i < N; i++)
        {
            fillBoxId.push_back(mfcd.AddBox(mfid,
                                            isects[i].second,
                                            0,
                                            isects[i].first,
                                            srccomp,
                                            0,
                                            numcomp));

            BL_ASSERT(fillBoxId.back().box() == isects[i].second);
            //
            // Here we'll save the index into the FabSet.
            //
            fillBoxId.back().FabIndex(mfi_lo.index());
            //
            // Here we'll save the direction.
            //
            side.push_back(Orientation::high);
        }
    }

    mfcd.CollectData();

    BL_ASSERT(fillBoxId.size() == side.size());

    FArrayBox fab;

    for (int i = 0, N = fillBoxId.size(); i < N; i++)
    {
        const FillBoxId& fbid_mflx = fillBoxId[i];

        Orientation the_face(dir,Orientation::Side(side[i]));

        BL_ASSERT(the_face == face_lo || the_face == face_hi);

        fab.resize(fbid_mflx.box(), numcomp);

        mfcd.FillFab(mfid, fbid_mflx, fab);

        const int fabindex = fbid_mflx.FabIndex();

        BL_ASSERT(bndry[the_face].DistributionMap()[fabindex] == ParallelDescriptor::MyProc());

        fab.mult(mult);

        if (op == COPY)
        {
            bndry[the_face][fabindex].copy(fab,fab.box(),0,fbid_mflx.box(),destcomp,numcomp);
        }
        else
        {
            bndry[the_face][fabindex].plus(fab,fab.box(),0,destcomp,numcomp);
        }
    }
}

//
// Helper function and data for CrseInit()/CrseInitFinish().
//

static Array<int>                           CIMsgs;
static std::vector<FabArrayBase::FabComTag> CITags;
static std::vector<FArrayBox*>              CIFabs;
static BArena                               CIArena;

static
void
DoIt (Orientation        face,
      int                k,
      FabSet*            bndry,
      const Box&         bx,
      const FArrayBox&   flux,
      int                srccomp,
      int                destcomp,
      int                numcomp,
      Real               mult,
      FluxRegister::FrOp op = FluxRegister::COPY)
{
    const DistributionMapping& dMap = bndry[face].DistributionMap();

    FArrayBox tmp;

    if (ParallelDescriptor::MyProc() == dMap[k])
    {
        //
        // Local data.
        //
        if (op == FluxRegister::COPY) 
        {
            bndry[face][k].copy(flux, bx, srccomp, bx, destcomp, numcomp);
            bndry[face][k].mult(mult, bx, destcomp, numcomp);    
        }
        else
        {
            tmp.resize(bx, numcomp);
            tmp.copy(flux, bx, srccomp, bx, 0, numcomp);
            tmp.mult(mult);
            bndry[face][k].plus(tmp, bx, bx, 0, destcomp, numcomp);
        }
    }
    else
    {
        FabArrayBase::FabComTag tag;

        tag.toProc   = dMap[k];
        tag.fabIndex = k;
        tag.box      = bx;
        tag.face     = face;
        tag.destComp = destcomp;
        tag.nComp    = numcomp;

        FArrayBox* fab = new FArrayBox(bx, numcomp);

        fab->copy(flux, bx, srccomp, bx, 0, numcomp);
        fab->mult(mult, bx, 0, numcomp);

        CITags.push_back(tag);
        CIFabs.push_back(fab);

        if (CIMsgs.size() == 0)
            CIMsgs.resize(ParallelDescriptor::NProcs(), 0);

        CIMsgs[dMap[k]]++;
    }
}

void
FluxRegister::CrseInit (const FArrayBox& flux,
                        const Box&       subbox,
                        int              dir,
                        int              srccomp,
                        int              destcomp,
                        int              numcomp,
                        Real             mult,
                        FrOp             op)
{
    BL_ASSERT(flux.box().contains(subbox));
    BL_ASSERT(srccomp  >= 0 && srccomp+numcomp  <= flux.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= ncomp);

    if (ParallelDescriptor::IOProcessor())
        BoxLib::Warning("\n*** FluxRegister::CrseInit(const FArrayBox&,...) is deprecated; please use CrseInit(MultiFab&,...) instead!!");
    
    const Orientation lo(dir,Orientation::low);

    std::vector< std::pair<int,Box> > isects;

    bndry[lo].boxArray().intersections(subbox,isects);

    for (int i = 0, N = isects.size(); i < N; i++)
    {
        DoIt(lo,isects[i].first,bndry,isects[i].second,flux,srccomp,destcomp,numcomp,mult,op);
    }

    const Orientation hi(dir,Orientation::high);

    bndry[hi].boxArray().intersections(subbox,isects);

    for (int i = 0, N = isects.size(); i < N; i++)
    {
        DoIt(hi,isects[i].first,bndry,isects[i].second,flux,srccomp,destcomp,numcomp,mult,op);
    }
}

void
FluxRegister::CrseInitFinish (FrOp op)
{
    if (ParallelDescriptor::IOProcessor())
        BoxLib::Warning("\n*** FluxRegister::CrseInitFinish() is deprecated; please use CrseInit(MultiFab&,...) instead!!");

    if (ParallelDescriptor::NProcs() == 1) return;

#if BL_USE_MPI
    const int MyProc = ParallelDescriptor::MyProc();
    const int NProcs = ParallelDescriptor::NProcs();

    const bool verbose = false;

    if (verbose)
    {
        long count = 0;

        for (int i = 0, N = CIFabs.size(); i < N; i++)
        {
            count += CIFabs[i]->box().numPts()*CIFabs[i]->nComp()*sizeof(Real);
        }

        const int IOProc = ParallelDescriptor::IOProcessorNumber();

        ParallelDescriptor::ReduceLongMax(count,IOProc);

        if (ParallelDescriptor::IOProcessor())
            std::cout << "FluxRegister::CrseInitFinish(): HWM = " << count << std::endl;
    }

    BL_ASSERT(CITags.size() == CIFabs.size());

    if (CIMsgs.size() == 0)
        CIMsgs.resize(ParallelDescriptor::NProcs(),0);

    BL_ASSERT(CIMsgs[MyProc] == 0);

    Array<int> Rcvs(NProcs,0);
    //
    // Set Rcvs[i] to # of blocks we expect to get from CPU i ...
    //
    BL_MPI_REQUIRE( MPI_Alltoall(CIMsgs.dataPtr(),
                                 1,
                                 ParallelDescriptor::Mpi_typemap<int>::type(),
                                 Rcvs.dataPtr(),
                                 1,
                                 ParallelDescriptor::Mpi_typemap<int>::type(),
                                 ParallelDescriptor::Communicator()) );
    BL_ASSERT(Rcvs[MyProc] == 0);

    int NumRcvs = 0;
    for (int i = 0; i < NProcs; i++)
        NumRcvs += Rcvs[i];
    if (NumRcvs == 0) NumRcvs = 1;
    Array<ParallelDescriptor::CommData> recvdata(NumRcvs);

    int NumSnds = 0;
    for (int i = 0; i < NProcs; i++)
        NumSnds += CIMsgs[i];
    if (NumSnds == 0) NumSnds = 1;
    Array<ParallelDescriptor::CommData> senddata(NumSnds);
    //
    // Make sure we can treat CommData as a stream of integers.
    //
    BL_ASSERT(sizeof(ParallelDescriptor::CommData) == ParallelDescriptor::CommData::DIM*sizeof(int));
    {
        Array<int> sendcnts(NProcs,0), sdispls(NProcs,0);
        Array<int> recvcnts(NProcs,0), rdispls(NProcs,0), offset(NProcs,0);

        for (int i = 0; i < NProcs; i++)
        {
            recvcnts[i] = Rcvs[i]   * ParallelDescriptor::CommData::DIM;
            sendcnts[i] = CIMsgs[i] * ParallelDescriptor::CommData::DIM;

            if (i < NProcs-1)
            {
                rdispls[i+1] = rdispls[i] + recvcnts[i];
                sdispls[i+1] = sdispls[i] + sendcnts[i];
            }
        }

        for (int i = 1; i < NProcs; i++)
            offset[i] = offset[i-1] + CIMsgs[i-1];

        for (int j = 0, N = CITags.size(); j < N; j++)
        {
            ParallelDescriptor::CommData data(CITags[j].face,
                                              CITags[j].fabIndex,
                                              MyProc,
                                              0,
                                              CITags[j].nComp,
                                              CITags[j].destComp,   // Store as srcComp()
                                              0,                    // Not used.
                                              CITags[j].box);

            senddata[offset[CITags[j].toProc]++] = data;
        }

        BL_MPI_REQUIRE( MPI_Alltoallv(senddata.dataPtr(),
                                      sendcnts.dataPtr(),
                                      sdispls.dataPtr(),
                                      ParallelDescriptor::Mpi_typemap<int>::type(),
                                      recvdata.dataPtr(),
                                      recvcnts.dataPtr(),
                                      rdispls.dataPtr(),
                                      ParallelDescriptor::Mpi_typemap<int>::type(),
                                      ParallelDescriptor::Communicator()) );
    }
    Array<int> sendcnts(NProcs,0), sdispls(NProcs,0);
    Array<int> recvcnts(NProcs,0), rdispls(NProcs,0);

    int send_sz = 0, recv_sz = 0, roffset = 0, soffset = 0;

    for (int i = 0; i < NProcs; i++)
    {
        size_t recv_N = 0;
        for (int j = 0; j < Rcvs[i]; j++)
            recv_N += recvdata[roffset+j].box().numPts() * recvdata[roffset+j].nComp();
        recv_sz    += recv_N;
        recvcnts[i] = recv_N;
        roffset    += Rcvs[i];

        size_t send_N = 0;
        for (int j = 0; j < CIMsgs[i]; j++)
            send_N += senddata[soffset+j].box().numPts() * senddata[soffset+j].nComp();
        send_sz    += send_N;
        sendcnts[i] = send_N;
        soffset    += CIMsgs[i];

        if (i < NProcs-1)
        {
            rdispls[i+1] = rdispls[i] + recvcnts[i];
            sdispls[i+1] = sdispls[i] + sendcnts[i];
        }
    }

    BL_ASSERT((send_sz*sizeof(Real)) < std::numeric_limits<size_t>::max());

    Real* sendbuf = static_cast<Real*>(BoxLib::The_Arena()->alloc(send_sz*sizeof(Real)));

    Array<int> offset = sdispls;

    for (int j = 0; j < CITags.size(); j++)
    {
        BL_ASSERT(CITags[j].box == CIFabs[j]->box());
        BL_ASSERT(CITags[j].nComp == CIFabs[j]->nComp());
        const int N = CITags[j].box.numPts() * CITags[j].nComp;
        memcpy(&sendbuf[offset[CITags[j].toProc]], CIFabs[j]->dataPtr(), N * sizeof(Real));
        delete CIFabs[j];
        CIFabs[j] = 0;
        offset[CITags[j].toProc] += N;
    }

    BL_ASSERT((recv_sz*sizeof(Real)) < std::numeric_limits<size_t>::max());

    Real* recvbuf = static_cast<Real*>(BoxLib::The_Arena()->alloc(recv_sz*sizeof(Real)));

    BL_MPI_REQUIRE( MPI_Alltoallv(sendbuf,
                                  sendcnts.dataPtr(),
                                  sdispls.dataPtr(),
                                  ParallelDescriptor::Mpi_typemap<Real>::type(),
                                  recvbuf,
                                  recvcnts.dataPtr(),
                                  rdispls.dataPtr(),
                                  ParallelDescriptor::Mpi_typemap<Real>::type(),
                                  ParallelDescriptor::Communicator()) );

    BoxLib::The_Arena()->free(sendbuf);

    FArrayBox fab;

    roffset = 0;

    for (int i = 0; i < NProcs; i++)
    {
        const Real* dptr = &recvbuf[rdispls[i]];

        for (int j = 0; j < Rcvs[i]; j++)
        {
            const ParallelDescriptor::CommData& cd = recvdata[roffset+j];
            fab.resize(cd.box(),cd.nComp());
            const int N = fab.box().numPts() * fab.nComp();
            memcpy(fab.dataPtr(), dptr, N * sizeof(Real));
            if (op == COPY)
            {
                bndry[cd.face()][cd.fabindex()].copy(fab, fab.box(), 0, fab.box(), cd.srcComp(), cd.nComp());
            }
            else
            {
                bndry[cd.face()][cd.fabindex()].plus(fab, fab.box(), fab.box(), 0, cd.srcComp(), cd.nComp());
            }
            dptr += N;
        }

        roffset += Rcvs[i];
    }

    BoxLib::The_Arena()->free(recvbuf);

    CIFabs.erase(CIFabs.begin(), CIFabs.end());
    CITags.erase(CITags.begin(), CITags.end());

    for (int i = 0; i < NProcs; i++) CIMsgs[i] = 0;
#endif /*BL_USE_MPI*/
}

void
FluxRegister::FineAdd (const MultiFab& mflx,
                       int             dir,
                       int             srccomp,
                       int             destcomp,
                       int             numcomp,
                       Real            mult)
{
    const int N = mflx.IndexMap().size();

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int i = 0; i < N; i++)
    {
        const int k = mflx.IndexMap()[i];
        FineAdd(mflx[k],dir,k,srccomp,destcomp,numcomp,mult);
    }
}

void
FluxRegister::FineAdd (const MultiFab& mflx,
                       const MultiFab& area,
                       int             dir,
                       int             srccomp,
                       int             destcomp,
                       int             numcomp,
                       Real            mult)
{
    const int N = mflx.IndexMap().size();

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int i = 0; i < N; i++)
    {
        const int k = mflx.IndexMap()[i];
        FineAdd(mflx[k],area[k],dir,k,srccomp,destcomp,numcomp,mult);
    }
}

void
FluxRegister::FineAdd (const FArrayBox& flux,
                       int              dir,
                       int              boxno,
                       int              srccomp,
                       int              destcomp,
                       int              numcomp,
                       Real             mult)
{
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= flux.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= ncomp);
#ifndef NDEBUG
    Box cbox = BoxLib::coarsen(flux.box(),ratio);
#endif
    const Box&  flxbox = flux.box();
    const int*  flo    = flxbox.loVect();
    const int*  fhi    = flxbox.hiVect();
    const Real* flxdat = flux.dataPtr(srccomp);

    FArrayBox& loreg = bndry[Orientation(dir,Orientation::low)][boxno];

    BL_ASSERT(cbox.contains(loreg.box()));
    const int* rlo = loreg.box().loVect();
    const int* rhi = loreg.box().hiVect();
    Real* lodat = loreg.dataPtr(destcomp);
    FORT_FRFINEADD(lodat,ARLIM(rlo),ARLIM(rhi),
                   flxdat,ARLIM(flo),ARLIM(fhi),
                   &numcomp,&dir,ratio.getVect(),&mult);

    FArrayBox& hireg = bndry[Orientation(dir,Orientation::high)][boxno];

    BL_ASSERT(cbox.contains(hireg.box()));
    rlo = hireg.box().loVect();
    rhi = hireg.box().hiVect();
    Real* hidat = hireg.dataPtr(destcomp);
    FORT_FRFINEADD(hidat,ARLIM(rlo),ARLIM(rhi),
                   flxdat,ARLIM(flo),ARLIM(fhi),
                   &numcomp,&dir,ratio.getVect(),&mult);
}

void
FluxRegister::FineAdd (const FArrayBox& flux,
                       const FArrayBox& area,
                       int              dir,
                       int              boxno,
                       int              srccomp,
                       int              destcomp,
                       int              numcomp,
                       Real             mult)
{
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= flux.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= ncomp);
#ifndef NDEBUG
    Box cbox = BoxLib::coarsen(flux.box(),ratio);
#endif
    const Real* area_dat = area.dataPtr();
    const int*  alo      = area.loVect();
    const int*  ahi      = area.hiVect();
    const Box&  flxbox   = flux.box();
    const int*  flo      = flxbox.loVect();
    const int*  fhi      = flxbox.hiVect();
    const Real* flxdat   = flux.dataPtr(srccomp);

    FArrayBox& loreg = bndry[Orientation(dir,Orientation::low)][boxno];

    BL_ASSERT(cbox.contains(loreg.box()));
    const int* rlo = loreg.box().loVect();
    const int* rhi = loreg.box().hiVect();
    Real* lodat = loreg.dataPtr(destcomp);
    FORT_FRFAADD(lodat,ARLIM(rlo),ARLIM(rhi),
                 flxdat,ARLIM(flo),ARLIM(fhi),
                 area_dat,ARLIM(alo),ARLIM(ahi),
                 &numcomp,&dir,ratio.getVect(),&mult);

    FArrayBox& hireg = bndry[Orientation(dir,Orientation::high)][boxno];

    BL_ASSERT(cbox.contains(hireg.box()));
    rlo = hireg.box().loVect();
    rhi = hireg.box().hiVect();
    Real* hidat = hireg.dataPtr(destcomp);
    FORT_FRFAADD(hidat,ARLIM(rlo),ARLIM(rhi),
                 flxdat,ARLIM(flo),ARLIM(fhi),
                 area_dat,ARLIM(alo),ARLIM(ahi),
                 &numcomp,&dir,ratio.getVect(),&mult);
}

void
FluxRegister::write (const std::string& name, std::ostream& os) const
{
    if (ParallelDescriptor::IOProcessor())
    {
        os << ratio      << '\n';
        os << fine_level << '\n';
        os << ncomp      << '\n';
    }

    const BndryRegister* br = this;

    br->write(name,os);
}


void
FluxRegister::read (const std::string& name, std::istream& is)
{

    is >> ratio;
    is >> fine_level;
    is >> ncomp;

    BndryRegister* br = this;

    br->read(name,is);
}
