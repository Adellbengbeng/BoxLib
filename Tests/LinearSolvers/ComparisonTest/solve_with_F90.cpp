#include <AMReX_Utility.H>
#include <AMReX_ParmParse.H>
#include <AMReX_LO_BCTYPES.H>
#include <AMReX_MultiFab.H>
#include <AMReX_Geometry.H>
#include <AMReX_MultiFabUtil.H>

#include <FMultiGrid.H>

void solve_with_F90(const Array<MultiFab*>& soln, Real a, Real b, 
		    const Array<MultiFab*>& alph, 
		    const Array<MultiFab*>& beta, 
		    const Array<MultiFab*>& rhs, 
		    const Array<Geometry>& geom, 
		    const Array<BoxArray>& grids,
		    int ibnd)
{
  const Real run_strt = ParallelDescriptor::second();

  int composite_solve = 0;
  Real tolerance_rel, tolerance_abs;
  int maxorder = 3;
  {
    ParmParse pp;
    pp.query("composite_solve", composite_solve);
    
    pp.get("tol_rel", tolerance_rel);
    pp.get("tol_abs", tolerance_abs);
  }
  {
      ParmParse pp("mg");
      pp.query("maxorder", maxorder);
  }

  int nlevel = geom.size();
  
  int mg_bc[2*BL_SPACEDIM];
  
  if (ibnd == LO_NEUMANN) {
    for ( int n = 0; n < BL_SPACEDIM; ++n ) {
      mg_bc[2*n + 0] = MGT_BC_NEU;
      mg_bc[2*n + 1] = MGT_BC_NEU;
    }
  }
  else if (ibnd == LO_DIRICHLET) {
    for ( int n = 0; n < BL_SPACEDIM; ++n ) {
      mg_bc[2*n + 0] = MGT_BC_DIR;
      mg_bc[2*n + 1] = MGT_BC_DIR;
    }
  }
  else { // periodic
    for ( int n = 0; n < BL_SPACEDIM; ++n ) {
      mg_bc[2*n + 0] = 0; // MGT_BC_PER;
      mg_bc[2*n + 1] = 0; // MGT_BC_PER;
    }
  }
   
  Array< Array<std::unique_ptr<MultiFab> > > bcoeffs(nlevel);
  for (int ilev=0; ilev<nlevel; ilev++ ) {

    bcoeffs[ilev].resize(BL_SPACEDIM);

    for (int n = 0; n < BL_SPACEDIM ; n++) 
    {
      BoxArray edge_boxes(grids[ilev]);
      edge_boxes.surroundingNodes(n);
      
      bcoeffs[ilev][n].reset(new MultiFab(edge_boxes,1,0,Fab_allocate));
    }

    BoxLib::average_cellcenter_to_face(BoxLib::GetArrOfPtrs(bcoeffs[ilev]),
				       *beta[ilev], geom[ilev]);
  }

  if (composite_solve) 
  {
      FMultiGrid fmg(geom);
      
      fmg.set_bc(mg_bc, *soln[0]);
      fmg.set_maxorder(maxorder);

      fmg.set_scalars(a, b);
      fmg.set_coefficients(alph, BoxLib::GetArrOfArrOfPtrs(bcoeffs));

      fmg.solve(soln, rhs, tolerance_rel, tolerance_abs); 
  }
  else 
  {
      for (int ilev=0; ilev<nlevel; ilev++ ) {
	  IntVect crse_ratio = (ilev == 0) ? 
	      IntVect::TheZeroVector() : IntVect::TheUnitVector() * 2;
	  FMultiGrid fmg(geom[ilev], ilev, crse_ratio);

	  if (ilev == 0) {
	      fmg.set_bc(mg_bc, *soln[0]);
	  } else {
	      fmg.set_bc(mg_bc, *soln[ilev-1], *soln[ilev]);
	  }
	  fmg.set_maxorder(maxorder);

	  fmg.set_scalars(a, b);
	  fmg.set_coefficients(*alph[ilev], BoxLib::GetArrOfPtrs(bcoeffs[ilev]));
	  
	  fmg.solve(*soln[ilev], *rhs[ilev], tolerance_rel, tolerance_abs);
      }
  }

  Real run_time = ParallelDescriptor::second() - run_strt;

  ParallelDescriptor::ReduceRealMax(run_time, ParallelDescriptor::IOProcessorNumber());
  if (ParallelDescriptor::IOProcessor()) {
    std::cout << "Total BoxLib_F Run time      : " << run_time << std::endl;
  }
}

