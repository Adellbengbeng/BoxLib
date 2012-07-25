#include <winstd.H>

#include <algorithm>
#include <iomanip>

#include <ParmParse.H>
#include <ParallelDescriptor.H>
#include <Utility.H>
#include <LO_BCTYPES.H>
#include <CG_F.H>
#include <CGSolver.H>
#include <MultiGrid.H>

namespace
{
    bool initialized = false;
}
//
// Set default values for these in Initialize()!!!
//
int              CGSolver::def_maxiter;
int              CGSolver::def_verbose;
CGSolver::Solver CGSolver::def_cg_solver;
bool             CGSolver::use_jbb_precond;
bool             CGSolver::use_jacobi_precond;
double           CGSolver::def_unstable_criterion;

void
CGSolver::Initialize ()
{
    if (initialized) return;
    //
    // Set defaults here!!!
    //
    CGSolver::def_maxiter            = 40;
    CGSolver::def_verbose            = 0;
    CGSolver::def_cg_solver          = BiCGStab;
    CGSolver::use_jbb_precond        = 0;
    CGSolver::use_jacobi_precond     = 0;
    CGSolver::def_unstable_criterion = 10;

    ParmParse pp("cg");

    pp.query("v",                  def_verbose);
    pp.query("maxiter",            def_maxiter);
    pp.query("verbose",            def_verbose);
    pp.query("use_jbb_precond",    use_jbb_precond);
    pp.query("use_jacobi_precond", use_jacobi_precond);
    pp.query("unstable_criterion", def_unstable_criterion);

    int ii;
    if (pp.query("cg_solver", ii))
    {
        switch (ii)
        {
        case 0: def_cg_solver = CG;       break;
        case 1: def_cg_solver = BiCGStab; break;
        default:
            BoxLib::Error("CGSolver::Initialize(): bad cg_solver");
        }
    }

    if (ParallelDescriptor::IOProcessor() && (def_verbose > 2))
    {
        std::cout << "CGSolver settings ...\n";
	std::cout << "   def_maxiter            = " << def_maxiter            << '\n';
	std::cout << "   def_unstable_criterion = " << def_unstable_criterion << '\n';
	std::cout << "   def_cg_solver          = " << def_cg_solver          << '\n';
	std::cout << "   use_jbb_precond        = " << use_jbb_precond        << '\n';
	std::cout << "   use_jacobi_precond     = " << use_jacobi_precond     << '\n';
    }

    BoxLib::ExecOnFinalize(CGSolver::Finalize);
    
    initialized = true;
}

void
CGSolver::Finalize ()
{
    initialized = false;
}

CGSolver::CGSolver (LinOp& _Lp,
                    bool   _use_mg_precond,
                    int    _lev)
    :
    Lp(_Lp),
    mg_precond(0),
    lev(_lev),
    use_mg_precond(_use_mg_precond)
{
    Initialize();
    maxiter = def_maxiter;
    verbose = def_verbose;
    cg_solver = def_cg_solver;
    set_mg_precond();
}

void
CGSolver::set_mg_precond ()
{
    delete mg_precond;
    if (use_mg_precond)
    {
        mg_precond = new MultiGrid(Lp);
    }
}

CGSolver::~CGSolver ()
{
    delete mg_precond;
}

static
void
Spacer (std::ostream& os, int lev)
{
    for (int k = 0; k < lev; k++)
    {
        os << "   ";
    }
}

static
Real
norm_inf (const MultiFab& res, bool local = false)
{
    Real restot = 0.0;

    for (MFIter mfi(res); mfi.isValid(); ++mfi) 
    {
        restot = std::max(restot, res[mfi].norm(mfi.validbox(), 0));
    }

    if (!local)
        ParallelDescriptor::ReduceRealMax(restot);

    return restot;
}

int
CGSolver::solve (MultiFab&       sol,
                 const MultiFab& rhs,
                 Real            eps_rel,
                 Real            eps_abs,
                 LinOp::BC_Mode  bc_mode,
		 Solver          solver)
{
    int ret = -1;

    switch (solver)
    {
    case 0:
        ret = solve_cg(sol, rhs, eps_rel, eps_abs, bc_mode);
        break;
    case 1:
        ret = solve_bicgstab(sol, rhs, eps_rel, eps_abs, bc_mode);
        break;
    default:
        BoxLib::Error("CGSolver::solve(): unknown solver");
    }

    return ret;
}

