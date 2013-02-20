subroutine t_cc_ml_multigrid(mla, mgt, domain_bc, bottom_solver, do_diagnostics, eps, stencil_order, fabio)
  use BoxLib
  use cc_stencil_module
  use cc_stencil_fill_module
  use ml_norm_module
  use mg_module
  use list_box_module
  use ml_boxarray_module
  use ml_layout_module
  use itsol_module
  use bl_mem_stat_module
  use bl_timer_module
  use box_util_module
  use bl_IO_module
  use fabio_module
  use ml_restriction_module
  use ml_prolongation_module
  use ml_cc_module

  use bndry_reg_module

  implicit none

  type(ml_layout), intent(inout) :: mla
  integer        , intent(in   ) :: domain_bc(:,:)
  integer        , intent(in   ) :: bottom_solver 
  integer        , intent(in   ) :: do_diagnostics 
  real(dp_t)     , intent(in   ) :: eps
  type(mg_tower) , intent(inout) :: mgt(:)
  integer        , intent(in   ) :: stencil_order
  logical        , intent(in   ) :: fabio

  type(box      )                :: pd

  type(multifab) :: cell_coeffs
  type(multifab) :: edge_coeffs(mla%dim)

  type( multifab), allocatable   :: full_soln(:)
  type( multifab), allocatable   ::        rh(:)

  type(layout)                   :: la
  real(dp_t)     , allocatable   :: xa(:), xb(:), pxa(:), pxb(:)

  integer        , allocatable   :: ref_ratio(:,:)
  integer                        :: i, d, n, dm, nlevs

  real(dp_t)                     :: snrm(2)

  real(dp_t)                     :: mac_beta

  dm = mla%dim

  nlevs = mla%nlevel

  allocate(full_soln(nlevs),rh(nlevs))
  allocate(xa(dm), xb(dm), pxa(dm), pxb(dm))

  allocate(ref_ratio(nlevs-1,dm))
  do n = 1,nlevs-1
    ref_ratio(n,:) = mla%mba%rr(n,:)
  end do

  ! NOTE THIS CHANGE: we now have stencil values which reach outside the
  !  grid in the case of Dirichlet bc's and skewed stencils

  do n = nlevs, 1, -1

     call multifab_build(full_soln(n), mla%la(n), 1, 1)
     call setval(full_soln(n), val = ZERO, all=.true.)

     call multifab_build( rh(n), mla%la(n), 1, 0)
     call setval(rh(n), val = ZERO, all=.true.)
     if (n == nlevs) call mf_init_2(rh(n))
  end do

  !! Fill coefficient arrays

  mac_beta = 1.0_dp_t

  do n = nlevs, 1, -1

     la = mla%la(n)

     call multifab_build(cell_coeffs, la, nc=1, ng=0)
     call setval(cell_coeffs,0.d0,all=.true.)

     do d = 1,dm
        call multifab_build_edge(edge_coeffs(d), la, nc=1, ng=0, dir=d)
        call setval(edge_coeffs(d),mac_beta,all=.true.)
     end do

     pxa = ZERO
     pxb = ZERO
     if (n > 1) then
        xa = HALF*ref_ratio(n-1,:)*mgt(n)%dh(:,mgt(n)%nlevels)
        xb = HALF*ref_ratio(n-1,:)*mgt(n)%dh(:,mgt(n)%nlevels)
     else
        xa = ZERO
        xb = ZERO
     end if

     pd = mla%mba%pd(n)

     call stencil_fill_cc(mgt(n)%ss(mgt(n)%nlevels), cell_coeffs, edge_coeffs, mgt(n)%dh(:,mgt(n)%nlevels), &
                          mgt(n)%mm(mgt(n)%nlevels), xa, xb, pxa, pxb, stencil_order, &
                          domain_bc)

     call destroy(cell_coeffs)

    do d = 1,dm
       call destroy(edge_coeffs(d))
    end do

  end do

  if ( fabio ) then
    call fabio_ml_write(rh, ref_ratio(:,1), "rh-init_cc")
  end if

  snrm(2) = ml_norm_inf(rh,mla%mask)
  if ( parallel_IOProcessor() ) then
     print *, 'RHS MAX NORM ', snrm(2)
  end if

! ****************************************************************************

  call ml_cc(mla, mgt, rh, full_soln, mla%mask, ref_ratio, do_diagnostics, eps)

