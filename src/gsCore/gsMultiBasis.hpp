/** @file gsMultiBasis.hpp

    @brief Provides declaration of MultiBasis class.

    This file is part of the G+Smo library. 

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    
    Author(s): A. Mantzaflaris
*/

#pragma once

#include <iterator>

#include <gsCore/gsMultiPatch.h>
#include <gsHSplines/gsHTensorBasis.h>
#include <gsUtils/gsCombinatorics.h>

namespace gismo
{

template<class T>
gsMultiBasis<T>::gsMultiBasis( const gsBasis<T> & bb )
: m_topology( bb.dim() )
{
    m_bases.push_back( bb.clone() );
    m_topology.addBox();
    m_topology.addAutoBoundaries();
}

template<class T>
gsMultiBasis<T>::gsMultiBasis( const gsMultiPatch<T> & mpatch )
: m_topology( mpatch )
{
    m_bases = mpatch.basesCopy();
}
  
template<class T>
gsMultiBasis<T>::gsMultiBasis( const gsMultiBasis& other )
: m_bases         ( other.m_bases.size() ),
  m_topology      ( other.m_topology     )
{
    cloneAll( other.m_bases.begin(), other.m_bases.end(),
              this->m_bases.begin() );
}

template<class T>
gsMultiBasis<T>::~gsMultiBasis()
{
    freeAll(m_bases);
}

template<class T>
std::ostream& gsMultiBasis<T>::print( std::ostream& os ) const
{
    gsInfo<<"Topology: "<< m_topology <<"\n";
    
    return os;
}

 
template<class T>
void gsMultiBasis<T>::addBasis( gsBasis<T> * & g ) 
{
    gsDebug<< "TO DO\n";
    if ( m_topology.dim() == -1 ) 
    {
        m_topology.setDim( g->dim() );
    } 
    else 
    {
        assert( g->dim() == m_topology.dim() );
    }
    m_bases.push_back( g ) ;
    m_topology.addBox();
    g = NULL;
}
  
template<class T>
int gsMultiBasis<T>::findBasisIndex( gsBasis<T>* g ) const {
    typename BasisContainer::const_iterator it
        = std::find( m_bases.begin(), m_bases.end(), g );
    assert( it != m_bases.end() );
    return it - m_bases.begin();
}
  
template<class T>
void gsMultiBasis<T>::addInterface( gsBasis<T>* g1, boxSide s1,
                                    gsBasis<T>* g2, boxSide s2 ) 
{
    int p1 = findBasisIndex( g1 );
    int p2 = findBasisIndex( g2 );
    m_topology.addInterface( p1, s1, p2, s2);
}
  

template<class T>
int gsMultiBasis<T>::maxDegree(int k) const
{
    GISMO_ASSERT(m_bases.size(), "Empty multibasis.");
    int result = m_bases[0]->degree(k);
    for (size_t i = 0; i < m_bases.size(); ++i)
        if (m_bases[i]->degree(k) > result )
            result = m_bases[i]->degree(k);
    return result;
}

template<class T>
int gsMultiBasis<T>::maxCwiseDegree() const
{
    GISMO_ASSERT(m_bases.size(), "Empty multibasis.");
    int result = m_bases[0]->maxDegree();
    for (size_t i = 0; i < m_bases.size(); ++i)
        result = math::max(m_bases[i]->maxDegree(), result);
    return result;
}

template<class T>
int gsMultiBasis<T>::minCwiseDegree() const
{
    GISMO_ASSERT(m_bases.size(), "Empty multibasis.");
    int result = m_bases[0]->minDegree();
    for (size_t i = 0; i < m_bases.size(); ++i)
        result = math::min(m_bases[i]->minDegree(), result);
    return result;
}

template<class T>
int gsMultiBasis<T>::minDegree(int k) const
{
    GISMO_ASSERT(m_bases.size(), "Empty multibasis.");
    int result = m_bases[0]->degree(k);
    for (size_t i = 0; i < m_bases.size(); ++i)
        if (m_bases[i]->degree(k) < result )
            result = m_bases[i]->degree(k);
    return result;
}

template<class T>
void gsMultiBasis<T>::getMapper(bool conforming, 
                                gsDofMapper & mapper, 
                                bool finalize) const
{
    mapper = gsDofMapper(*this);//.init(*this);
    
    if ( conforming )  // Conforming boundaries ?
    {
        for ( gsBoxTopology::const_iiterator it = m_topology.iBegin();
              it != m_topology.iEnd(); ++it )
        {
            matchInterface(*it,mapper);
        }
    }
    
    if (finalize)
        mapper.finalize();
}

template<class T>
void gsMultiBasis<T>::getMapper(bool conforming, 
                                const gsBoundaryConditions<T> & bc, 
                                int unk,
                                gsDofMapper & mapper, 
                                bool finalize) const
{
    mapper = gsDofMapper(*this, bc, unk); //.init(*this, bc, unk);
    
    if ( conforming ) // Conforming boundaries ?
    {
        for ( gsBoxTopology::const_iiterator it = m_topology.iBegin();
              it != m_topology.iEnd(); ++it )
        {
            matchInterface(*it,mapper);
        }
    }

    if (finalize)
        mapper.finalize();
}
    
template<class T>
void gsMultiBasis<T>::matchInterface(const boundaryInterface & bi, gsDofMapper & mapper) const
{
    // should work for all basis which have matchWith() implementeds
    gsMatrix<unsigned> b1, b2;
    m_bases[bi.first().patch]->matchWith(bi, *m_bases[bi.second().patch],
                                         b1, b2);

    // Match the dofs on the interface
    mapper.matchDofs(bi.first().patch, b1, bi.second().patch, b2 );
}

template<class T>
bool gsMultiBasis<T>::repairInterface( const boundaryInterface & bi )
{
    bool changed = false;

    std::vector<unsigned> refEltsFirst;
    std::vector<unsigned> refEltsSecond;

    // Find the areas/elements that do not match...
    switch( this->dim() )
    {
    case 2:
        changed = repairInterfaceFindElements<2>( bi, refEltsFirst, refEltsSecond );
        break;
    case 3:
        changed = repairInterfaceFindElements<3>( bi, refEltsFirst, refEltsSecond );
        break;
    default:
        GISMO_ASSERT(false,"wrong dimension");
    }

    // ...and if there are any found, refine the bases accordingly
    if( changed )
    {
        if( refEltsFirst.size() > 0 )
            m_bases[ bi.first().patch ]->refineElements( refEltsFirst );
        if( refEltsSecond.size() > 0 )
            m_bases[ bi.second().patch ]->refineElements( refEltsSecond );
    }

    return changed;
}

template<class T>
template<int d>
bool gsMultiBasis<T>::repairInterfaceFindElements(
        const boundaryInterface & bi,
        std::vector<unsigned> & refEltsFirst,
        std::vector<unsigned> & refEltsSecond )
{
    GISMO_ASSERT( d == 2 || d == 3, "Dimension must be 2 or 3.");

    refEltsFirst.clear();
    refEltsSecond.clear();

    // get direction and orientation maps
    const gsVector<bool> dirOrient = bi.dirOrientation();
    const gsVector<int> dirMap= bi.dirMap();

    // get the bases of both sides as gsHTensorBasis
    const gsHTensorBasis<d,T> * bas0 = dynamic_cast< const gsHTensorBasis<d,T> * >( m_bases[ bi.first().patch ] );
    const gsHTensorBasis<d,T> * bas1 = dynamic_cast< const gsHTensorBasis<d,T> * >( m_bases[ bi.second().patch ] );

    GISMO_ASSERT( bas0 != 0 && bas1 != 0, "Cannot cast basis as needed.");

    gsMatrix<unsigned> lo0;
    gsMatrix<unsigned> up0;
    gsVector<unsigned> level0;
    gsMatrix<unsigned> lo1;
    gsMatrix<unsigned> up1;
    gsVector<unsigned> level1;

    unsigned idxExponent;

    // get the higher one of both indexLevels
    unsigned indexLevelUse = ( bas0->tree().getIndexLevel() > bas1->tree().getIndexLevel() ? bas0->tree().getIndexLevel() : bas1->tree().getIndexLevel() );
    unsigned indexLevelDiff0 = indexLevelUse - bas0->tree().getIndexLevel();
    unsigned indexLevelDiff1 = indexLevelUse - bas1->tree().getIndexLevel();

    // get upper corners, but w.r.t. level "indexLevelUse"
    gsVector<unsigned> upperCorn0 = bas0->tree().upperCorner();
    gsVector<unsigned> upperCorn1 = bas1->tree().upperCorner();
    for( unsigned i=0; i < d; i++)
    {
        upperCorn0[i] = upperCorn0[i] << indexLevelDiff0;
        upperCorn1[i] = upperCorn1[i] << indexLevelDiff1;
    }

//    GISMO_ASSERT( upperCorn0[0] == upperCorn1[0] &&
//            upperCorn0[1] == upperCorn1[1], "The meshes are not matching as they should be!");
//    GISMO_ASSERT( (d<3) || (upperCorn0[2] == upperCorn1[2]),
//                  "The meshes are not matching as they should be!");

    // get the box-representation of the gsHDomain on the interface
    bas0->tree().getBoxesOnSide( bi.first().side(),  lo0, up0, level0);
    bas1->tree().getBoxesOnSide( bi.second().side(), lo1, up1, level1);

    // Compute the indices on the same level (indexLevelUse)
    idxExponent = ( indexLevelUse - bas0->tree().getMaxInsLevel());
    for( index_t i=0; i < index_t( lo0.rows() ); i++)
        for( unsigned j=0; j < d; j++)
        {
            lo0(i,j) = lo0(i,j) << idxExponent;
            up0(i,j) = up0(i,j) << idxExponent;
        }
    idxExponent = ( indexLevelUse - bas1->tree().getMaxInsLevel());
    for( index_t i=0; i < index_t( lo1.rows() ); i++)
        for( unsigned jj=0; jj < d; jj++)
        {
            // Computation done via dirMap, because...
            unsigned j = dirMap[jj];
            lo1(i,j) = lo1(i,j) << idxExponent;
            up1(i,j) = up1(i,j) << idxExponent;

            //... we also have to check whether the orientation
            // is preserved or not.
            if( !dirOrient[jj] )
            {
                unsigned tmp = upperCorn1[j] - lo1(i,j);
                lo1(i,j)  = upperCorn1[j] - up1(i,j);
                up1(i,j)  = tmp;
            }
        }

    // Find the merged interface mesh with
    // the respective levels.
    // Not efficient, but simple to implement.

    // a, b will correspond to the coordinate
    // directions which "span" the interface
    unsigned a0, b0, a1, b1;
    // c corresponds to the coordinate direction which
    // defines the interface-side by being set to 0 or 1
    unsigned c0, c1;
    switch( bi.first().direction() )
    {
    case 0:
        a0 = 1; b0 = 2; c0 = 0;
        break;
    case 1:
        a0 = 0; b0 = 2; c0 = 1;
        break;
    case 2:
        a0 = 0; b0 = 1; c0 = 2;
        break;
    default:
        a0 = 99; b0 = 99; c0 = 99;
        // Initialize for CDash, let it crash if this happens.
    }

    // If d == 2, the "b"'s are not needed.
    // Setting them to the "a"'s will
    // result in some steps and tests being repeated,
    // but the implementation not optimized w.r.t. efficiency.
    if( d == 2 )
        b0 = a0;

    a1 = dirMap[a0];
    b1 = dirMap[b0];
    c1 = dirMap[c0];

    // Run through all possible pairings of
    // boxes and see if they overlap.
    // If so, their overlap is a box of the merged
    // interface mesh.
    std::vector< std::vector<unsigned> > iU;
    for( index_t i0 = 0; i0 < index_t( lo0.rows() ); i0++)
        for( index_t i1 = 0; i1 < index_t( lo1.rows() ); i1++)
        {
            if(     lo0(i0,a0) < up1(i1,a1) &&
                    lo0(i0,b0) < up1(i1,b1) &&
                    lo1(i1,a1) < up0(i0,a0) &&
                    lo1(i1,b1) < up0(i0,b0) )// overlap
            {
                std::vector<unsigned> tmp;
                tmp.push_back( std::max( lo0(i0,a0), lo1(i1,a1) ) );
                tmp.push_back( std::max( lo0(i0,b0), lo1(i1,b1) ) ); // duplicate in 2D
                tmp.push_back( std::min( up0(i0,a0), up1(i1,a1) ) );
                tmp.push_back( std::min( up0(i0,b0), up1(i1,b1) ) ); // duplicate in 2D
                tmp.push_back( level0[i0] );
                tmp.push_back( level1[i1] );
                iU.push_back( tmp );
            }
        }

    std::vector<unsigned> tmpvec(1+2*d);
    for( size_t i = 0; i < size_t( iU.size() ); i++)
    {
        // the levels on both sides of the
        // box of the interface
        unsigned L0 = iU[i][4];
        unsigned L1 = iU[i][5];

        if( L0 != L1 ) // one side has to be refined
        {
            unsigned a, b, c, Luse;
            unsigned refSideIndex;
            unsigned upperCornOnLevel;

            if( L0 < L1 ) // refine first()
            {
                Luse = L1;
                a = a0;
                b = b0;
                c = c0;
                refSideIndex = bi.first().side().index();
                upperCornOnLevel = ( upperCorn0[c] >> ( indexLevelUse - Luse ) );
            }
            else // refine second()
            {
                Luse = L0;
                a = a1;
                b = b1;
                c = c1;
                refSideIndex = bi.second().side().index();
                upperCornOnLevel = ( upperCorn1[c] >> ( indexLevelUse - Luse ) );
            }

            // store the new level
            tmpvec[0] = Luse;
            // store the box on the interface that
            // has to be refined to that new level
            tmpvec[1+a] = iU[i][0] >> ( indexLevelUse - Luse );
            tmpvec[1+d+a] = iU[i][2] >> ( indexLevelUse - Luse );
            if( d == 3 )
            {
                tmpvec[1+b] = iU[i][1] >> ( indexLevelUse - Luse );
                tmpvec[1+d+b] = iU[i][3] >> ( indexLevelUse - Luse );
            }

            if( refSideIndex % 2 == 1 )
            {
                // west, south, front:
                tmpvec[1+c] = 0;
                tmpvec[1+d+c] = 1;
            }
            else
            {
                // east, north, back:
                tmpvec[1+c] = upperCornOnLevel-1;
                tmpvec[1+d+c] = upperCornOnLevel;
            }

            if( Luse == L1 ) // refine first
            {
                // no messing around with orientation and
                // maps needed.
                for( unsigned j=0; j < tmpvec.size(); j++)
                    refEltsFirst.push_back( tmpvec[j] );
            }
            else // refine second
            {
                // if the orientation is changed, flip where necessary
                for( unsigned jj = 0; jj < d; jj++)
                {
                    unsigned j = dirMap[jj];
                    if( j != c && !dirOrient[ jj ] )
                    {
                        upperCornOnLevel = ( upperCorn1[j] >> ( indexLevelUse - Luse ) );
                        unsigned tmp = tmpvec[1+j];
                        tmpvec[1+j]     = upperCornOnLevel - tmpvec[1+d+j];
                        tmpvec[1+d+j] = upperCornOnLevel - tmp;
                    }
                }

                for( unsigned j=0; j < (1+2*d); j++)
                    refEltsSecond.push_back( tmpvec[j] );
            }
        }
    }

    return ( ( refEltsFirst.size() > 0 ) || ( refEltsSecond.size() > 0 ) );
}


template<class T>
bool gsMultiBasis<T>::repairInterface2d( const boundaryInterface & bi )
{
    // get direction and orientation maps
    const gsVector<bool> dirOrient = bi.dirOrientation();

    // get the bases of both sides as gsHTensorBasis
    const gsHTensorBasis<2,T> * bas0 = dynamic_cast< const gsHTensorBasis<2,T> * >( m_bases[ bi.first().patch ] );
    const gsHTensorBasis<2,T> * bas1 = dynamic_cast< const gsHTensorBasis<2,T> * >( m_bases[ bi.second().patch ] );

    GISMO_ASSERT( bas0 != 0 && bas1 != 0, "Cannot cast basis as needed.");

    gsMatrix<unsigned> lo;
    gsMatrix<unsigned> up;
    gsVector<unsigned> level;

    unsigned idxExponent;

    // get the higher one of both indexLevels
    unsigned indexLevelUse = ( bas0->tree().getIndexLevel() > bas1->tree().getIndexLevel() ? bas0->tree().getIndexLevel() : bas1->tree().getIndexLevel() );
    unsigned indexLevelDiff0 = indexLevelUse - bas0->tree().getIndexLevel();
    unsigned indexLevelDiff1 = indexLevelUse - bas1->tree().getIndexLevel();

    // get the box-representation of the gsHDomain on the interface
    bas0->tree().getBoxesOnSide( bi.first().side(), lo, up, level);

    int dir0 = ( bi.first().direction() + 1 ) % 2;
    bool orientPreserv = dirOrient[ dir0 ];
    // for mapping the indices to the same
    idxExponent = ( indexLevelUse - bas0->tree().getMaxInsLevel());
    gsMatrix<unsigned> intfc0( lo.rows(), 3 );
    for( index_t i=0; i < lo.rows(); i++)
    {
        intfc0(i,0) = lo(i,dir0) << idxExponent;
        intfc0(i,1) = up(i,dir0) << idxExponent;
        intfc0(i,2) = level[i];
    }
    intfc0.sortByColumn(0);

    // get the box-representation of the gsHDomain on the interface
    bas1->tree().getBoxesOnSide( bi.second().side(), lo, up, level);
    int dir1 = ( bi.second().direction() + 1 ) % 2;
    idxExponent = ( indexLevelUse - bas1->tree().getMaxInsLevel());
    gsMatrix<unsigned> intfc1( lo.rows(), 3 );
    for( index_t i=0; i < lo.rows(); i++)
    {
        intfc1(i,0) = lo(i,dir1) << idxExponent;
        intfc1(i,1) = up(i,dir1) << idxExponent;
        intfc1(i,2) = level[i];
    }

    // now the knot indices in intfc0 and intfc1 both correspond to
    // numbering on level "indexLevelUse"

    // get upper corners, but w.r.t. level "indexLevelUse"
    gsVector<unsigned,2> upperCorn0 = bas0->tree().upperCorner();
    upperCorn0[0] = upperCorn0[0] << indexLevelDiff0;
    upperCorn0[1] = upperCorn0[1] << indexLevelDiff0;

    gsVector<unsigned,2> upperCorn1 = bas1->tree().upperCorner();
    upperCorn1[0] = upperCorn1[0] << indexLevelDiff1;
    upperCorn1[1] = upperCorn1[1] << indexLevelDiff1;

    if( !orientPreserv )
    {
        // flip the knot indices
        for( index_t i=0; i < lo.rows(); i++)
        {
            unsigned tmp = upperCorn1[dir1] - intfc1(i, 1);
            intfc1(i,1)  = upperCorn1[dir1] - intfc1(i, 0);
            intfc1(i,0)  = tmp;
        }
    }
    intfc1.sortByColumn(0);

    GISMO_ASSERT(intfc0( intfc0.rows()-1, 1) == intfc1( intfc1.rows()-1, 1)," Something wrong with interfaces! Mark 264");

    // Merge the knot spans from both sides into intfcU
    // intfcU[i][0]: end-knot-index
    // intfcU[i][1]: level on first()
    // intfcU[i][2]: level on second()
    int i0 = 0; int i1 = 0;
    std::vector< std::vector< unsigned > > intfcU;
    while( i0 < intfc0.rows() && i1 < intfc1.rows() )
    {
        std::vector<unsigned> tmp(3);

        if( intfc0( i0, 1 ) == intfc1( i1, 1 ) )
        {
            tmp[0] = intfc0(i0,1);
            tmp[1] = intfc0(i0,2);
            tmp[2] = intfc1(i1,2);
            intfcU.push_back( tmp );
            i0++;
            i1++;
        }
        else if( intfc0( i0, 1 ) > intfc1( i1, 1 ) )
        {
            tmp[0] = intfc1(i1,1);
            tmp[1] = intfc0(i0,2);
            tmp[2] = intfc1(i1,2);
            intfcU.push_back( tmp );
            i1++;
        }
        else
        {
            tmp[0] = intfc0(i0,1);
            tmp[1] = intfc0(i0,2);
            tmp[2] = intfc1(i1,2);
            intfcU.push_back( tmp );
            i0++;
        }
    }

    // create the refineboxes needed for
    // reparing the interface
    unsigned knot0;
    unsigned knot1 = 0;
    std::vector<unsigned> refElts0;
    std::vector<unsigned> refElts1;

    for( unsigned i=0; i < intfcU.size(); i++)
    {
        knot0 = knot1;
        knot1 = intfcU[i][0];
        unsigned L0 = intfcU[i][1];
        unsigned L1 = intfcU[i][2];

        if( L0 < L1 ) // refine first()
        {
            refElts0.push_back( L1 );

            // knot indices on level L1:
            unsigned knot0L = knot0 >> ( indexLevelUse - L1 );
            unsigned knot1L = knot1 >> ( indexLevelUse - L1 );

            unsigned upperCornOnLevel;
            switch( bi.first().side().index() )
            {
            case 1: // west
                refElts0.push_back( 0 );
                refElts0.push_back( knot0L );
                refElts0.push_back( 1 );
                refElts0.push_back( knot1L );
                break;
            case 2: // east
                upperCornOnLevel = ( upperCorn0[0] >> ( indexLevelUse - L1 ) );

                refElts0.push_back( upperCornOnLevel-1 );
                refElts0.push_back( knot0L );
                refElts0.push_back( upperCornOnLevel );
                refElts0.push_back( knot1L );
                break;
            case 3: // south
                refElts0.push_back( knot0L );
                refElts0.push_back( 0 );
                refElts0.push_back( knot1L );
                refElts0.push_back( 1 );
                break;
            case 4: // north
                upperCornOnLevel = ( upperCorn0[1] >> ( indexLevelUse - L1 ) );

                refElts0.push_back( knot0L );
                refElts0.push_back( upperCornOnLevel-1 );
                refElts0.push_back( knot1L );
                refElts0.push_back( upperCornOnLevel );
                break;
            default:
                GISMO_ASSERT(false,"3D not implemented yet. You can do it, if you want.");
                break;
            }
        }
        else if( L0 > L1 ) // refine second()
        {
            refElts1.push_back( L0 );

            // knot indices on level "indexLevelUse":
            unsigned knot0L = knot0;
            unsigned knot1L = knot1;
            // flip, if necessary
            if( !orientPreserv )
            {
                unsigned tmp = knot0L;
                knot0L = ( upperCorn1[dir1] - knot1L );
                knot1L = ( upperCorn1[dir1] - tmp );
            }
            // push to level L0
            knot0L = knot0L >> ( indexLevelUse - L0 );
            knot1L = knot1L >> ( indexLevelUse - L0 );

            gsVector<unsigned> upperCornOnLevel(2);
            switch( bi.second().side().index() )
            {
            case 1: // west
                refElts1.push_back( 0 );
                refElts1.push_back( knot0L );
                refElts1.push_back( 1 );
                refElts1.push_back( knot1L );
                break;
            case 2: // east
                upperCornOnLevel[0] = ( upperCorn1[0] >> ( indexLevelUse - L0 ) );
                upperCornOnLevel[1] = ( upperCorn1[1] >> ( indexLevelUse - L0 ) );

                refElts1.push_back( upperCornOnLevel[0]-1 );
                refElts1.push_back( knot0L );
                refElts1.push_back( upperCornOnLevel[0] );
                refElts1.push_back( knot1L );
                break;
            case 3: // south
                refElts1.push_back( knot0L );
                refElts1.push_back( 0 );
                refElts1.push_back( knot1L );
                refElts1.push_back( 1 );
                break;
            case 4: // north
                upperCornOnLevel[0] = ( upperCorn1[0] >> ( indexLevelUse - L0 ) );
                upperCornOnLevel[1] = ( upperCorn1[1] >> ( indexLevelUse - L0 ) );

                refElts1.push_back( knot0L );
                refElts1.push_back( upperCornOnLevel[1]-1 );
                refElts1.push_back( knot1L );
                refElts1.push_back( upperCornOnLevel[1] );
                break;
            default:
                GISMO_ASSERT(false,"3D not implemented yet. You can do it, if you want.");
                break;
            }
        }
    }

    if( refElts0.size() > 0 )
        m_bases[ bi.first().patch ]->refineElements( refElts0 );
    if( refElts1.size() > 0 )
        m_bases[ bi.second().patch ]->refineElements( refElts1 );

    return ( ( refElts0.size() > 0 ) || ( refElts1.size() > 0 ) );

}

} // namespace gismo