static
void
sxay (MultiFab&       ss,
      const MultiFab& xx,
      Real            a,
      const MultiFab& yy)
{
    const int ncomp = ss.nComp();

    for (MFIter mfi(ss); mfi.isValid(); ++mfi)
    {
        const int k = mfi.index();

        const Box&       ssbx  = ss.box(k);
        FArrayBox&       ssfab = ss[k];
        const FArrayBox& xxfab = xx[k];
        const FArrayBox& yyfab = yy[k];

        FORT_CGSXAY(ssfab.dataPtr(),
                    ARLIM(ssfab.loVect()), ARLIM(ssfab.hiVect()),
                    xxfab.dataPtr(),
                    ARLIM(xxfab.loVect()), ARLIM(xxfab.hiVect()),
                    &a,
                    yyfab.dataPtr(),
                    ARLIM(yyfab.loVect()), ARLIM(yyfab.hiVect()),
                    ssbx.loVect(), ssbx.hiVect(),
                    &ncomp);
    }
}

static
Real
dotxy (const MultiFab& r,
       const MultiFab& z,
       bool            local = false)
{
    const int ncomp = z.nComp();

    Real rho = 0.0;

    for (MFIter mfi(r); mfi.isValid(); ++mfi)
    {
        const int k = mfi.index();
        Real trho;

        const Box&       rbx  = r.box(k);
        const FArrayBox& rfab = r[k];
        const FArrayBox& zfab = z[k];

        FORT_CGXDOTY(&trho,
                     zfab.dataPtr(),
                     ARLIM(zfab.loVect()),ARLIM(zfab.hiVect()),
                     rfab.dataPtr(),
                     ARLIM(rfab.loVect()),ARLIM(rfab.hiVect()),
                     rbx.loVect(),rbx.hiVect(),
                     &ncomp);
        rho += trho;
    }

    if (!local)
        ParallelDescriptor::ReduceRealSum(rho);

    return rho;
}

int
CGSolver::solve_bicgstab (MultiFab&       sol,
		       const MultiFab& rhs,
		       Real            eps_rel,
		       Real            eps_abs,
		       LinOp::BC_Mode  bc_mode)
{
    const int nghost = 1;
    const int ncomp  = 1;

    BL_ASSERT(sol.nComp() == 1);
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab s(sol.boxArray(), ncomp, nghost);
    MultiFab sh(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab rh(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);
    MultiFab ph(sol.boxArray(), ncomp, nghost);
    MultiFab v(sol.boxArray(), ncomp, nghost);
    MultiFab t(sol.boxArray(), ncomp, nghost);

    if (verbose && false)
    {
        std::cout << "eps_rel = "       << eps_rel         << std::endl;
        std::cout << "eps_abs = "       << eps_abs         << std::endl;
        std::cout << "lp.norm = "       << Lp.norm(0, lev) << std::endl;
        std::cout << "sol.norm_inf = " << norm_inf(sol)   << std::endl;
        std::cout << "rhs.norm_inf = " << norm_inf(rhs)   << std::endl;
    }

    sorig.copy(sol);
    Lp.residual(r, rhs, sorig, lev, bc_mode);
    rh.copy(r);
    sol.setVal(0.0);
    const LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real       rnorm    = norm_inf(r);
    const Real rnorm0   = rnorm;
    const Real Lp_norm  = Lp.norm(0, lev);
    const Real rh_norm  = rnorm0;
    Real       sol_norm = 0.0;
  
    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "CGSolver_bicgstab: Initial error (error0) =        " << rnorm0 << '\n';
    }
    int ret = 0, nit = 1;
    Real rho_1 = 0, alpha = 0, omega = 0;

    if ( rnorm == 0.0 || rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
    {
        if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev);
            std::cout << "CGSolver_bicgstab: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_rel*(Lp_norm*sol_norm + rh_norm )" <<  eps_rel*(Lp_norm*sol_norm + rh_norm ) 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }

    for (; nit <= maxiter; ++nit)
    {
        Real rho = dotxy(rh,r);
        if ( rho == 0 ) 
	{
            ret = 1;
            break;
	}
        if ( nit == 1 )
        {
            p.copy(r);
        }
        else
        {
            Real beta = (rho/rho_1)*(alpha/omega);
            sxay(p, p, -omega, v);
            sxay(p, r, beta, p);
        }
        if ( use_mg_precond )
        {
            ph.setVal(0.0);
            mg_precond->solve(ph, p, eps_rel, eps_abs, temp_bc_mode);
        }
        else if ( use_jacobi_precond )
        {
            ph.setVal(0.0);
            Lp.jacobi_smooth(ph, p, lev, temp_bc_mode);
        }
        else 
        {
            ph.copy(p);
        }
        Lp.apply(v, ph, lev, temp_bc_mode);

        if ( Real rhTv = dotxy(rh,v) )
	{
            alpha = rho/rhTv;
	}
        else
	{
            ret = 2;
            break;
	}
        sxay(sol, sol, alpha, ph);
        sxay(s, r, -alpha, v);
        rnorm = norm_inf(s);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_bicgstab: Half Iter "
                          << std::setw(11) << nit
                          << " rel. err. "
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs ) break;
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#endif

        if ( use_mg_precond )
        {
            sh.setVal(0.0);
            mg_precond->solve(sh, s, eps_rel, eps_abs, temp_bc_mode);
        }
        else if ( use_jacobi_precond )
        {
            sh.setVal(0.0);
            Lp.jacobi_smooth(sh, s, lev, temp_bc_mode);
        }
        else
        {
            sh.copy(s);
        }
        Lp.apply(t, sh, lev, temp_bc_mode);
        if ( Real tTt = dotxy(t,t) )
	{
            omega = dotxy(t,s)/tTt;
	}
        else
	{
            ret = 3;
            break;
	}
        sxay(sol, sol, omega, sh);
        sxay(r, s, -omega, t);
        rnorm = norm_inf(r);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_bicgstab: Iteration "
                          << std::setw(11) << nit
                          << " rel. err. "
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs ) break;
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#endif
        if ( omega == 0 )
	{
            ret = 4;
            break;
	}
        rho_1 = rho;
    }

    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0. && rnorm < eps_abs)) && verbose))
	{
            Spacer(std::cout, lev);
            std::cout << "CGSolver_bicgstab: Final: Iteration "
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rh_norm) << '\n';
	}
    }