! ****************************************************************************

  if ( fabio ) then
     call fabio_ml_write(full_soln, ref_ratio(:,1), "soln_cc")
  end if

  snrm(1) = ml_norm_l2(full_soln,ref_ratio,mla%mask)
  snrm(2) = ml_norm_inf(full_soln,mla%mask)
  if ( parallel_IOProcessor() ) then
     print *, 'SOLUTION MAX NORM ', snrm(2)
     print *, 'SOLUTION L2 NORM ', snrm(1)
  end if

  if ( parallel_IOProcessor() ) print *, 'MEMORY STATS'
  call print(multifab_mem_stats(),  " multifab before")
  call print(imultifab_mem_stats(), "imultifab before")
  call print(fab_mem_stats(),       "      fab before")
  call print(ifab_mem_stats(),      "     ifab before")
  call print(boxarray_mem_stats(),  " boxarray before")
  call print(boxassoc_mem_stats(),  " boxassoc before")
  call print(layout_mem_stats(),    "   layout before")

  do n = 1,nlevs
     call multifab_destroy(rh(n))
     call multifab_destroy(full_soln(n))
  end do

contains

  subroutine mf_init(mf)

    type(multifab), intent(inout) :: mf
    type(box) bx
    bx = pd
    bx%lo(1:bx%dim) = (bx%hi(1:bx%dim) + bx%lo(1:bx%dim))/2
    bx%hi(1:bx%dim) = bx%lo(1:bx%dim)
    call setval(mf, ONE, bx)

  end subroutine mf_init

  subroutine mf_init_2(mf)
    type(multifab), intent(inout) :: mf
    integer i
    type(box) bx
    do i = 1, nfabs(mf)

       bx = get_box(mf,i)
       bx%lo(1:bx%dim) = (bx%hi(1:bx%dim) + bx%lo(1:bx%dim))/2
       bx%hi(1:bx%dim) = bx%lo(1:bx%dim)
       call setval(mf%fbs(i), 1.0_dp_t, bx)
!       print *,'SETTING RHS TO ONE ON ',bx%lo(1:bx%dim)

!      Single point of non-zero RHS: use this to make system solvable
       bx = get_box(mf,i)
       bx%lo(1       ) = (bx%hi(1       ) + bx%lo(1       ))/2 + 1
       bx%lo(2:bx%dim) = (bx%hi(2:bx%dim) + bx%lo(2:bx%dim))/2
       bx%hi(1:bx%dim) = bx%lo(1:bx%dim)
       call setval(mf%fbs(i), -1.0_dp_t, bx)
!       print *,'SETTING RHS TO -ONE ON ',bx%lo(1:bx%dim)

!      1-d Strip: Variation in x-direction
!      bx%lo(1) = (bx%hi(1) + bx%lo(1))/2
!      bx%hi(1) = bx%lo(1)+1

!      1-d Strip: Variation in y-direction
!      bx%lo(2) = (bx%hi(2) + bx%lo(2))/2
!      bx%hi(2) = bx%lo(2)+1

!      1-d Strip: Variation in z-direction
!      bx%lo(3) = (bx%hi(3) + bx%lo(3))/2
!      bx%hi(3) = bx%lo(3)+1

    end do
  end subroutine mf_init_2

  subroutine mf_init_1(mf)
    type(multifab), intent(inout) :: mf
    integer i
    type(box) bx
    type(box) rhs_box, rhs_intersect_box

    rhs_box%dim = mf%dim
    rhs_box%lo(1:rhs_box%dim) = 7
    rhs_box%hi(1:rhs_box%dim) = 8

    do i = 1, nfabs(mf)
       bx = get_ibox(mf,i)
       rhs_intersect_box = box_intersection(bx,rhs_box)
       if (.not. empty(rhs_intersect_box)) then
         bx%lo(1:bx%dim) = lwb(rhs_intersect_box)
         bx%hi(1:bx%dim) = upb(rhs_intersect_box)
!         print *,'SETTING RHS IN BOX ',i,' : ', bx%lo(1:bx%dim),bx%hi(1:bx%dim)
         call setval(mf%fbs(i), ONE, bx)
       end if
    end do

  end subroutine mf_init_1

end subroutine t_cc_ml_multigrid
