/*
   Copyright (c) 2009-2013, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef MATRICES_REDHEFFER_HPP
#define MATRICES_REDHEFFER_HPP

namespace elem {

template<typename T> 
inline void
Redheffer( Matrix<T>& R, int n )
{
#ifndef RELEASE
    CallStackEntry entry("Redheffer");
#endif
    R.ResizeTo( n, n );
    for( int j=0; j<n; ++j )
    {
        for( int i=0; i<n; ++i )
        {
            if( j==0 || ((j+1)%(i+1))==0 )
                R.Set( i, j, T(1) );
            else
                R.Set( i, j, T(0) );
        }
    }
}

template<typename T,Distribution U,Distribution V>
inline void
Redheffer( DistMatrix<T,U,V>& R, int n )
{
#ifndef RELEASE
    CallStackEntry entry("Redheffer");
#endif
    R.ResizeTo( n, n );
    const int localHeight = R.LocalHeight();
    const int localWidth = R.LocalWidth();
    const int colShift = R.ColShift();
    const int rowShift = R.RowShift();
    const int colStride = R.ColStride();
    const int rowStride = R.RowStride();
    for( int jLocal=0; jLocal<localWidth; ++jLocal )
    {
        const int j = rowShift + jLocal*rowStride;
        for( int iLocal=0; iLocal<localHeight; ++iLocal )
        {
            const int i = colShift + iLocal*colStride;
            if( j==0 || ((j+1)%(i+1))==0 )
                R.SetLocal( iLocal, jLocal, T(1) );
            else
                R.SetLocal( iLocal, jLocal, T(0) );
        }
    }
}

} // namespace elem

#endif // ifndef MATRICES_REDHEFFER_HPP