#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rh_norm ) && rnorm > eps_abs )
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_bicgstab:: failed to converge!");
        ret = 8;
    }
#else
    if ( ret == 0 && rnorm > eps_rel*rnorm0 && rnorm > eps_abs)
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_bicgstab:: failed to converge!");
        ret = 8;
    }
#endif

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rh_norm) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0.0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}

int
CGSolver::solve_cg (MultiFab&       sol,
		    const MultiFab& rhs,
		    Real            eps_rel,
		    Real            eps_abs,
		    LinOp::BC_Mode  bc_mode)
{
    const int nghost = 1;
    const int ncomp  = sol.nComp();

    BL_ASSERT(ncomp == 1 );
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab z(sol.boxArray(), ncomp, nghost);
    MultiFab q(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);

    MultiFab r1(sol.boxArray(), ncomp, nghost);
    MultiFab z1(sol.boxArray(), ncomp, nghost);
    MultiFab r2(sol.boxArray(), ncomp, nghost);
    MultiFab z2(sol.boxArray(), ncomp, nghost);

    sorig.copy(sol);

    Lp.residual(r, rhs, sorig, lev, bc_mode);

    sol.setVal(0);

    const LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real       rnorm    = norm_inf(r);
    const Real rnorm0   = rnorm;
    Real       minrnorm = rnorm;

    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "              CG: Initial error :        " << rnorm0 << '\n';
    }

    const Real Lp_norm = Lp.norm(0, lev);
    const Real rh_norm = rnorm0;
    Real sol_norm      = 0;
    Real rho_1         = 0;
    int  ret           = 0;
    int  nit           = 1;

    if ( rnorm == 0.0 || rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
    {
        if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev);
            std::cout << "       CG: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_rel*(Lp_norm*sol_norm + rh_norm )" <<  eps_rel*(Lp_norm*sol_norm + rh_norm ) 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }

    for (; nit <= maxiter; ++nit)
    {
        if (use_jbb_precond && ParallelDescriptor::NProcs() > 1)
        {
            z.setVal(0);

            jbb_precond(z,r,lev,Lp);
        }
        else
        {
            z.copy(r);
        }

        Real rho = dotxy(z,r);

        if (nit == 1)
        {
            p.copy(z);
        }
        else
        {
            Real beta = rho/rho_1;
            sxay(p, z, beta, p);
        }
        Lp.apply(q, p, lev, temp_bc_mode);

        Real alpha;
        if ( Real pw = dotxy(p,q) )
	{
            alpha = rho/pw;
	}
        else
	{
            ret = 1;
            break;
	}
        
        if (ParallelDescriptor::IOProcessor() && verbose > 2)
        {
            Spacer(std::cout, lev);
            std::cout << "CGSolver_cg:"
                      << " nit " << nit
                      << " rho " << rho
                      << " alpha " << alpha << '\n';
        }
        sxay(sol, sol, alpha, p);
        sxay(  r,   r,-alpha, q);
        rnorm = norm_inf(r);
        sol_norm = norm_inf(sol);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "       CG:       Iteration"
                          << std::setw(4) << nit
                          << " rel. err. "
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm) || rnorm < eps_abs ) break;
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#endif
      
        if ( rnorm > def_unstable_criterion*minrnorm )
	{
            ret = 2;
            break;
	}
        else if ( rnorm < minrnorm )
	{
            minrnorm = rnorm;
	}

        rho_1 = rho;
    }
    
    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0. && rnorm < eps_abs)) && verbose))
	{
            Spacer(std::cout, lev);
            std::cout << "       CG: Final Iteration"
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rh_norm) << '\n';
	}
    }
