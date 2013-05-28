module ml_restriction_module

  use bl_types
  use multifab_module

  implicit none

  private

  public :: ml_restriction, ml_cc_restriction, ml_cc_restriction_c
  public :: ml_edge_restriction, ml_edge_restriction_c, periodic_add_copy 

contains

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_cc_restriction_c(crse, cc, fine, cf, ir, nc)
    use cc_restriction_module
    type(multifab), intent(inout) :: fine
    type(multifab), intent(inout) :: crse
    integer, intent(in)           :: cc, cf, ir(:)
    integer, intent(in), optional :: nc

    integer             :: i, n, lnc, dm, lo(get_dim(fine)), hi(get_dim(fine)), lof(get_dim(fine))
    real(dp_t), pointer :: fp(:,:,:,:), cp(:,:,:,:)
    type(layout)        :: lacfine,laf
    type(multifab)      :: cfine

    lnc = 1; if ( present(nc) ) lnc = nc

    laf = get_layout(fine)

    call layout_build_coarse(lacfine, laf, ir)

    call build(cfine, lacfine, nc = lnc, ng = 0)

    dm = get_dim(cfine)

    !$OMP PARALLEL DO PRIVATE(i,n,lof,lo,hi,fp,cp)
    do i = 1, nfabs(fine)
       lof = lwb(get_pbox(fine, i))
       lo  = lwb(get_ibox(cfine,i))
       hi  = upb(get_ibox(cfine,i))
       do n = 1, lnc
          fp => dataptr(fine,  i, n+cf-1, 1)
          cp => dataptr(cfine, i, n,      1)
          select case (dm)
          case (1)
             call cc_restriction_1d(cp(:,1,1,1), lo, fp(:,1,1,1), lof, lo, hi, ir)
          case (2)
             call cc_restriction_2d(cp(:,:,1,1), lo, fp(:,:,1,1), lof, lo, hi, ir)
          case (3)
             call cc_restriction_3d(cp(:,:,:,1), lo, fp(:,:,:,1), lof, lo, hi, ir)
          end select
       end do
    end do
    !$OMP END PARALLEL DO

    call copy(crse, cc, cfine, 1, lnc)

    call destroy(cfine)

    call multifab_fill_boundary_c(crse,cc,lnc)

  end subroutine ml_cc_restriction_c

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_cc_restriction(crse, fine, ir)
    type(multifab), intent(inout) :: fine
    type(multifab), intent(inout) :: crse
    integer,        intent(in)    :: ir(:)
    if ( ncomp(crse) .ne. ncomp(fine) ) then
       call bl_error('ml_cc_restriction: crse & fine must have same # of components')
    end if
    call ml_cc_restriction_c(crse, 1, fine, 1, ir, ncomp(crse))
  end subroutine ml_cc_restriction

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_edge_restriction_c(crse, cc, fine, cf, ir, face, nc)
    use edge_restriction_module
    type(multifab), intent(inout) :: fine
    type(multifab), intent(inout) :: crse
    integer,        intent(in)    :: cc, cf, ir(:)
    integer,        intent(in)    :: face
    integer, intent(in), optional :: nc

    integer             :: i, n, lnc, dm, len
    integer             :: lo(get_dim(fine)), hi(get_dim(fine)), loc(get_dim(fine)), lof(get_dim(fine))
    real(dp_t), pointer :: fp(:,:,:,:), cp(:,:,:,:)
    type(box)           :: bx,fine_domain,crse_domain
    type(layout)        :: lacfine, lacfine_lo, lacfine_hi 
    type(layout)        :: la_lo,la_hi,laf
    type(multifab)      :: cfine, fine_lo, fine_hi
    type(list_box)      :: bxs_lo,bxs_hi
    type(boxarray)      :: ba_lo,ba_hi
    logical             :: nodal(get_dim(fine)), pmask(get_dim(fine))

    dm = get_dim(crse)

    lnc = 1; if ( present(nc) ) lnc = nc

    laf = get_layout(fine)

    call layout_build_coarse(lacfine, laf, ir)

    call multifab_build(cfine, lacfine, nc = ncomp(crse), ng = 0, nodal = nodal_flags(crse))

    !$OMP PARALLEL DO PRIVATE(i,n,lo,hi,loc,lof,fp,cp)
    do i = 1, nfabs(fine)
       lo  = lwb(get_ibox(cfine,i))
       hi  = upb(get_ibox(cfine,i))
       loc = lwb(get_pbox(cfine,i))
       lof = lwb(get_pbox(fine, i))
       do n = 1, lnc
          fp  => dataptr(fine,  i, n+cf-1, 1)
          cp  => dataptr(cfine, i, n,      1)
          select case (dm)
          case (1)
             call edge_restriction_1d(cp(:,1,1,1), loc, fp(:,1,1,1), lof, lo, hi, ir)
          case (2)
             call edge_restriction_2d(cp(:,:,1,1), loc, fp(:,:,1,1), lof, lo, hi, ir, face)
          case (3)
             call edge_restriction_3d(cp(:,:,:,1), loc, fp(:,:,:,1), lof, lo, hi, ir, face)
          end select
       enddo
    end do
    !$OMP END PARALLEL DO

    call copy(crse, cc, cfine, 1, lnc)

    call destroy(cfine)
    !
    ! Now do periodic fix-up if necessary.
    !
    pmask = get_pmask(get_layout(crse))

    if (pmask(face)) then

       fine_domain = get_pd(get_layout(fine))
       crse_domain = get_pd(get_layout(crse))
       nodal(:)    = .false.
       nodal(face) = .true.
       len         = box_extent_d(fine_domain,face)
       !
       ! First copy from lo edges to hi edges.
       !
       do i = 1, nboxes(fine%la)
          bx = get_box(fine%la,i)
          if (bx%lo(face) == fine_domain%lo(face)) then
             bx = shift(bx, len, face)
             bx = intersection(bx,grow(fine_domain,1,face,+1))
             call push_back(bxs_lo,bx)
          end if
       end do

       if (.not. empty(bxs_lo)) then

          call build(ba_lo,bxs_lo,sort=.false.)
          call destroy(bxs_lo)
          call build(la_lo,ba_lo,fine_domain,pmask)
          call destroy(ba_lo)
          call multifab_build(fine_lo, la_lo, nc = ncomp(fine), ng = 0, nodal = nodal)
   
          call multifab_copy_on_shift(fine_lo, 1, fine, cf, lnc, len, face)

          call layout_build_coarse(lacfine_lo, la_lo, ir)
          call multifab_build(cfine, lacfine_lo, nc = ncomp(crse), ng = 0, nodal = nodal_flags(crse))

          !$OMP PARALLEL DO PRIVATE(i,n,lo,hi,loc,lof,fp,cp)
          do i = 1, nfabs(fine_lo)
             lo  = lwb(get_ibox(cfine,i))
             hi  = upb(get_ibox(cfine,i))
             hi(face) = lo(face)
             loc = lwb(get_pbox(cfine,i))
             lof = lwb(get_pbox(fine_lo, i))
             do n = 1, lnc
                fp  => dataptr(fine_lo, i, n, 1)
                cp  => dataptr(cfine     , i, n, 1)
                select case (dm)
                case (1)
                   call edge_restriction_1d(cp(:,1,1,1), loc, fp(:,1,1,1), lof, lo, hi, ir)
                case (2)
                   call edge_restriction_2d(cp(:,:,1,1), loc, fp(:,:,1,1), lof, lo, hi, ir, face)
                case (3)
                   call edge_restriction_3d(cp(:,:,:,1), loc, fp(:,:,:,1), lof, lo, hi, ir, face)
                end select
             enddo
          end do
          !$OMP END PARALLEL DO
   
          call copy(crse, cc, cfine, 1, lnc)

          call destroy(cfine)
          call destroy(fine_lo)
          call destroy(la_lo)
       
       end if
       !
       ! Next copy from hi edges to lo edges.
       !
       do i = 1, nboxes(fine%la)
          bx = get_box(fine%la,i)
          if (bx%hi(face) == fine_domain%hi(face)) then
             bx = shift(bx, -len, face)
             bx = intersection(bx,grow(fine_domain,1,face,-1))
             call push_back(bxs_hi,bx)
          end if
       end do

       if (.not. empty(bxs_hi)) then

          call build(ba_hi,bxs_hi,sort=.false.)
          call destroy(bxs_hi)
          call build(la_hi,ba_hi,fine_domain,pmask)
          call destroy(ba_hi)
          call multifab_build(fine_hi, la_hi, nc = ncomp(fine), ng = 0, nodal = nodal)
   
          call multifab_copy_on_shift(fine_hi, 1, fine, cf, lnc, -len, face)

          call layout_build_coarse(lacfine_hi, la_hi, ir)
          call multifab_build(cfine, lacfine_hi, nc = ncomp(crse), ng = 0, nodal = nodal_flags(crse))

          !$OMP PARALLEL DO PRIVATE(i,n,lo,hi,loc,lof,fp,cp)
          do i = 1, nfabs(fine_hi)
             lo  = lwb(get_ibox(cfine,i))
             hi  = upb(get_ibox(cfine,i))
             lo(face) = hi(face)
             loc = lwb(get_pbox(cfine,i))
             lof = lwb(get_pbox(fine_hi, i))
             do n = 1, lnc
                fp  => dataptr(fine_hi, i, n, 1)
                cp  => dataptr(cfine     , i, n, 1)
                select case (dm)
                case (1)
                   call edge_restriction_1d(cp(:,1,1,1), loc, fp(:,1,1,1), lof, lo, hi, ir)
                case (2)
                   call edge_restriction_2d(cp(:,:,1,1), loc, fp(:,:,1,1), lof, lo, hi, ir, face)
                case (3)
                   call edge_restriction_3d(cp(:,:,:,1), loc, fp(:,:,:,1), lof, lo, hi, ir, face)
                end select
             enddo
          end do
          !$OMP END PARALLEL DO

          call copy(crse, cc, cfine, 1, lnc)

          call destroy(cfine)
          call destroy(fine_hi)
          call destroy(la_hi)

       end if ! .not. empty

    end if ! pmask(face)

  end subroutine ml_edge_restriction_c

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_edge_restriction(crse, fine, ir, face)
    type(multifab), intent(inout) :: fine
    type(multifab), intent(inout) :: crse
    integer,        intent(in)    :: ir(:)
    integer,        intent(in)    :: face

    if ( ncomp(crse) .ne. ncomp(fine) ) then
       call bl_error('ml_edge_restriction: crse & fine must have same # of components')
    end if
    call ml_edge_restriction_c(crse, 1, fine, 1, ir, face, ncomp(crse))

  end subroutine ml_edge_restriction

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_restrict_copy_sum(out, in)
     use bl_types
     real(dp_t), intent(inout) :: out(:,:,:,:)
     real(dp_t), intent(in   ) ::  in(:,:,:,:)
     integer                   :: i, j, k, n
     !
     ! out = out + in 
     !
     do n = 1, size(out,4)
        do k = 1, size(out,3)
           do j = 1, size(out,2)
              do i = 1, size(out,1)
                 out(i,j,k,n) = out(i,j,k,n) + in(i,j,k,n)
              end do
           end do
        end do
     end do
  end subroutine ml_restrict_copy_sum

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_nodal_restriction(crse, fine, mm_fine, mm_crse, ir, inject, zero_only)
    use nodal_restriction_module
    type(multifab),  intent(inout)        :: crse
    type(multifab),  intent(inout)        :: fine
    type(imultifab), intent(in   )        :: mm_fine
    type(imultifab), intent(in   )        :: mm_crse
    integer,         intent(in)           :: ir(:)
    logical,         intent(in), optional :: inject
    logical,         intent(in), optional :: zero_only

    integer             :: i, n, rmode, dm
    integer             :: lo (get_dim(fine)), hi (get_dim(fine)), loc(get_dim(fine)), lof(get_dim(fine))
    integer             :: lom_fine(get_dim(fine)), lom_crse(get_dim(fine))
    logical             :: linject, lzero_only
    real(dp_t), pointer :: fp(:,:,:,:), cp(:,:,:,:)
    integer,    pointer :: mp_fine(:,:,:,:), mp_crse(:,:,:,:)
    type(layout)        :: lacfine, laf
    type(multifab)      :: cfine
    type(imultifab)     :: mm_cfine

    type(bl_prof_timer), save :: bpt

    if ( ncomp(crse) .ne. ncomp(fine) ) then
       call bl_error('ml_nodal_restriction: crse & fine must have same # of components')
    end if

    call build(bpt, "ml_nodal_restriction")

    linject    = .false. ; if ( present(inject   ) ) linject    = inject
    lzero_only = .false. ; if ( present(zero_only) ) lzero_only = zero_only

    laf = get_layout(fine)

    call layout_build_coarse(lacfine, laf, ir)
    call multifab_build(cfine, lacfine, nc = ncomp(crse), ng = 0, nodal = nodal_flags(crse))
    call copy(cfine, crse)

    dm = get_dim(fine)

    if ( .not. linject ) then

       !$OMP PARALLEL DO PRIVATE(i,n,lo,hi,loc,lom_fine,cp,mp_fine)
       do i = 1, nfabs(fine)
          lo       = lwb(get_ibox(cfine,   i))
          hi       = upb(get_ibox(cfine,   i))
          loc      = lwb(get_pbox(cfine,   i))
          lom_fine = lwb(get_pbox(mm_fine, i))
          do n = 1, ncomp(fine)
             cp      => dataptr(cfine,   i, n, 1)
             mp_fine => dataptr(mm_fine, i, n, 1)
             select case (dm)
             case (1)
                call nodal_zero_1d(cp(:,1,1,1), loc, mp_fine(:,1,1,1), lom_fine, lo, hi, ir)
             case (2)
                call nodal_zero_2d(cp(:,:,1,1), loc, mp_fine(:,:,1,1), lom_fine, lo, hi, ir)
             case (3)
                call nodal_zero_3d(cp(:,:,:,1), loc, mp_fine(:,:,:,1), lom_fine, lo, hi, ir)
             end select
          end do
       end do
       !$OMP END PARALLEL DO

       call copy(crse, cfine)
       call setval(cfine, 0.0_dp_t)
    end if

    if ( .not. lzero_only ) then

       rmode = 0
       call imultifab_build(mm_cfine, lacfine, nc = ncomp(mm_crse), ng = 0, nodal = nodal_flags(mm_crse))
       call copy(mm_cfine, mm_crse)

       !$OMP PARALLEL DO PRIVATE(i,n,lo,hi,lof,loc,lom_crse,lom_fine,cp,fp,mp_crse,mp_fine)
       do i = 1, nfabs(fine)
          lo       = lwb(get_ibox(cfine,   i))
          hi       = upb(get_ibox(cfine,   i))
          lof      = lwb(get_pbox(fine,    i))
          loc      = lwb(get_pbox(cfine,   i))
          lom_crse = lwb(get_pbox(mm_cfine,i))
          lom_fine = lwb(get_pbox(mm_fine, i))
          do n = 1, ncomp(fine)
             cp      => dataptr(cfine,   i, n, 1)
             fp      => dataptr(fine,    i, n, 1)
             mp_crse => dataptr(mm_cfine,i, n, 1)
             mp_fine => dataptr(mm_fine, i, n, 1)
             select case (dm)
             case (1)
                call nodal_restriction_1d(cp(:,1,1,1), loc, fp(:,1,1,1), lof, &
                     mp_fine(:,1,1,1), lom_fine, &
                     mp_crse(:,1,1,1), lom_crse, lo, hi, ir, linject, rmode)
             case (2)
                call nodal_restriction_2d(cp(:,:,1,1), loc, fp(:,:,1,1), lof, &
                     mp_fine(:,:,1,1), lom_fine, &
                     mp_crse(:,:,1,1), lom_crse, lo, hi, ir, linject, rmode)
             case (3)
                call nodal_restriction_3d(cp(:,:,:,1), loc, fp(:,:,:,1), lof, &
                     mp_fine(:,:,:,1), lom_fine, &
                     mp_crse(:,:,:,1), lom_crse, lo, hi, ir, linject, rmode)
             end select
          end do
       end do
       !$OMP END PARALLEL DO

       call destroy(mm_cfine)

       if ( linject ) then
          call multifab_copy(crse, cfine)
       else
          call multifab_copy(crse, cfine, filter = ml_restrict_copy_sum)
          call periodic_add_copy(crse,cfine,synced=.false.)
       end if
    end if

    call destroy(cfine)

    call destroy(bpt)

  end subroutine ml_nodal_restriction

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine periodic_add_copy(dst,src,synced)

    use bl_prof_module

    type(multifab), intent(inout) :: dst
    type(multifab), intent(in   ) :: src
    logical,        intent(in   ) :: synced
    !
    ! if ( synced )
    !
    !   This version assumes that src IS synced up on each edge to start with  - for example,
    !   if a node in grid A on the lo-x side has value a, and the equivalent node in grid B 
    !   but on the hi-x side has value b, and the equivalent node in grid C on the hi-x side
    !   has value b also, then the final value of the nodes in A,B and C will be (a+b)
    !
    ! else
    !
    !   This version assumes that src is NOT synced up on each edge to start with  - for example,
    !             if a node in grid A on the lo-x side has value a and 
    !   the equivalent node in grid B on the hi-x side has value b and
    !   the equivalent node in grid C on the hi-x side has value c ,
    !   then the final value of the node in each grid A, B and C will be (a+b+c)
    !
    type(multifab)      :: temp_dst
    type(box)           :: domain,bxi,bxj,bx_to,bx_from
    type(box)           :: domain_edge_src, domain_edge_dst
    type(layout)        :: dstla, srcla
    type(boxarray)      :: ba
    real(dp_t), pointer :: ap(:,:,:,:), bp(:,:,:,:)
    integer             :: i,j,ii,jj,idir,jdir,kdir,proc,dm,nc
    logical             :: nodal(get_dim(dst))
    integer             :: shift_vector(3),dims(3),dlen(3)
    logical             :: pmask(get_dim(dst))

    integer,    parameter :: TAG  = 1111
    real(dp_t), parameter :: ZERO = 0.0_dp_t
    real(dp_t), parameter :: ONE  = 1.0_dp_t

    type(box_intersector), pointer :: bisrc(:), bidst(:)

    real(kind=dp_t), dimension(:,:,:,:), allocatable :: pt

    type(bl_prof_timer), save :: bpt

    if ( ncomp(dst) .ne. ncomp(src) ) then
       call bl_error('periodic_add_copy: src & dst must have same # of components')
    end if

    if ( .not. nodal_q(dst) ) call bl_error('periodic_add_copy(): dst NOT nodal')
    if ( .not. nodal_q(src) ) call bl_error('periodic_add_copy(): src NOT nodal')

    pmask = get_pmask(get_layout(dst))

    if ( all(pmask .eqv. .false.) ) return

    nc         = ncomp(dst)
    dm         = get_dim(dst)
    dims       = 1
    nodal      = .true.
    domain     = box_nodalize(get_pd(get_layout(dst)),nodal)
    dlen(1:dm) = extent(domain)

    call build(bpt, "periodic_add_copy")

    if ( synced ) call multifab_build(temp_dst,get_layout(dst),nc,0,nodal)
    !
    ! Need to build temporary layouts with nodal boxarrays for the intersection tests below.
    !
    call copy(ba, get_boxarray(get_layout(dst)))
    call boxarray_nodalize(ba, nodal)
    call build(dstla, ba, boxarray_bbox(ba), mapping = LA_LOCAL)  ! LA_LOCAL ==> bypass processor distribution calculation.
    call destroy(ba)
    call copy(ba, get_boxarray(get_layout(src)))
    call boxarray_nodalize(ba, nodal)
    call build(srcla, ba, boxarray_bbox(ba), mapping = LA_LOCAL)  ! LA_LOCAL ==> bypass processor distribution calculation.
    call destroy(ba)

    do kdir = -1,1

       if ( dm < 3  .and. kdir /= 0                         ) cycle
       if ( dm == 3 .and. (.not. pmask(dm)) .and. kdir /= 0 ) cycle

       if ( dm == 3 ) shift_vector(3) = kdir * (dlen(dm) - 1)

       do jdir = -1,1

          if ( .not. pmask(2) .and. jdir /= 0 ) cycle

          shift_vector(2) = jdir * (dlen(2) - 1)

          do idir = -1,1

             if ( .not. pmask(1) .and. idir /= 0                            ) cycle
             if ( dm == 2 .and. (idir == 0 .and. jdir == 0)                 ) cycle
             if ( dm == 3 .and. (idir == 0 .and. jdir == 0 .and. kdir == 0) ) cycle

             shift_vector(1) = idir * (dlen(1) - 1)

             domain_edge_src = intersection(domain,shift(domain, shift_vector))
             domain_edge_dst = shift(domain_edge_src,-shift_vector)

             if ( synced ) call setval(temp_dst,ZERO)
             !
             ! Add values from domain_edge_src side to domain_edge_dst side
             !
             bidst => layout_get_box_intersector(dstla, domain_edge_dst)

             do jj = 1, size(bidst)
                j     =  bidst(jj)%i
                bxj   =  bidst(jj)%bx
                bisrc => layout_get_box_intersector(srcla, domain_edge_src)
                do ii = 1, size(bisrc)
                   i = bisrc(ii)%i
                   if ( remote(dst%la,j) .and. remote(src%la,i) ) cycle
                   bxi     = shift(bisrc(ii)%bx,-shift_vector)
                   bx_from = intersection(bxi,bxj)
                   if ( empty(bx_from) ) cycle
                   bx_to   = bx_from
                   bx_from = shift(bx_from,shift_vector)
                   if ( local(dst%la,j) .and. local(src%la,i) ) then
                      if ( synced ) then
                         ap => dataptr(temp_dst,local_index(temp_dst,j),bx_to)
                         bp => dataptr(src,     local_index(src,     i), bx_from)
                         call cpy_d(ap,bp) ! ap = bp
                      else
                         ap => dataptr(dst,local_index(dst,j),bx_to)
                         bp => dataptr(src,local_index(src,i),bx_from)
                         call cpy_d(ap,bp,filter=ml_restrict_copy_sum) ! ap = ap + bp
                      end if
                   else if ( local(src%la,i) ) then
                      !
                      ! We own src.
                      !
                      bp => dataptr(src,local_index(src,i),bx_from)
                      call parallel_send(bp, get_proc(get_layout(dst),j), TAG)
                   else
                      !
                      ! We own dst.
                      !
                      dims(1:dm) = extent(bx_from)
                      proc = get_proc(get_layout(src),i)
                      allocate(pt(dims(1),dims(2),dims(3),nc))
                      call parallel_recv(pt, proc, TAG)
                      if ( synced ) then
                         ap => dataptr(temp_dst,local_index(temp_dst,j),bx_to)
                         call cpy_d(ap,pt) ! ap = pt
                      else
                         ap => dataptr(dst,local_index(dst,j),bx_to)
                         call cpy_d(ap,pt,filter=ml_restrict_copy_sum) ! ap = ap + pt
                      end if
                      deallocate(pt)
                   end if
                end do

                deallocate(bisrc)
             end do

             deallocate(bidst)

             if ( synced ) call plus_plus(dst,temp_dst)
          end do
       end do
    end do

    if ( synced ) call destroy(temp_dst)

    call destroy(dstla)
    call destroy(srcla)

    call destroy(bpt)

  end subroutine periodic_add_copy

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine ml_restriction(crse, fine, mm_fine, mm_crse, ir, inject, zero_only)
    use bl_prof_module
    type(multifab),  intent(inout) :: fine
    type(multifab),  intent(inout) :: crse
    type(imultifab), intent(in   ) :: mm_fine
    type(imultifab), intent(in   ) :: mm_crse
    integer,         intent(in)    :: ir(:)
    logical,         intent(in), optional :: inject
    logical,         intent(in), optional :: zero_only
    type(bl_prof_timer), save :: bpt
    if ( ncomp(crse) .ne. ncomp(fine) ) then
       call bl_error('ml_restriction: crse & fine must have same # of components')
    end if
    call build(bpt, "ml_restriction")
    if ( nodal_q(fine) ) then
       call ml_nodal_restriction(crse, fine, mm_fine, mm_crse, ir, inject, zero_only)
    else
       call ml_cc_restriction(crse, fine, ir)
    end if
    call destroy(bpt)
 end subroutine ml_restriction

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

end module ml_restriction_module
