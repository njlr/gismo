/** @file gsVisitorCDR.h

    @brief Visitor for the convection-diffusion-reaction equation.

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): S. Kleiss
*/

#pragma once

namespace gismo
{

/** \brief Visitor for the convection-diffusion-reaction equation.
 *
 * Visitor for PDEs of the form\n
 * Find \f$ u: \mathbb R^d \rightarrow \mathbb R^d\f$
 * \f[ -\mathrm{div}( A \nabla u ) + b\cdot \nabla u + c u = f \f]
 * (+ boundary conditions), where\n
 * \f$ A \f$ (diffusion coefficient) is a \f$d\times d\f$-matrix,\n
 * \f$ b \f$ (convection velocity) is a \f$d\times 1\f$-vector,\n
 * \f$ c \f$ (reaction coefficient) is a scalar
 *
 * The coefficients are given as gsFunction with vector-valued return.\n
 * See the constructor gsVisitorCDR() for details on their format!
 *
 * Obviously, setting \f$ A= I\f$, \f$ b= 0\f$, and \f$c = 0\f$ results
 * in the special case of the Poisson equation.
 *
 */

template <class T>
class gsVisitorCDR
{
public:

        /** \brief Constructor for gsVisitorCDR, convection-diffusion-reaction
     *
     * \param[in] rhs Given right-hand-side function/source term that, for
     * each evaluation point, returns a scalar.
     * \param[in] coeff_A gsFunction that, for each evaluation point,
     * returns a vector of size \f$d^2\f$.
     * The entries of \f$A\f$ should appear in the order (for \f$d=3\f$)
     * \f$ (A_{11},A_{21},A_{31},A_{12},A_{22},A_{32},A_{13},A_{23},A_{33})^T\f$,
     * such that A.resize(d,d) returns the correct matrix.
     * \param[in] coeff_b gsFunction that, for each evaluation point,
     * returns a vector of size \f$d\f$.
     * \param[in] coeff_c gsFunction that, for each evaluation point,
     * returns a scalar.
     * \param[in] flagStabilization Specifies stabilization for the convection term.\n
     * 0: no stabilization\n
     * 1: SUPG-stabilization
     *
     */
    gsVisitorCDR(const gsPde<T> & pde)
    { 
        const gsConvDiffRePde<T>* cdr =
            static_cast<const gsConvDiffRePde<T>*>(&pde);
        
        coeff_A_ptr = cdr->diffusion ();
        coeff_b_ptr = cdr->convection();
        coeff_c_ptr = cdr->reaction  ();
        rhs_ptr     = cdr->rhs       ();

        flagStabType = 0;
        
        GISMO_ASSERT( rhs_ptr->targetDim() == 1 ,
                      "Not yet tested for multiple right-hand-sides");
    }

    gsVisitorCDR(const gsFunction<T> & rhs,
                 const gsFunction<T> & coeff_A,
                 const gsFunction<T> & coeff_b,
                 const gsFunction<T> & coeff_c,
                 unsigned flagStabilization = 1) :
        rhs_ptr(&rhs),
        coeff_A_ptr( & coeff_A),coeff_b_ptr( & coeff_b),coeff_c_ptr( & coeff_c),
        flagStabType( flagStabilization )
    {
        GISMO_ASSERT( rhs.targetDim() == 1 ,"Not yet tested for multiple right-hand-sides");
        GISMO_ASSERT( flagStabilization == 0 || flagStabilization == 1, "flagStabilization not known");

    }

    void initialize(const gsBasis<T> & basis,
                    const index_t patchIndex,
                    const gsOptionList & options,
                    gsQuadRule<T>    & rule,
                    unsigned         & evFlags )
    {
        // Setup Quadrature
        rule = gsGaussRule<T>(basis, options);// harmless slicing occurs here

        //flagStabType = static_cast<unsigned>(options.askSwitch("SUPG", false));
        flagStabType = static_cast<unsigned>(options.askInt("Stabilization", 0));
    
        // Set Geometry evaluation flags
        evFlags = NEED_VALUE | NEED_MEASURE | NEED_GRAD_TRANSFORM;
    }