#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rh_norm) && rnorm > eps_abs )
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_cg:: failed to converge!");
        ret = 8;
    }
#else
    if ( ret == 0 &&  rnorm > eps_rel*rnorm0 && rnorm > eps_abs )
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_cg:: failed to converge!");
        ret = 8;
    }
#endif

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rh_norm) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0.0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}

int
CGSolver::jbb_precond (MultiFab&       sol,
		       const MultiFab& rhs,
                       int             lev,
		       LinOp&          Lp)
{
    //
    // This is a local routine.  No parallel is allowed to happen here.
    //
    int                  lev_loc = lev;
    const Real           eps_rel = 1.e-2;
    const Real           eps_abs = 1.e-16;
    const int            nghost  = 1;
    const int            ncomp   = sol.nComp();
    const bool           local   = true;
    const LinOp::BC_Mode bc_mode = LinOp::Homogeneous_BC;

    BL_ASSERT(ncomp == 1 );
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev_loc));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev_loc));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);

    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab z(sol.boxArray(), ncomp, nghost);
    MultiFab q(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);

    sorig.copy(sol);

    Lp.residual(r, rhs, sorig, lev_loc, LinOp::Homogeneous_BC, local);

    sol.setVal(0);

    Real       rnorm    = norm_inf(r,local);
    const Real rnorm0   = rnorm;
    Real       minrnorm = rnorm;

    if (verbose > 2 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev_loc);
        std::cout << "     jbb_precond: Initial error :        " << rnorm0 << '\n';
    }

    const Real Lp_norm = Lp.norm(0, lev_loc, local);
    const Real rh_norm = rnorm0;
    Real sol_norm = 0;
    int  ret      = 0;			// will return this value if all goes well
    Real rho_1    = 0;
    int  nit      = 1;
    if ( rnorm == 0.0 || rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
    {
        if (verbose > 2 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_rel*(Lp_norm*sol_norm + rh_norm )" <<  eps_rel*(Lp_norm*sol_norm + rh_norm ) 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }

    for (; nit <= maxiter; ++nit)
    {
        z.copy(r);

        Real rho = dotxy(z,r,local);
        if (nit == 1)
        {
            p.copy(z);
        }
        else
        {
            Real beta = rho/rho_1;
            sxay(p, z, beta, p);
        }

        Lp.apply(q, p, lev_loc, bc_mode, local);

        Real alpha;
        if ( Real pw = dotxy(p,q,local) )
	{
            alpha = rho/pw;
	}
        else
	{
            ret = 1;
            break;
	}
        
        if ( ParallelDescriptor::IOProcessor() && verbose > 3 )
        {
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond:" << " nit " << nit
                      << " rho " << rho << " alpha " << alpha << '\n';
        }
        sxay(sol, sol, alpha, p);
        sxay(  r,   r,-alpha, q);
        rnorm    = norm_inf(r,   local);
        sol_norm = norm_inf(sol, local);

        if ( ParallelDescriptor::IOProcessor() )
        {
            if ( verbose > 3 ||
                 (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                   (eps_abs > 0. && rnorm < eps_abs)) && verbose > 3) )
            {
                Spacer(std::cout, lev_loc);
                std::cout << "jbb_precond:       Iteration"
                          << std::setw(4) << nit
                          << " rel. err. "
                          << rnorm/(rh_norm) << '\n';
            }
        }

        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm) || rnorm < eps_abs )
	{
            break;
	}
      
        if ( rnorm > def_unstable_criterion*minrnorm )
	{
            ret = 2;
            break;
	}
        else if ( rnorm < minrnorm )
	{
            minrnorm = rnorm;
	}

        rho_1 = rho;
    }
    
    if ( ParallelDescriptor::IOProcessor() )
    {
        if ( verbose > 2 ||
             (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
               (eps_abs > 0. && rnorm < eps_abs)) && (verbose > 2)) )
	{
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond: Final Iteration"
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rh_norm) << '\n';
	}
    }
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rh_norm) && rnorm > eps_abs )
    {
        if ( ParallelDescriptor::IOProcessor() )
	{
            BoxLib::Warning("jbb_precond:: failed to converge!");
	}
        ret = 8;
    }

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rh_norm) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}
