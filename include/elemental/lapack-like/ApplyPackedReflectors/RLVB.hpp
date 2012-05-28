/*
   Copyright (c) 2009-2012, Jack Poulson
   All rights reserved.

   This file is part of Elemental.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

    - Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    - Neither the name of the owner nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

namespace elem {

//
// Since applying Householder transforms from vectors stored right-to-left
// implies that we will be forming a generalization of
//
//   (I - tau_1 u_1 u_1^H) (I - tau_0 u_0 u_0^H) = 
//   I - tau_0 u_0 u_0^H - tau_1 u_1 u_1^H + (tau_0 tau_1 u_1^H u_0) u_1 u_0^H =
//   I - [ u_0, u_1 ] [  tau_0,                 0     ] [ u_0^H ]
//                    [ -tau_0 tau_1 u_1^H u_0, tau_1 ] [ u_1^H ],
//
// which has a lower-triangular center matrix, say S, we will form S as 
// the inverse of a matrix T, which can easily be formed as
// 
//   tril(T) = tril( U^H U ),  diag(T) = 1/t or 1/conj(t),
//
// where U is the matrix of Householder vectors and t is the vector of scalars.
//

template<typename R> 
inline void
internal::ApplyPackedReflectorsRLVB
( int offset, const Matrix<R>& H, Matrix<R>& A )
{
#ifndef RELEASE
    PushCallStack("internal::ApplyPackedReflectorsRLVB");
    if( offset > 0 || offset < -H.Height() )
        throw std::logic_error("Transforms out of bounds");
    if( H.Height() != A.Width() )
        throw std::logic_error
        ("Height of transforms must equal width of target matrix");
#endif
    Matrix<R>
        HTL, HTR,  H00, H01, H02,  HPan, HPanCopy,
        HBL, HBR,  H10, H11, H12,
                   H20, H21, H22;
    Matrix<R> ARight;

    Matrix<R> SInv, Z;

    LockedPartitionUpDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    while( HBR.Height() < H.Height() && HBR.Width() < H.Width() )
    {
        LockedRepartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        const int HPanHeight = H11.Height() + H21.Height();
        const int HPanWidth = 
            std::min( H11.Width(), std::max(HPanHeight+offset,0) );
        const int leftover = A.Width()-HPanHeight;
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        ARight.View( A, 0, leftover, A.Height(), HPanHeight );

        Zeros( ARight.Height(), HPanWidth, Z );
        Zeros( HPanWidth, HPanWidth, SInv );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        MakeTrapezoidal( LEFT, LOWER, offset, HPanCopy );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        Syrk( LOWER, TRANSPOSE, (R)1, HPanCopy, (R)0, SInv );
        HalveMainDiagonal( SInv );

        Gemm( NORMAL, NORMAL, (R)1, ARight, HPanCopy, (R)0, Z );
        Trsm( RIGHT, LOWER, NORMAL, NON_UNIT, (R)1, SInv, Z );
        Gemm( NORMAL, TRANSPOSE, (R)-1, Z, HPanCopy, (R)1, ARight );
        //--------------------------------------------------------------------//

        SlideLockedPartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, /**/ H01, H02,
         /*************/ /******************/
               /**/       H10, /**/ H11, H12,
          HBL, /**/ HBR,  H20, /**/ H21, H22 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}

