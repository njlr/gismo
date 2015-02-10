/** @file gsConjugateGradient.hpp

    @brief Conjugate gradient solver

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): C. Hofreither
*/

#pragma once

#include <gsSolver/gsIterativeSolver.h>

namespace gismo
{

/** The conjugate gradient implementation from Eigen, adapted to allow for more
 *  general preconditioners and better iteration control. Also capable of using
 *  a gsLinearOperator as matrix.
 *
 *
 *  Only implemented for single right hand side!
 */

class gsConjugateGradient : public gsIterativeSolver
{
public:
    typedef gsMatrix<real_t>    VectorType;

    /// Constructor for general linear operator
    gsConjugateGradient(const gsLinearOperator& _mat, int _maxIt=1000, real_t _tol=1e-10)
        : gsIterativeSolver(_mat, _maxIt, _tol){}

    /// Constructor for sparse matrix
    template<typename T, int _Options, typename _Index>
    gsConjugateGradient(const gsSparseMatrix<T, _Options, _Index > & _mat, index_t _maxIt=1000, real_t _tol=1e-10)
        : gsIterativeSolver(makeMatrixOperator(_mat.template selfadjointView<Lower>()), _maxIt, _tol) {}

    /// Constructor for dense matrix
    template<class T, int _Rows, int _Cols, int _Options>
    gsConjugateGradient(const gsMatrix<T, _Rows, _Cols, _Options> & _mat, index_t _maxIt=1000, real_t _tol=1e-10)
        : gsIterativeSolver(makeMatrixOperator(_mat.template selfadjointView<Lower>()), _maxIt, _tol) {}

    void initIteration(const VectorType& rhs, VectorType& x0, const gsLinearOperator& precond);

    void solve(const VectorType& rhs, VectorType& x, const gsLinearOperator& precond)
        {
            initIteration(rhs, x, precond);

            while(m_numIter < m_maxIters)
            {
                if (step(x, precond))
                    break;
                m_numIter++;
            }
            m_error = std::sqrt(residualNorm2 / rhsNorm2);
        }

    bool step( VectorType& x, const gsLinearOperator& precond );

private:
    using gsIterativeSolver::m_mat;
    using gsIterativeSolver::m_error;
    using gsIterativeSolver::m_maxIters;
    using gsIterativeSolver::m_numIter;
    using gsIterativeSolver::m_tol;

    VectorType z, tmp, tmp2, p;
    VectorType residual;
    real_t absNew, residualNorm2, threshold, rhsNorm2;
};

} // namespace gismo