    // Evaluate on element.
    inline void evaluate(gsBasis<T> const       & basis, // to do: more unknowns
                         gsGeometryEvaluator<T> & geoEval,
                         gsMatrix<T>            & quNodes)
    {
        // Compute the active basis functions
        // Assumes actives are the same for all quadrature points on the elements
        basis.active_into(quNodes.col(0), actives);
        numActive = actives.rows();
        
        // Evaluate basis functions on element
        basis.evalAllDers_into( quNodes, 2, basisData);
        
        // Compute image of Gauss nodes under geometry mapping as well as Jacobians
        geoEval.evaluateAt(quNodes);// is this generic ??
        
        // Evaluate the coefficients
        coeff_A_ptr->eval_into( geoEval.values(), coeff_A_vals );
        coeff_b_ptr->eval_into( geoEval.values(), coeff_b_vals );
        coeff_c_ptr->eval_into( geoEval.values(), coeff_c_vals );

        // Evaluate right-hand side at the geometry points
        rhs_ptr->eval_into( geoEval.values(), rhsVals ); // to do: parametric rhs ?
        
        // Initialize local matrix/rhs
        localMat.setZero(numActive, numActive      );
        localRhs.setZero(numActive, rhsVals.rows() );//multiple right-hand sides
    }

    
    inline void assemble(gsDomainIterator<T>    & element, 
                         gsGeometryEvaluator<T> & geoEval,
                         gsVector<T> const      & quWeights)
    {

        const index_t N = numActive;

        gsMatrix<T> & basisVals  = basisData[0];
        gsMatrix<T> & basisGrads = basisData[1];
        gsMatrix<T> & basis2ndDerivs = basisData[2];

        const unsigned d = element.dim();

        // supgMat will contain the contributions to the assembled matrix
        // that come from the SUPG stabilization. It is initialized whether
        // SUPG-stabilization is used or not, because the SUPG-parameter
        // has to be computed AFTER the loop over the quadrature points
        // (see below).
        gsMatrix<T> supgMat( localMat.rows(), localMat.cols() );
        supgMat.setZero();

        for (index_t k = 0; k < quWeights.rows(); ++k) // loop over quadrature nodes
        {
            // Multiply weight by the geometry measure
            const T weight = quWeights[k] * geoEval.measure(k);

            // Compute physical gradients at k as a Dim x numActive matrix
            geoEval.transformGradients(k, basisGrads, physBasisGrad);

            // ...just to make sure... tracking the dimensions of the terms involved...

            // d ... dim
            // N ... numActive

            // physBasisGrad : d x N
            // A.col(k)      : d^2 x 1
            // tmp_A         : d x d
            gsMatrix<T> tmp_A = coeff_A_vals.col(k);
            tmp_A.resize(d,d);

            // b.col(k)      : d x 1
            // physBasisGrad : d x N
            // tmp_b         : 1 x N
            gsMatrix<T> b_basisGrads = coeff_b_vals.col(k).transpose() * physBasisGrad;

            // basisVals.col(k): N x 1
            // rhsVals.col(k)  : 1 x 1
            // result:         : N x 1
            localRhs.noalias() += weight * ( basisVals.col(k) * rhsVals.col(k).transpose() ) ;

            // ( N x d ) * ( d x d ) * ( d x N ) = N x N
            localMat.noalias() += weight * (physBasisGrad.transpose() * ( tmp_A * physBasisGrad) );
            // ( N x 1 ) * ( 1 x N) = N x N
            localMat.noalias() += weight * (basisVals.col(k) * b_basisGrads);
            // ( scalar ) * ( N x 1 ) * ( 1 x N ) = N x N
            localMat.noalias() += weight * coeff_c_vals(0,k) * (basisVals.col(k) * basisVals.col(k).transpose());


            if( flagStabType == 1 ) // 1: SUPG
            {

                // auxiliary variable, number of second derivatives
                const index_t d2_offsetter = d*(d+1)/2;

                const typename gsMatrix<T>::constColumns J = geoEval.jacobian(k);
                gsMatrix<T> Jinv = J.inverse();

                gsMatrix<T> grad_b_basisGradsT(N,d);
                grad_b_basisGradsT.setZero();

                // loop over all basis functions
                for( index_t fct_i = 0; fct_i < N; ++fct_i  )
                {
                    gsMatrix<T> tmp_basis2ndDerivs(d,d);
                    tmp_basis2ndDerivs.setZero();

                    if( d == 2 )
                    {
                        tmp_basis2ndDerivs(0,0) = basis2ndDerivs(fct_i*d2_offsetter  , k);
                        tmp_basis2ndDerivs(0,1) = basis2ndDerivs(fct_i*d2_offsetter+2, k);
                        tmp_basis2ndDerivs(1,0) = basis2ndDerivs(fct_i*d2_offsetter+2, k);
                        tmp_basis2ndDerivs(1,1) = basis2ndDerivs(fct_i*d2_offsetter+1, k);
                    }
                    else if (d == 3 )
                    {
                        tmp_basis2ndDerivs(0,0) = basis2ndDerivs(fct_i*d2_offsetter, k);
                        tmp_basis2ndDerivs(0,1) = basis2ndDerivs(fct_i*d2_offsetter+3, k);
                        tmp_basis2ndDerivs(0,2) = basis2ndDerivs(fct_i*d2_offsetter+4, k);
                        tmp_basis2ndDerivs(1,0) = basis2ndDerivs(fct_i*d2_offsetter+3, k);
                        tmp_basis2ndDerivs(1,1) = basis2ndDerivs(fct_i*d2_offsetter+1, k);
                        tmp_basis2ndDerivs(1,2) = basis2ndDerivs(fct_i*d2_offsetter+5, k);
                        tmp_basis2ndDerivs(2,0) = basis2ndDerivs(fct_i*d2_offsetter+4, k);
                        tmp_basis2ndDerivs(2,1) = basis2ndDerivs(fct_i*d2_offsetter+5, k);
                        tmp_basis2ndDerivs(2,2) = basis2ndDerivs(fct_i*d2_offsetter+2, k);
                    }
                    else
                    {
                        GISMO_ERROR("What kind of dimension are you using? Should be 2 or 3.");
                    }
                    // d x d-matrix
                    tmp_basis2ndDerivs.noalias() = (Jinv.transpose() * tmp_basis2ndDerivs) * Jinv;


                    for( unsigned i = 0; i < d; ++i )
                        for( unsigned j = 0; j < d; ++j )
                            grad_b_basisGradsT(fct_i, i) += coeff_b_vals(j,k) * tmp_basis2ndDerivs(j,i);

                }

                supgMat.noalias() += weight * grad_b_basisGradsT * ( tmp_A * physBasisGrad );
                supgMat.noalias() += weight * (b_basisGrads.transpose() * b_basisGrads);
                supgMat.noalias() += weight * coeff_c_vals(0,k) * ( b_basisGrads.transpose() * basisVals.col(k).transpose());

            }
        }

        if( flagStabType == 1 ) // 1: SUPG
        {
            // Calling getSUPGParameter re-evaluates the gsGeometryEvaluator.
            // Thus, it has to be called AFTER geoEval has been used.
            T supgParam = getSUPGParameter( element.lowerCorner(),
                                            element.upperCorner(),
                                            geoEval);
            // Add the contributions from the SUPG-stabilization.
            localMat.noalias() += supgParam * supgMat;
        }
    }