template<typename R> 
inline void
internal::ApplyPackedReflectorsRLVB
( int offset, 
  const DistMatrix<R,MC,MR>& H,
        DistMatrix<R,MC,MR>& A )
{
#ifndef RELEASE
    PushCallStack("internal::ApplyPackedReflectorsRLVB");
    if( H.Grid() != A.Grid() )
        throw std::logic_error("{H,A} must be distributed over the same grid");
    if( offset > 0 || offset < -H.Height() )
        throw std::logic_error("Transforms out of bounds");
    if( H.Height() != A.Width() )
        throw std::logic_error
        ("Height of transforms must equal width of target matrix");
#endif
    const Grid& g = H.Grid();

    DistMatrix<R,MC,MR>
        HTL(g), HTR(g),  H00(g), H01(g), H02(g),  HPan(g), HPanCopy(g),
        HBL(g), HBR(g),  H10(g), H11(g), H12(g),
                         H20(g), H21(g), H22(g);
    DistMatrix<R,MC,MR> ARight(g);

    DistMatrix<R,VC,  STAR> HPan_VC_STAR(g);
    DistMatrix<R,MR,  STAR> HPan_MR_STAR(g);
    DistMatrix<R,STAR,STAR> SInv_STAR_STAR(g);
    DistMatrix<R,STAR,MC  > ZTrans_STAR_MC(g);
    DistMatrix<R,STAR,VC  > ZTrans_STAR_VC(g);

    LockedPartitionUpDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    while( HBR.Height() < H.Height() && HBR.Width() < H.Width() )
    {
        LockedRepartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        const int HPanHeight = H11.Height() + H21.Height();
        const int HPanWidth = 
            std::min( H11.Width(), std::max(HPanHeight+offset,0) );
        const int leftover = A.Width()-HPanHeight;
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        ARight.View( A, 0, leftover, A.Height(), HPanHeight );

        HPan_MR_STAR.AlignWith( ARight );
        ZTrans_STAR_MC.AlignWith( ARight );
        ZTrans_STAR_VC.AlignWith( ARight );
        Zeros( HPanWidth, ARight.Height(), ZTrans_STAR_MC );
        Zeros( HPanWidth, HPanWidth, SInv_STAR_STAR );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        MakeTrapezoidal( LEFT, LOWER, offset, HPanCopy );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        HPan_VC_STAR = HPanCopy;
        Syrk
        ( LOWER, TRANSPOSE, 
          (R)1, HPan_VC_STAR.LockedLocalMatrix(),
          (R)0, SInv_STAR_STAR.LocalMatrix() );     
        SInv_STAR_STAR.SumOverGrid();
        HalveMainDiagonal( SInv_STAR_STAR );

        HPan_MR_STAR = HPan_VC_STAR;
        internal::LocalGemm
        ( TRANSPOSE, TRANSPOSE,
          (R)1, HPan_MR_STAR, ARight, (R)0, ZTrans_STAR_MC );
        ZTrans_STAR_VC.SumScatterFrom( ZTrans_STAR_MC );
 
        internal::LocalTrsm
        ( LEFT, LOWER, TRANSPOSE, NON_UNIT,
          (R)1, SInv_STAR_STAR, ZTrans_STAR_VC );

        ZTrans_STAR_MC = ZTrans_STAR_VC;
        internal::LocalGemm
        ( TRANSPOSE, TRANSPOSE, 
          (R)-1, ZTrans_STAR_MC, HPan_MR_STAR, (R)1, ARight );
        //--------------------------------------------------------------------//
        HPan_MR_STAR.FreeAlignments();
        ZTrans_STAR_MC.FreeAlignments();
        ZTrans_STAR_VC.FreeAlignments();

        SlideLockedPartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, /**/ H01, H02,
         /*************/ /******************/
               /**/       H10, /**/ H11, H12,
          HBL, /**/ HBR,  H20, /**/ H21, H22 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}

template<typename R> 
inline void
internal::ApplyPackedReflectorsRLVB
( Conjugation conjugation, int offset, 
  const Matrix<Complex<R> >& H,
  const Matrix<Complex<R> >& t,
        Matrix<Complex<R> >& A )
{
#ifndef RELEASE
    PushCallStack("internal::ApplyPackedReflectorsRLVB");
    if( offset > 0 || offset < -H.Height() )
        throw std::logic_error("Transforms out of bounds");
    if( H.Height() != A.Width() )
        throw std::logic_error
        ("Height of transforms must equal width of target matrix");
    if( t.Height() != H.DiagonalLength( offset ) )
        throw std::logic_error("t must be the same length as H's offset diag");
#endif
    typedef Complex<R> C;

    Matrix<C>
        HTL, HTR,  H00, H01, H02,  HPan, HPanCopy,
        HBL, HBR,  H10, H11, H12,
                   H20, H21, H22;
    Matrix<C> ARight;
    Matrix<C>
        tT,  t0,
        tB,  t1,
             t2;
    
    Matrix<C> SInv, Z;

    LockedPartitionUpDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    LockedPartitionUp
    ( t, tT,
         tB, 0 );
    while( HBR.Height() < H.Height() && HBR.Width() < H.Width() )
    {
        LockedRepartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        const int HPanHeight = H11.Height() + H21.Height();
        const int HPanWidth = 
            std::min( H11.Width(), std::max(HPanHeight+offset,0) );
        const int leftover = A.Width()-HPanHeight;
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        LockedRepartitionUp
        ( tT,  t0,
               t1,
         /**/ /**/
          tB,  t2, HPanWidth );

        ARight.View( A, 0, leftover, A.Height(), HPanHeight );

        Zeros( ARight.Height(), HPan.Width(), Z );
        Zeros( HPan.Width(), HPan.Width(), SInv );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        MakeTrapezoidal( LEFT, LOWER, offset, HPanCopy );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        Herk( LOWER, ADJOINT, (C)1, HPanCopy, (C)0, SInv );
        FixDiagonal( conjugation, t1, SInv );

        Gemm( NORMAL, NORMAL, (C)1, ARight, HPanCopy, (C)0, Z );
        Trsm( RIGHT, LOWER, NORMAL, NON_UNIT, (C)1, SInv, Z );
        Gemm( NORMAL, ADJOINT, (C)-1, Z, HPanCopy, (C)1, ARight );
        //--------------------------------------------------------------------//

        SlideLockedPartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, /**/ H01, H02,
         /*************/ /******************/
               /**/       H10, /**/ H11, H12,
          HBL, /**/ HBR,  H20, /**/ H21, H22 );

        SlideLockedPartitionUp
        ( tT,  t0,
         /**/ /**/
               t1,
          tB,  t2 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}

template<typename R> 
inline void
internal::ApplyPackedReflectorsRLVB
( Conjugation conjugation, int offset, 
  const DistMatrix<Complex<R>,MC,MR  >& H,
  const DistMatrix<Complex<R>,MD,STAR>& t,
        DistMatrix<Complex<R>,MC,MR  >& A )
{
#ifndef RELEASE
    PushCallStack("internal::ApplyPackedReflectorsRLVB");
    if( H.Grid() != t.Grid() || t.Grid() != A.Grid() )
        throw std::logic_error
        ("{H,t,A} must be distributed over the same grid");
    if( offset > 0 || offset < -H.Height() )
        throw std::logic_error("Transforms out of bounds");
    if( H.Height() != A.Width() )
        throw std::logic_error
        ("Height of transforms must equal width of target matrix");
    if( t.Height() != H.DiagonalLength( offset ) )
        throw std::logic_error("t must be the same length as H's offset diag");
    if( !t.AlignedWithDiagonal( H, offset ) )
        throw std::logic_error("t must be aligned with H's 'offset' diagonal");
#endif
    typedef Complex<R> C;
    const Grid& g = H.Grid();

    DistMatrix<C,MC,MR>
        HTL(g), HTR(g),  H00(g), H01(g), H02(g),  HPan(g), HPanCopy(g),
        HBL(g), HBR(g),  H10(g), H11(g), H12(g),
                         H20(g), H21(g), H22(g);
    DistMatrix<C,MC,MR> ARight(g);
    DistMatrix<C,MD,STAR>
        tT(g),  t0(g),
        tB(g),  t1(g),
                t2(g);
    
    DistMatrix<C,VC,  STAR> HPan_VC_STAR(g);
    DistMatrix<C,MR,  STAR> HPan_MR_STAR(g);
    DistMatrix<C,STAR,STAR> t1_STAR_STAR(g);
    DistMatrix<C,STAR,STAR> SInv_STAR_STAR(g);
    DistMatrix<C,STAR,MC  > ZAdj_STAR_MC(g);
    DistMatrix<C,STAR,VC  > ZAdj_STAR_VC(g);

    LockedPartitionUpDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    LockedPartitionUp
    ( t, tT,
         tB, 0 );
    while( HBR.Height() < H.Height() && HBR.Width() < H.Width() )
    {
        LockedRepartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        const int HPanHeight = H11.Height() + H21.Height();
        const int HPanWidth = 
            std::min( H11.Width(), std::max(HPanHeight+offset,0) );
        const int leftover = A.Width()-HPanHeight;
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        LockedRepartitionUp
        ( tT,  t0,
               t1,
         /**/ /**/
          tB,  t2, HPanWidth );

        ARight.View( A, 0, leftover, A.Height(), HPanHeight );

        HPan_MR_STAR.AlignWith( ARight );
        ZAdj_STAR_MC.AlignWith( ARight );
        ZAdj_STAR_VC.AlignWith( ARight );
        Zeros( HPan.Width(), ARight.Height(), ZAdj_STAR_MC );
        Zeros( HPan.Width(), HPan.Width(), SInv_STAR_STAR );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        MakeTrapezoidal( LEFT, LOWER, offset, HPanCopy );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        HPan_VC_STAR = HPanCopy;
        Herk
        ( LOWER, ADJOINT, 
          (C)1, HPan_VC_STAR.LockedLocalMatrix(),
          (C)0, SInv_STAR_STAR.LocalMatrix() );     
        SInv_STAR_STAR.SumOverGrid();
        t1_STAR_STAR = t1;
        FixDiagonal( conjugation, t1_STAR_STAR, SInv_STAR_STAR );

        HPan_MR_STAR = HPan_VC_STAR;
        internal::LocalGemm
        ( ADJOINT, ADJOINT,
          (C)1, HPan_MR_STAR, ARight, (C)0, ZAdj_STAR_MC );
        ZAdj_STAR_VC.SumScatterFrom( ZAdj_STAR_MC );
 
        internal::LocalTrsm
        ( LEFT, LOWER, ADJOINT, NON_UNIT, (C)1, SInv_STAR_STAR, ZAdj_STAR_VC );

        ZAdj_STAR_MC = ZAdj_STAR_VC;
        internal::LocalGemm
        ( ADJOINT, ADJOINT, (C)-1, ZAdj_STAR_MC, HPan_MR_STAR, (C)1, ARight );
        //--------------------------------------------------------------------//
        HPan_MR_STAR.FreeAlignments();
        ZAdj_STAR_MC.FreeAlignments();
        ZAdj_STAR_VC.FreeAlignments();

        SlideLockedPartitionUpDiagonal
        ( HTL, /**/ HTR,  H00, /**/ H01, H02,
         /*************/ /******************/
               /**/       H10, /**/ H11, H12,
          HBL, /**/ HBR,  H20, /**/ H21, H22 );

        SlideLockedPartitionUp
        ( tT,  t0,
         /**/ /**/
               t1,
          tB,  t2 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}

} // namespace elem
