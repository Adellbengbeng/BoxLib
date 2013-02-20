module nodal_restriction_module

  use bl_error_module
  use bl_types
  use bc_functions_module
  use impose_neumann_bcs_module

  implicit none

  real(dp_t), private, parameter :: ZERO = 0.0_dp_t
  real(dp_t), private, parameter :: ONE  = 1.0_dp_t
  real(dp_t), private, parameter :: TWO  = 2.0_dp_t
  real(dp_t), private, parameter :: HALF = 0.5_dp_t

contains

  subroutine nodal_zero_1d(cc, loc, mm_fine, lom_fine, lo, hi, ir)
    integer,    intent(in)    :: loc(:)
    real(dp_t), intent(inout) :: cc(loc(1):)
    integer,    intent(in)    :: lom_fine(:)
    integer,    intent(in)    :: mm_fine(lom_fine(1):)
    integer,    intent(in)    :: lo(:), hi(:)
    integer,    intent(in)    :: ir(:)

    integer :: i

    do i = lo(1),hi(1)
       if ( .not. bc_dirichlet(mm_fine(ir(1)*i),1,0) ) cc(i) = ZERO
    end do

  end subroutine nodal_zero_1d

  subroutine nodal_zero_2d(cc, loc, mm_fine, lom_fine, lo, hi, ir)
    integer,    intent(in)    :: loc(:)
    real(dp_t), intent(inout) :: cc(loc(1):,loc(2):)
    integer,    intent(in)    :: lom_fine(:)
    integer,    intent(in)    :: mm_fine(lom_fine(1):,lom_fine(2):)
    integer,    intent(in)    :: lo(:), hi(:)
    integer,    intent(in)    :: ir(:)

    integer :: i, j

    do j = lo(2),hi(2)
       do i = lo(1),hi(1)
          if ( .not. bc_dirichlet(mm_fine(ir(1)*i,ir(2)*j),1,0) ) cc(i,j) = ZERO
       end do
    end do

  end subroutine nodal_zero_2d

  subroutine nodal_zero_3d(cc, loc, mm_fine, lom_fine, lo, hi, ir)
    integer,    intent(in)    :: loc(:)
    real(dp_t), intent(inout) :: cc(loc(1):,loc(2):,loc(3):)
    integer,    intent(in)    :: lom_fine(:)
    integer,    intent(in)    :: mm_fine(lom_fine(1):,lom_fine(2):,lom_fine(3):)
    integer,    intent(in)    :: lo(:), hi(:)
    integer,    intent(in)    :: ir(:)

    integer :: i, j, k, ifine, jfine, kfine

    !$OMP PARALLEL DO PRIVATE(i,j,k,ifine,jfine,kfine)
    do k = lo(3),hi(3)
       kfine = ir(3)*k
       do j = lo(2),hi(2)
          jfine = ir(2)*j
          do i = lo(1),hi(1)
             ifine = ir(1)*i
             if ( .not. bc_dirichlet(mm_fine(ifine,jfine,kfine),1,0) ) cc(i,j,k) = ZERO
          end do
       end do
    end do
    !$OMP END PARALLEL DO

  end subroutine nodal_zero_3d

  subroutine nodal_restriction_1d(cc, loc, ff, lof, &
                                  mm_fine, lom_fine, mm_crse, lom_crse, &
                                  lo, hi, ir, inject, mg_restriction_mode)
    integer,    intent(in)    :: loc(:)
    integer,    intent(in)    :: lof(:)
    integer,    intent(in)    :: lom_fine(:)
    integer,    intent(in)    :: lom_crse(:)
    integer,    intent(in)    :: lo(:), hi(:)
    real(dp_t), intent(inout) :: cc(loc(1):)
    real(dp_t), intent(inout) :: ff(lof(1):)
    integer,    intent(in)    :: mm_fine(lom_fine(1):)
    integer,    intent(in)    :: mm_crse(lom_crse(1):)
    integer,    intent(in)    :: ir(:)
    logical,    intent(in)    :: inject
    integer,    intent(in)    :: mg_restriction_mode

    integer    :: hif, i, ifine, m, ng
    real(dp_t) :: fac, fac0

    hif = lof(1)+size(ff,dim=1)-1

    if ( inject ) then

       do i = lo(1),hi(1)
          cc(i) = ff(ir(1)*i)
       end do

    else if ( mg_restriction_mode == 1 ) then

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_1d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / ir(1)
       do m = 0, ir(1)-1
         fac = (ir(1)-m) * fac0
         if (m == 0) fac = HALF * fac
         do i = lo(1),hi(1)
           ifine = i*ir(1)
           if ( .not. bc_dirichlet(mm_fine(ifine),1,0) ) &
             cc(i) = cc(i) + fac * (ff(ifine-m) + ff(ifine+m))
         end do
       end do

    else 

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_1d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / ir(1)
       do m = 0, ir(1)-1
         fac = (ir(1)-m) * fac0
         if ( m == 0 ) fac = HALF * fac
         do i = lo(1),hi(1)
           ifine = i*ir(1)
           if ( .not. bc_dirichlet(mm_fine(ifine),1,0) ) then
              if ( ifine == lof(1)+1 ) then
                 cc(i) = cc(i) + fac * ff(ifine+m)
              else if ( ifine == hif-1 ) then
                 cc(i) = cc(i) + fac * ff(ifine-m)
              else
                 cc(i) = cc(i) + fac * (ff(ifine-m) + ff(ifine+m))
              end if
           end if
         end do
       end do

    end if

    i = lo(1)
    if (bc_dirichlet(mm_crse(i),1,0)) then
      cc(i) = ZERO
    else if (bc_neumann(mm_crse(i),1,-1)) then
      cc(i) = TWO*cc(i)
    end if

    i = hi(1)
    if (bc_dirichlet(mm_crse(i),1,0)) then
      cc(i) = ZERO
    else if (bc_neumann(mm_crse(i),1,+1)) then
      cc(i) = TWO*cc(i)
    end if

  end subroutine nodal_restriction_1d

  subroutine nodal_restriction_2d(cc, loc, ff, lof, &
                                  mm_fine, lom_fine, mm_crse, lom_crse, &
                                  lo, hi, ir, inject, mg_restriction_mode)
    integer,    intent(in)    :: loc(:)
    integer,    intent(in)    :: lof(:)
    integer,    intent(in)    :: lom_fine(:)
    integer,    intent(in)    :: lom_crse(:)
    integer,    intent(in)    :: lo(:), hi(:)
    real(dp_t), intent(inout) :: ff(lof(1):,lof(2):)
    real(dp_t), intent(inout) :: cc(loc(1):,loc(2):)
    integer,    intent(in)    :: mm_fine(lom_fine(1):,lom_fine(2):)
    integer,    intent(in)    :: mm_crse(lom_crse(1):,lom_crse(2):)
    integer,    intent(in)    :: ir(:)
    logical,    intent(in)    :: inject
    integer,    intent(in)    :: mg_restriction_mode

    integer    :: i, j, ifine, jfine, m, n, ng
    integer    :: ileft,irght,jbot,jtop
    integer    :: hif(2)
    real(dp_t) :: fac,fac0,fac1
    logical    :: add_lo_x, add_lo_y, add_hi_x, add_hi_y

    hif(1) = lof(1)+size(ff,dim=1)-1
    hif(2) = lof(2)+size(ff,dim=2)-1
    
    ng = lom_fine(1) - lof(1)
    call impose_neumann_bcs_2d(ff,mm_fine,lom_fine,ng)

    if ( inject ) then

       do j = lo(2),hi(2)
          do i = lo(1),hi(1)
             cc(i,j) = ff(ir(1)*i,ir(2)*j)
          end do
       end do

    else if ( mg_restriction_mode == 1 ) then

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_2d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / (ir(1)*ir(2))
       do n = 0, ir(2)-1
         fac1 = (ir(2)-n) * fac0
         if (n == 0) fac1 = HALF * fac1
         do m = 0, ir(1)-1
            fac = (ir(1)-m) * fac1
            if (m == 0) fac = HALF * fac
            do j = lo(2),hi(2)
               jfine = j*ir(2)
               do i = lo(1),hi(1)
                  ifine = i*ir(1)
                  if (.not. bc_dirichlet(mm_fine(ifine,jfine),1,0)) then
                    cc(i,j) = cc(i,j) &
                       + fac*ff(ifine-m,jfine-n) &
                       + fac*ff(ifine+m,jfine-n) &
                       + fac*ff(ifine-m,jfine+n) &
                       + fac*ff(ifine+m,jfine+n)
                  end if
               end do
            end do
         end do
       end do

    else

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_2d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / (ir(1)*ir(2))
       do n = 0, ir(2)-1
         fac1 = (ir(2)-n) * fac0
         if (n == 0) fac1 = HALF * fac1
         do m = 0, ir(1)-1
            fac = (ir(1)-m) * fac1
            if (m == 0) fac = HALF * fac
            do j = lo(2),hi(2)
              jfine = j*ir(2)
              do i = lo(1),hi(1)
               add_lo_x = .true.
               add_lo_y = .true.
               add_hi_x = .true.
               add_hi_y = .true.

               ifine = i*ir(1)
               ileft = ifine-m
               irght = ifine+m
               jbot = jfine-n
               jtop = jfine+n

               if (.not. bc_dirichlet(mm_fine(ifine,jfine),1,0)) then

                  if (ifine == lof(1)+1) then
                     if (bc_neumann(mm_fine(ifine,jfine),1,-1)) then
                        ileft = irght
                     else
                        add_lo_x = .false.
                     end if
                  end if
                  if (jfine == lof(2)+1) then
                     if (bc_neumann(mm_fine(ifine,jfine),2,-1)) then
                        jbot = jtop
                     else
                        add_lo_y = .false.
                     end if
                  end if
                  if (ifine == hif(1)-1) then
                     if (bc_neumann(mm_fine(ifine,jfine),1,+1)) then
                        irght = ileft
                     else
                        add_hi_x = .false.
                     end if
                  end if
                  if (jfine == hif(2)-1) then
                     if (bc_neumann(mm_fine(ifine,jfine),2,+1)) then
                        jtop = jbot
                     else
                        add_hi_y = .false.
                     end if
                  end if

                 if (add_lo_x .and. add_lo_y) cc(i,j) = cc(i,j) + fac*ff(ileft,jbot)
                 if (add_hi_x .and. add_lo_y) cc(i,j) = cc(i,j) + fac*ff(irght,jbot)
                 if (add_lo_x .and. add_hi_y) cc(i,j) = cc(i,j) + fac*ff(ileft,jtop)
                 if (add_hi_x .and. add_hi_y) cc(i,j) = cc(i,j) + fac*ff(irght,jtop)

               end if
            end do
           end do
         end do
       end do

    end if

    do j = lo(2),hi(2)
      if (bc_dirichlet(mm_crse(lo(1),j),1,0)) cc(lo(1),j) = ZERO
      if (bc_dirichlet(mm_crse(hi(1),j),1,0)) cc(hi(1),j) = ZERO
    end do

    do i = lo(1),hi(1)
      if (bc_dirichlet(mm_crse(i,lo(2)),1,0)) cc(i,lo(2)) = ZERO
      if (bc_dirichlet(mm_crse(i,hi(2)),1,0)) cc(i,hi(2)) = ZERO
    end do

  end subroutine nodal_restriction_2d

  subroutine nodal_restriction_3d(cc, loc, ff, lof, &
                                  mm_fine, lom_fine, mm_crse, lom_crse, &
                                  lo, hi, ir, inject, mg_restriction_mode)
    integer,    intent(in   ) :: loc(:)
    integer,    intent(in   ) :: lof(:)
    integer,    intent(in   ) :: lom_fine(:)
    integer,    intent(in   ) :: lom_crse(:)
    integer,    intent(in   ) :: lo(:),hi(:)
    real(dp_t), intent(inout) :: ff(lof(1):,lof(2):,lof(3):)
    real(dp_t), intent(inout) :: cc(loc(1):,loc(2):,loc(3):)
    integer,    intent(in   ) :: mm_fine(lom_fine(1):,lom_fine(2):,lom_fine(3):)
    integer,    intent(in   ) :: mm_crse(lom_crse(1):,lom_crse(2):,lom_crse(3):)
    integer,    intent(in   ) :: ir(:)
    logical,    intent(in   ) :: inject
    integer,    intent(in   ) :: mg_restriction_mode

    integer    :: i, j, k, l, m, n, ng
    integer    :: ifine, jfine, kfine
    integer    :: ileft, irght, jbot, jtop, kdwn, kup
    integer    :: hif(3)
    real(dp_t) :: fac, fac0, fac1, fac2
    logical    :: add_lo_x, add_lo_y, add_lo_z, add_hi_x, add_hi_y, add_hi_z
    logical    :: doit, jface, kface

    hif(1) = lof(1)+size(ff,dim=1)-1
    hif(2) = lof(2)+size(ff,dim=2)-1
    hif(3) = lof(3)+size(ff,dim=3)-1

    if ( inject ) then

       do k = lo(3),hi(3)
          do j = lo(2),hi(2)
             do i = lo(1),hi(1)
                cc(i,j,k) = ff(ir(1)*i,ir(2)*j,ir(3)*k)
             end do
          end do
       end do

    else if ( mg_restriction_mode == 1 ) then

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_3d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / (ir(1)*ir(2)*ir(3))
       do l = 0, ir(3)-1
          fac2 = (ir(3)-l) * fac0
          if (l == 0) fac2 = HALF * fac2
          do n = 0, ir(2)-1
             fac1 = (ir(2)-n) * fac2
             if (n == 0) fac1 = HALF * fac1
             do m = 0, ir(1)-1
                fac = (ir(1)-m) * fac1
                if (m == 0) fac = HALF * fac
                !$OMP PARALLEL DO PRIVATE(i,j,k,ifine,jfine,kfine,jface,kface,doit)
                do k = lo(3),hi(3)
                   kfine = k*ir(3)

                   kface = .false. ; if ( (k.eq.lo(3)) .or. (k.eq.hi(3)) ) kface = .true.

                   do j = lo(2),hi(2)
                      jfine = j*ir(2)

                      jface = .false. ; if ( (j.eq.lo(2)) .or. (j.eq.hi(2)) ) jface = .true.

                      do i = lo(1),hi(1)
                         ifine = i*ir(1)

                         doit = .true.

                         if ( jface .or. kface .or. (i.eq.lo(1)) .or. (i.eq.hi(1)) ) then
                            if (bc_dirichlet(mm_fine(ifine,jfine,kfine),1,0)) doit = .false.
                         end if

                         if (doit) then
                            cc(i,j,k) = cc(i,j,k) &
                                 + fac*ff(ifine-m,jfine-n,kfine-l) &
                                 + fac*ff(ifine+m,jfine-n,kfine-l) &
                                 + fac*ff(ifine-m,jfine+n,kfine-l) &
                                 + fac*ff(ifine+m,jfine+n,kfine-l) &
                                 + fac*ff(ifine-m,jfine-n,kfine+l) &
                                 + fac*ff(ifine+m,jfine-n,kfine+l) &
                                 + fac*ff(ifine-m,jfine+n,kfine+l) &
                                 + fac*ff(ifine+m,jfine+n,kfine+l)
                         end if
                      end do
                   end do
                end do
                !$OMP END PARALLEL DO
             end do
          end do
       end do

    else

       ng = lom_fine(1) - lof(1)
       call impose_neumann_bcs_3d(ff,mm_fine,lom_fine,ng)

       fac0 = 1.0_dp_t / (ir(1)*ir(2)*ir(3))
       do l = 0, ir(3)-1
          fac2 = (ir(3)-l) * fac0
          if (l == 0) fac2 = HALF * fac2
          do n = 0, ir(2)-1
             fac1 = (ir(2)-n) * fac2
             if (n == 0) fac1 = HALF * fac1
             do m = 0, ir(1)-1
                fac = (ir(1)-m) * fac1
                if (m == 0) fac = HALF * fac
                !$OMP PARALLEL DO PRIVATE(i,j,k,ifine,jfine,kfine) &
                !$OMP PRIVATE(add_lo_x,add_lo_y,add_lo_z,add_hi_x,add_hi_y,add_hi_z) &
                !$OMP PRIVATE(ileft,irght,jbot,jtop,kdwn,kup,jface,kface,doit)
                do k = lo(3),hi(3)
                   kfine = k*ir(3)

                   kface = .false. ; if ( (k.eq.lo(3)) .or. (k.eq.hi(3)) ) kface = .true.

                   do j = lo(2),hi(2)
                      jfine = j*ir(2)

                      jface = .false. ; if ( (j.eq.lo(2)) .or. (j.eq.hi(2)) ) jface = .true.

                      do i = lo(1),hi(1)
                         ifine = i*ir(1)

                         doit = .true.

                         if ( jface .or. kface .or. (i.eq.lo(1)) .or. (i.eq.hi(1)) ) then
                            if (bc_dirichlet(mm_fine(ifine,jfine,kfine),1,0)) doit = .false.
                         end if

                         add_lo_x = .true.
                         add_lo_y = .true.
                         add_lo_z = .true.
                         add_hi_x = .true.
                         add_hi_y = .true.
                         add_hi_z = .true.

                         if (doit) then

                            ileft = ifine-m
                            irght = ifine+m
                            jbot  = jfine-n
                            jtop  = jfine+n
                            kdwn  = kfine-l
                            kup   = kfine+l

                            if (ifine == lof(1)+1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),1,-1)) then
                                  ileft = irght
                               else
                                  add_lo_x = .false. 
                               end if
                            end if
                            if (jfine == lof(2)+1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),2,-1)) then
                                  jbot = jtop
                               else
                                  add_lo_y = .false.
                               end if
                            end if
                            if (kfine == lof(3)+1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),3,-1)) then
                                  kdwn = kup
                               else
                                  add_lo_z = .false.
                               end if
                            end if
                            if (ifine == hif(1)-1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),1,+1)) then
                                  irght = ileft
                               else
                                  add_hi_x = .false.
                               end if
                            end if
                            if (jfine == hif(2)-1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),2,+1)) then
                                  jtop = jbot
                               else
                                  add_hi_y = .false.
                               end if
                            end if
                            if (kfine == hif(3)-1) then
                               if (bc_neumann(mm_fine(ifine,jfine,kfine),3,+1)) then
                                  kup = kdwn
                               else
                                  add_hi_z = .false.
                               end if
                            end if

                            if (add_lo_z) then
                               if (add_lo_x .and. add_lo_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(ileft,jbot,kdwn)
                               if (add_hi_x .and. add_lo_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(irght,jbot,kdwn)
                               if (add_lo_x .and. add_hi_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(ileft,jtop,kdwn)
                               if (add_hi_x .and. add_hi_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(irght,jtop,kdwn)
                            end if

                            if (add_hi_z) then
                               if (add_lo_x .and. add_lo_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(ileft,jbot,kup)
                               if (add_hi_x .and. add_lo_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(irght,jbot,kup)
                               if (add_lo_x .and. add_hi_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(ileft,jtop,kup)
                               if (add_hi_x .and. add_hi_y) &
                                    cc(i,j,k) = cc(i,j,k) + fac*ff(irght,jtop,kup)
                            end if

                         end if
                      end do
                   end do
                end do
                !$OMP END PARALLEL DO
             end do
          end do
       end do

    end if

    do k = lo(3),hi(3)
       do j = lo(2),hi(2)
          if (bc_dirichlet(mm_crse(lo(1),j,k),1,0)) cc(lo(1),j,k) = ZERO
          if (bc_dirichlet(mm_crse(hi(1),j,k),1,0)) cc(hi(1),j,k) = ZERO
       end do
    end do

    do k = lo(3),hi(3)
       do i = lo(1),hi(1)
          if (bc_dirichlet(mm_crse(i,lo(2),k),1,0)) cc(i,lo(2),k) = ZERO
          if (bc_dirichlet(mm_crse(i,hi(2),k),1,0)) cc(i,hi(2),k) = ZERO
       end do
    end do

    do j = lo(2),hi(2)
       do i = lo(1),hi(1)
          if (bc_dirichlet(mm_crse(i,j,lo(3)),1,0)) cc(i,j,lo(3)) = ZERO
          if (bc_dirichlet(mm_crse(i,j,hi(3)),1,0)) cc(i,j,hi(3)) = ZERO
       end do
    end do

  end subroutine nodal_restriction_3d

end module nodal_restriction_module