    inline void localToGlobal(const int patchIndex,
                              const std::vector<gsMatrix<T> > & eliminatedDofs,
                              gsSparseSystem<T>     & system)
    {
        // Map patch-local DoFs to global DoFs
        system.mapColIndices(actives, patchIndex, actives);

        // Add contributions to the system matrix and right-hand side
        system.push(localMat, localRhs, actives, eliminatedDofs.front(), 0, 0);
    }

    T getSUPGParameter( const gsVector<T> & lo,
                        const gsVector<T> & up,
                        gsGeometryEvaluator<T> & geoEval )
    {
        const int N = 2;

        const index_t d = lo.size();

        gsMatrix<T> b_at_phys_pts;

        // compute the center point of the cell...

        // ...get the points map it to the physical space...
        gsMatrix<T> phys_pts = geoEval.values();
        // ...evaluate the convection coefficient there, ...
        coeff_b_ptr->eval_into( phys_pts, b_at_phys_pts );
        // ...and get it's norm.
        T b_norm = 0;
        for( int i=0; i < d; i++)
            b_norm += b_at_phys_pts(i,0) * b_at_phys_pts(i,0);
        b_norm = math::sqrt( b_norm );

        T SUPG_param = T(0.0);
        if( b_norm > 0 )
        {

            gsMatrix<T> bdryPts;
            gsMatrix<T> aMat;

            if( d == 2 )
            {
                int N1 = N+1;
                bdryPts.resize( 2, 4*N1 );
                aMat.resize( 2, 4*N1 );

                for( int i = 0; i <= N; ++i )
                {
                    T a = T(i)/T(N);
                    aMat(0,i) = a;
                    aMat(1,i) = T(0.0);
                    aMat(0,i+N1) = a;
                    aMat(1,i+N1) = T(1.0);

                    aMat(0,i+2*N1) = T(0.0);
                    aMat(1,i+2*N1) = a;
                    aMat(0,i+3*N1) = T(1.0);
                    aMat(1,i+3*N1) = a;
                }
            }
            else if( d == 3 )
            {

                GISMO_ASSERT(false,"NOT IMLEMENTED YET, Mark m271");

                /*
                bdryPts.resize( 3, 6*(N+1)*(N+1) );
                aMat.resize( 3, 6*(N+1)*(N+1) );

                int N1 = N+1;
                bdryPts.resize( 2, 4*N1 );
                aMat.resize( 2, 4*N1 );

                int ij = 0;
                for( int i = 0; i <= N; ++i )
                    for( int j = 0; j <= N; ++j )
                    {
                        T ai = T(i)/T(N);
                        T aj = T(j)/T(N);
                        aMat(0,ij) = T(0.0);
                        aMat(1,ij) = T(0.0);
                        aMat(2,ij) = T(0.0);
                        aMat(0,ij+N1) = T(1.0);
                        aMat(1,ij+N1) = T(1.0);
                        aMat(1,ij+N1) = T(1.0);

                    }
                */

            }
            else
            {
                GISMO_ASSERT(false,"WRONG DIMENSION. Mark m243");
            }


            for( index_t di = 0; di < d; ++di )
                for( index_t i = 0; i < aMat.cols(); ++i)
                {
                    bdryPts(di,i) = ( 1 - aMat(di,i) )*lo[di] + aMat(di,i) * up[di];
                }

            geoEval.evaluateAt( bdryPts );
            gsMatrix<T> b_proj = geoEval.values().transpose() * b_at_phys_pts;

            T b_proj_min = b_proj(0,0);
            T b_proj_max = b_proj(0,0);
            for( index_t i = 0; i < b_proj.size(); i++)
            {
                if( b_proj_min > b_proj(i) )
                    b_proj_min = b_proj(i);
                if( b_proj_max < b_proj(i) )
                    b_proj_max = b_proj(i);
            }
            SUPG_param = ( b_proj_max - b_proj_min ) / ( 2 * b_norm );
        }

        return SUPG_param;
    }



protected:
    // Right hand side
    const gsFunction<T> * rhs_ptr;
    // PDE Coefficient
    const gsFunction<T> * coeff_A_ptr;
    const gsFunction<T> * coeff_b_ptr;
    const gsFunction<T> * coeff_c_ptr;
    // flag for stabilization method
    unsigned flagStabType;

protected:
    // Basis values
    std::vector<gsMatrix<T> > basisData;
    gsMatrix<T>        physBasisGrad;
    gsMatrix<unsigned> actives;
    index_t numActive;

    gsMatrix<T> coeff_A_vals;
    gsMatrix<T> coeff_b_vals;
    gsMatrix<T> coeff_c_vals;

protected:
    // Local values of the right hand side
    gsMatrix<T> rhsVals;

protected:
    // Local matrices
    gsMatrix<T> localMat;
    gsMatrix<T> localRhs;
};


} // namespace gismo

